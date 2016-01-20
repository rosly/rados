/*
 * This file is a part of RadOs project
 * Copyright (c) 2013, Radoslaw Biernacki <radoslaw.biernacki@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2) Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3) No personal names or organizations' names associated with the 'RadOs'
 *    project may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE RADOS PROJECT AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "os_private.h"

#define OS_TIMER_MAGIC1 ((uint_fast16_t)0xAABB)
#define OS_TIMER_MAGIC2 ((uint_fast16_t)0xCCDD)

/** Global monotonic counter of system ticks */
os_ticks_t os_ticks_cnt = 0;

/** Global list of all timers. Timers are sorted by time which remain until burn
 *  off */
static list_t os_timer_list;

/** Number of ticks for first timer burn off. It is decreased each system tick
 *  and only when it reach 0 the timer list is iterated. This saves the CPU
 *  cycles durring os_timer_tick() */
static os_ticks_t os_timer_first_rem = 0;

/* --= forward decarations =-- */
static void OS_HOT os_timer_add(os_timer_t *add_timer);
static void OS_HOT os_timer_trigger(void);

/** Module initialization function, can be called only from os_start() */
void OS_COLD os_timers_init(void)
{
   list_init(&os_timer_list);
}

void os_timer_create(
   os_timer_t *timer,
   timer_proc_t clbck,
   void *param,
   os_ticks_t timeout_ticks,
   os_ticks_t reload_ticks)
{
   arch_criticalstate_t cristate;

   /* timeout must be at least 1 tick in future */
   OS_ASSERT(timeout_ticks > 0);
   /* prevent from double usage of already initialized timer */
   OS_ASSERT(timer->magic != OS_TIMER_MAGIC1);

   /* currently I assume that timers may be created from ISR, but I'm not sure
    * for 100% if this will not broke something \TODO check that!*/

   //memset(timer, 0, sizeof(os_timer_t));
   list_init(&(timer->list));
   timer->ticks_rem = timeout_ticks;
   timer->ticks_reload = reload_ticks;
   timer->clbck = clbck;
   timer->param = param;
#ifdef OS_CONFIG_APICHECK
   timer->magic = OS_TIMER_MAGIC1;
#endif

   /* timer list and os_timer_first_rem are accessed from ISR, we need to
    * disable the interrupts */
   arch_critical_enter(cristate);

   /* do we have some timer pending already */
   if (os_timer_first_rem > 0) {
      /* check if the created timer will burn off before first timer */
      if (os_timer_first_rem < timer->ticks_rem) {
          os_timer_trigger(); /* recalculate the timers remaining time */
          os_timer_add(timer);
          os_timer_first_rem = timer->ticks_rem; /* this timer is first */
      } else {
         /* otherwise we only have to compensate the timeout of created timer by
          * the value of ticks which already passed in from point of view of
          * firts timer on the list */
         timer->ticks_rem += os_timer_list->next->ticks_rem - os_timer_first_rem;
         os_timer_add(timer);
      }
   }

   arch_critical_exit(cristate);
}

void os_timer_destroy(os_timer_t *timer)
{
   arch_criticalstate_t cristate;
   os_timer_t *first;

   /* prevent double usage of initialized timer */
   OS_ASSERT((timer->magic == OS_TIMER_MAGIC1) ||
             (timer->magic == OS_TIMER_MAGIC2));

   /* timer list is iterated from ISR, we need to disable the interrupts */
   arch_critical_enter(cristate);

   /* only still active timers need some actions
    * note that this function is designed in way, that as long as the memory for
    * timer is valid it allows for multiple destroy operations on the same timer
    * */
   if (timer->ticks_rem > 0) {

      /* check if the removed timer is the first one */
      first = os_container_of(list_peekfirst(&timers), os_timer_t, list);
      if (first == timer) {
          os_timer_trigger(); /* recalculate the timers remaining time */
          first = os_container_of(list_peekfirst(&timers), os_timer_t, list);
          if (first)
             os_timer_first_rem = timer->ticks_rem;
          else
             os_timer_first_rem = 0;
      }

      /* detach from active timer list */
      list_unlink(&(timer->list));
      /* marking timer as expired, prevents double destroy */
      timer->ticks_rem = 0;
      /* clearing the auto-reload field, this will allow for safe destroy of
       * timers from the timer_callback (timer will not be restarted when this
       * field is 0) */
      timer->ticks_reload = 0;
   }

#ifdef OS_CONFIG_APICHECK
   /* obstruct magic, mark that this timer was successfully destroyed */
   timer->magic = OS_TIMER_MAGIC2;
#endif

   arch_critical_exit(cristate);
}

void OS_HOT os_tick(void)
{
   OS_ASSERT(isr_nesting > 0);   /* this function may be called only form ISR */

   /* Increment system global monotonic ticks counter Overflow scenario are
    * handled by code in os_ticks_now() and os_ticks_diff() */
   ++os_ticks_cnt;

   if (os_timer_first_rem > 0) {

       /* decrease the copy of ticks_rem of first timer and check if it is a
        * time to iterate over the timer list */
       if ((--os_timer_first_rem) == 0)
         os_timer_trigger();
   }

   /* switch to other READY task which has the same or greater priority (0 as
    * param of os_schedule() means just that) */
   os_schedule(0);
}

/** Function add the timer to the timer list. Function keeps the timer list
 *  sorted by remaining time of the timers */
static void OS_HOT os_timer_add(os_timer_t *add_timer)
{
   list_t *itr;
   os_timer_t *itr_timer;

   itr = list_itr_begin(&os_timer_list);
   while (false == list_itr_end(&os_timer_list, itr)) {
      itr_timer = os_container_of(itr, os_timer_t, list);
      if (itr_timer->ticks_rem > add_timer->ticks_rem)
         break;
      itr = itr->next;
   }
   list_put_before(itr, &(add_timer->list));
}

/**
 * This function will be called when os_timer_first_rem will reach 0.
 * Function iterate over all timers and decrease its remaining time by certain
 * value. This value is initial time of the first timer because we use it as the
 * starting valu of os_timer_firts_rem.
 * Function triggers the timers which had timeouted (timer->ticks_rem == 0) and
 * rearms them in case they are auto reloaded */
static void OS_HOT os_timer_trigger(void)
{
   list_t *itr;
   os_timer_t *itr_timer;
   list_t list_autoreload;

   /* for auto reloaded timers use temporary list */
   list_init(&list_autoreload);
   itr = list_itr_begin(&os_timer_list);
   while (false == list_itr_end(&os_timer_list, itr)) {
      itr_timer = os_container_of(itr, os_timer_t, list);
      /* the list will be modified, calculate  pointer the next element */
      itr = itr->next;
      if ((itr_timer->ticks_rem -= timer_tick_unsynch) > 0) {
         /* current timer does not burn off yet, following timers wont trigger
          * either but we continue since w have to synchronize all of the timers
          **/
         continue;
      }

      /* in any case (auto reload or single shot) remove timer from active list */
      list_unlink(&(itr_timer->list));

      /* call the timer callback, from this callback it is allowed to call the
       * os_timer_destroy() */
      itr_timer->clbck(itr_timer->param);

      /* check if it is marked as auto reload.
       * timer callback might destroy the timer but in tis case tick_reload will
       * be 0 */
      if (itr_timer->ticks_reload > 0) {
         /* timer is auto reload, put the timer to temporary auto reload list */
         list_append(&list_autoreload, &(itr_timer->list));
      }
   }

   timer_tick_unsynch = 0;

   /* Now re-add all auto reload timers from temporary list.
    * We need a temporary list because we keep all timers sorted, and we cannot
    * figure out the position of timers which we auto reload until we finish
    * processing of previous trigger sequence */
   while ((itr = list_detachfirst(&list_autoreload))) {
      itr_timer = os_container_of(itr, os_timer_t, list);
      itr_timer->ticks_rem = itr_timer->ticks_reload;
      os_timer_add(itr_timer); /* add timer at the proper place at the list */
   }
}

/* \TODO unit test missing */
os_ticks_t os_ticks_diff(
   os_ticks_t ticks_start,
   os_ticks_t ticks_now)
{
   os_ticks_t ret;

   if (ticks_start > ticks_now)
      ret = OS_TICKS_MAX - ticks_start + 1 + ticks_now;
   else
      ret = ticks_now - ticks_start;

   return ret;
}

