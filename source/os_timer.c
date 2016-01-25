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

/** Maximal number of unsynchronized ticks
 * This define is used to define the maximal number of quickly handled ticks.
 * For more info look at timer_tick_unsynch */
#define OS_TIMER_UNSYNCH_MAX ((os_ticks_t)1024)

/** Prevent from creating timer with to big timeout, even if timeout will fit
 * into timeout datatype, it could create problems with high
 * OS_TIMER_UNSYNCH_MAX */
#define OS_TIMER_TICKSREM_MAX ((os_ticks_t)(UINT16_MAX - OS_TIMER_UNSYNCH_MAX))

#define OS_TIMER_MAGIC1 ((uint_fast16_t)0xAABB)
#define OS_TIMER_MAGIC2 ((uint_fast16_t)0xCCDD)

/** Global monotonic counter of system ticks */
os_ticks_t ticks_cnt = 0;

/** Global list of all timers. Timers are sorted by time which remain until
 *  burnoff */
static slist_t timer_list;

/** Number of "quickly" handled ticks.
 *
 * All timers are kept on sorted list and until first timer does not burn off
 * it means that remain wont either. Because of this we do not iterate through
 * timer list at each tick, but accumulate ticks in timer_tick_unsynch variable.
 * When this variable will be equal to first timer burn off time, than we
 * iterate through the timer list. Also due to limited maximal value which can
 * be stored in os_ticks_t, we iterate from time to time just to keep track of
 * timeout values. This spontaneous iterations are controlled by
 * OS_TIMER_UNSYNCH_MAX */
static os_ticks_t timer_tick_unsynch = 0;

/** Function add the timer to the timer list. Function keeps the timer list
 * sorted by remaining burn off time of the timers. */
static void timer_add(os_timer_t *add_timer)
{
   slist_t* OS_RESTRICT itr;
   slist_t* OS_RESTRICT itr_prev = &timer_list;
   os_timer_t *itr_timer;

   itr = slist_itr_begin(&timer_list);
   while (!slist_itr_end(itr)) {
      itr_timer = os_container_of(itr, os_timer_t, list);
      if (itr_timer->ticks_rem > add_timer->ticks_rem)
         break;
      itr_prev = itr;
      itr = itr->next;
   }

   slist_put_after(itr_prev, &(add_timer->list));
}

/** Function triggers the timers which had timeouted (timer->ticks_rem == 0) and
 * rearm them in case they are auto reloaded
 * NO_INLINE prevents from inlining which in turn forcess calling funtion to
 * push the registers to stack (which is bad for frequently called funtion such
 * as os_tick) */
static void OS_NOINLINE timer_trigger(void)
{
   slist_t *itr;
   os_timer_t *itr_timer;
   slist_t list_autoreload;

   /* for auto reloaded timers use temporary list */
   slist_init(&list_autoreload);

   /* iterate over timer list */
   itr = slist_itr_begin(&timer_list);
   while (!slist_itr_end(itr)) {

      itr_timer = os_container_of(itr, os_timer_t, list);

      /* the list will be modifiedi, calculate pointer the next element. */
      itr = itr->next;

      if ((itr_timer->ticks_rem -= timer_tick_unsynch) > 0) {
         /* this timer does not timedout (this means that following will not
          * either). But we need to continue iteration to update the ticks_rem
          * of all timer on the list */
         continue;
      }

      /* current timer has timeouted. We have to remove it from timer list. But
       * since we removing timers from begining of the list if they timeout and
       * we continue doing that until we found timer that does not timeout, it
       * means that we efectivly remove first timer at each roll of the loop.
       * /note following is more efficient than slist_detachfirst() */
      slist_unlink_next(&timer_list);

      /* call the timer callback. Keep in mind that from this callback it is
       * allowed to call the os_timer_destroy() */
      itr_timer->clbck(itr_timer->param);

      if (itr_timer->ticks_reload > 0) {
         /* seems that timer callback does not destroyed the timer and it is
          * (still) marked as auto-reload. We cannot imidiatelly add this timer
          * back on to timer list. We need to use temporary list */
         slist_append(&list_autoreload, &(itr_timer->list));
      }
   }

   timer_tick_unsynch = 0;

   /* Now re-add all auto reload timers from temporary list.
    * We need a temporary list because we keep all timers sorted, and we cannot
    * figure out the correct position of reloded timer until we finish
    * processing of timer list */
   while ((itr = slist_detachfirst(&list_autoreload))) {
      itr_timer = os_container_of(itr, os_timer_t, list);
      itr_timer->ticks_rem = itr_timer->ticks_reload;
      timer_add(itr_timer); /* add timer at the proper place at the list */
   }
}

/** Module initialization function, can be called only from os_start() */
void OS_COLD os_timers_init(void)
{
   slist_init(&timer_list);
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
   /* and cannot be to high, or it will create problems with unsynch ticks */
   OS_ASSERT(timeout_ticks < OS_TIMER_TICKSREM_MAX);
   /* prevent from double usage of already initialized timer */
   OS_ASSERT(timer->magic != OS_TIMER_MAGIC1);

   /* currently I assume that timers may be created from ISR, but I'm not sure
    * for 100% if this will not broke something \TODO check that!*/

   //memset(timer, 0, sizeof(os_timer_t));
   slist_init(&(timer->list));
   timer->ticks_rem = timeout_ticks;
   timer->ticks_reload = reload_ticks;
   timer->clbck = clbck;
   timer->param = param;
#ifdef OS_CONFIG_APICHECK
   timer->magic = OS_TIMER_MAGIC1;
#endif

   /* timer list and timer_tick_unsynch are accessed from ISR, we need to
    * disable the interrupts */
   arch_critical_enter(cristate);
   /* we need to take the timer_tick_unsynch into account now (it may be != 0)
    * In case there are some unsynced ticks, it is enough that we increase the
    * burn off time for this timer. Once synch will be performed in some not
    * distant future, the added timeout will be compensated */
   timer->ticks_rem += timer_tick_unsynch;
   timer_add(timer);
   arch_critical_exit(cristate);
}

void os_timer_destroy(os_timer_t *timer)
{
   arch_criticalstate_t cristate;

   /* prevent double usage of initialized timer */
   OS_ASSERT((timer->magic == OS_TIMER_MAGIC1) ||
             (timer->magic == OS_TIMER_MAGIC2));

   /* timer list is iterated from ISR, we need to disable the interrupts */
   arch_critical_enter(cristate);

   /* only still active timers need some actions */
   if (timer->ticks_rem > 0) {
      /* detach from active timer list */
      //slist_unlink(&(timer->list)); BUG TODO
      /* marking timer as expired, prevents double destroy */
      timer->ticks_rem = 0;
      /* clearing the auto-reload field, this will allow for safe destroy of
       * timers from the timer_callback (timer will not be restarted when this
       * field is 0) */
      timer->ticks_reload = 0;
   }

#ifdef OS_CONFIG_APICHECK
   /* obstruct magic, mark that this timer was successfully destroyed
    * \note this function is designed in way, that ss long as the memory for
    * timer is valid it allows for multiple destroy operations on the same timer
    * */
   timer->magic = OS_TIMER_MAGIC2;
#endif

   /* in case list become empty, reset the unsynch */
   if (slist_is_empty(&timer_list)) {
      timer_tick_unsynch = 0;
   }

   arch_critical_exit(cristate);
}

void OS_HOT os_tick(void)
{
   os_timer_t *head_timer;

   OS_ASSERT(isr_nesting > 0);   /* this function may be called only form ISR */

   /* Increment system global monotonic ticks counter.
    * Overflow scenario for this counter are handled by usage os_ticks_now()
    * and os_ticks_diff() */
   ++ticks_cnt;

   if (!slist_is_empty(&timer_list)) {

      ++timer_tick_unsynch;

      /* Perform iteration over timer list only in following cases:
       * - timeout of first timer (only than following could possibly timeout
       *   too)
       * - since timeout fields (os_ticks_t) is limited in size (16 bits) we
       *   need to synch from time to time
       * */
      head_timer = os_container_of(
         slist_peekfirst(&timer_list), os_timer_t, list);
      if (OS_UNLIKELY((timer_tick_unsynch >= head_timer->ticks_rem) ||
                      (timer_tick_unsynch > OS_TIMER_UNSYNCH_MAX))) {
         timer_trigger();
      }
   }

   /* switch to other READY task which has the same or greater priority (0 as
    * param of os_schedule() means just that) */
   os_schedule(0);
}

os_ticks_t os_ticks_now(void)
{
   os_ticks_t ticks;

   arch_ticks_atomiccpy(&ticks, &ticks_cnt);
   return ticks;
}

/* \TODO unit test missing */
os_ticks_t os_ticks_diff(
   os_ticks_t ticks_start,
   os_ticks_t ticks_end)
{
   os_ticks_t ret;

   if (ticks_start > ticks_end)
      ret = OS_TICKS_MAX - ticks_start + 1 + ticks_end;
   else
      ret = ticks_end - ticks_start;

   return ret;
}

