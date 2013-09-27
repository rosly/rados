/*
 * This file is a part of RadOs project
 * Copyright (c) 2013, Radoslaw Biernaki <radoslaw.biernacki@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1) Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2) Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3) No personal names or organizations' names associated with the 'RadOs' project
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE RADOS PROJET AND CONTRIBUTORS "AS IS" AND
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

/** Maximal number of unsychronized ticks */
#define OS_TIMER_UNSYNCH_MAX ((os_ticks_t)1024)

/** Prevent from craeting timer with to big timeout, so even if it will fit into
 *  timeout datatype, it could create problems with high OS_TIMER_UNSYNCH_MAX */
#define OS_TIMER_TICKSREM_MAX ((os_ticks_t)(UINT16_MAX - OS_TIMER_UNSYNCH_MAX))

/** current ticks counter, increamented each tick ISR call
 *  used for time keeping */
os_ticks_t os_ticks_cnt = 0;

/** /TODO In future we need some smarter algorithm than list for active timers,
 * list will not scale good so it will be only efective in case of small amout
 * of timers. For that I have following idea: Instead traversing whole list of
 * timers, check only the first which should burnoff and after it burnoff,
 * decrease the remain timers timeout by itimeout value of this first timer
 * To implement that we need additional variable (for instance timer_pending)
 * and timer list need to be keept in sorted order At each os_timer_tick
 * increment only the timer_pending and check only the first timer, if
 * timer_pending < timer->ticks_rem then do nothing In oposite case triger all
 * timers that have the same timeout value (traversing by list up to timer with
 * different ticks_rem). Then re-add timers if neccesary. Even in this case we
 * don't have to synchronize the timer-pending with remain timers ticks_rem. This
 * action is needed only when we add timer to list or readd timer due
 * autoreaload. Since that this operation should be names os_timer_synchronize
 * while timers should be added to queue (and soting should be done there) by
 * os_timer_add */
static list_t timers;
static os_ticks_t timer_tick_unsynch = 0;

static void OS_HOT os_timer_add(os_timer_t *add_timer);
static void OS_HOT os_timer_synch(void);
static void OS_HOT os_timer_triger(void);

/** Module initialization function, can be called only from os_start() */
void OS_COLD os_timers_init(void)
{
   list_init(&timers);
}

/* \note timer_proc_clbck cannot call the os_sched, this will be done at the end
 * of os_tick(), (which calls the os_timer_tick()) */
void os_timer_create(
  os_timer_t* timer,
  timer_proc_t clbck,
  void* param,
  os_ticks_t timeout_ticks,
  os_ticks_t reload_ticks)
{
   arch_criticalstate_t cristate;

   /* timeout must be at least 1 tick in future */
   OS_ASSERT(timeout_ticks > 0);
   /* and cannot be to high, or it will create problems with unsynch ticks */
   OS_ASSERT(timeout_ticks < OS_TIMER_TICKSREM_MAX);

   /* \TODO are we able to prevent double usage of initialized timer ? currently
    * this will probably crash the system. It is better to prevent such mistakes
    * than fixing bugs */

   /* currently I assume that timers may be created from ISR, but I'm not sure
    * for 100% if this will not broke something \TODO check that!*/

   //memset(timer, 0, sizeof(os_timer_t));
   list_init(&(timer->list));
   timer->ticks_rem = timeout_ticks;
   timer->ticks_reload = reload_ticks;
   timer->clbck = clbck;
   timer->param = param;

   /* timer list is iterated from ISR, we need to disable the interrupts */
   arch_critical_enter(cristate);
   /* we need to take the timer_tick_unsynch into account now (it may be != 0)
      it is enough that we add timer_tick_unsynch to ticks_rem, otherwise we
      would need to waste CPU for os_time_synch() called from here */
   timer->ticks_rem += timer_tick_unsynch; /* under critical section since timer_tick_unsynch is also accessed from ISR */
   os_timer_add(timer);
   arch_critical_exit(cristate);
}

/* \note this function is designed in way that allows to call it multiple times
 * until the memory for timer is valid this is safe */
void os_timer_destroy(os_timer_t* timer)
{
   arch_criticalstate_t cristate;

   /* timer list is iterated from ISR, we need to disable the interrupts */
   arch_critical_enter(cristate);
   if( timer->ticks_rem > 0 )
   {
      /* only still active timers need some actions */
      list_unlink(&(timer->list)); /* detach from active timer list */
      timer->ticks_rem = 0; /* marking timer as expired, prevents double destroy */
      timer->ticks_reload = 0; /* clearing the autoreload field, this will allow
                                  for safe destroy of timers from the
                                  timer_callback (timer will not be restarted if
                                  this field is 0) */
   }
   arch_critical_exit(cristate);
}

/* \note this function can be called only from os_tick()
   in other words this function can be called only from critical section */
void OS_HOT os_timer_tick(void)
{
   list_t *head;
   os_timer_t* head_timer;

   /* increment system global ticks count
    * to be able to use this counter properly, user code must call os_ticks_now
    * and os_ticks_diff funtions whcih handle the overflow scenario */
   ++os_ticks_cnt;

   /* handle the timer list */
   head = list_peekfirst(&timers);
   if( NULL == head ) {
      /* if there is no timers in queue we can safely reset unsynch and no timer
       * actions is needed */
      timer_tick_unsynch = 0;
   } else {
      /* if there are some timers on list, first try to avoid timer list
       * traversal (unsynch), then check if traversal is realy needed
       * -it can be that first timer on list was burned of
       * -or we rach some resonable maximal unsunch value and because timeout
       * fields has limited bytes it is better to synchronize anyway
       */
      ++timer_tick_unsynch;
      head_timer = os_container_of(head, os_timer_t, list);
      if( OS_UNLIKELY((timer_tick_unsynch >= head_timer->ticks_rem) ||
                      (timer_tick_unsynch > OS_TIMER_UNSYNCH_MAX)) ) {
         os_timer_synch();
         os_timer_triger();
      }
   }
}

/** Function add the timer to the timer list
 *  The timer list is keept as sorted */
static void OS_HOT os_timer_add(os_timer_t *add_timer)
{
   list_t *itr;
   os_timer_t* itr_timer;

   itr = list_itr_begin(&timers);
   while( false == list_itr_end(&timers, itr) )
   {
      itr_timer = os_container_of(itr, os_timer_t, list);
      if( itr_timer->ticks_rem > add_timer->ticks_rem ) {
         break;
      }
      itr = itr->next;
   }
   list_put_before(itr, &(add_timer->list));
}

/** Function decreases all active timers remain ticks counter by timer_tick_unsynch
 *  In other words we push the fast skipped ticks count, to the timer remain life
 *  time variable
 */
static void OS_HOT os_timer_synch(void)
{
   list_t *itr;
   os_timer_t* itr_timer;

   itr = list_itr_begin(&timers);
   while( false == list_itr_end(&timers, itr) )
   {
      itr_timer = os_container_of(itr, os_timer_t, list);
      itr_timer->ticks_rem -= timer_tick_unsynch;
      itr = itr->next;
   }

   timer_tick_unsynch = 0;
}

/** Function trigers the timeouted timers and reads them if they are autoreload
 */
static void OS_HOT os_timer_triger(void)
{
   list_t *itr;
   list_t *next;
   os_timer_t* itr_timer;
   list_t list_autoreload;

   list_init(&list_autoreload); /* init the temporary list */
   itr = list_itr_begin(&timers); /* start te iteration from begin of the list */
   while( false == list_itr_end(&timers, itr) )
   {
      /* the list will be modified, so we have to get pointer the next element
       * before modification */
      next = itr->next;
      itr_timer = os_container_of(itr, os_timer_t, list);
      if( 0 != itr_timer->ticks_rem ) {
         break; /* current timer does not burnoff, end of trigering loop */
      }
      list_unlink(&(itr_timer->list)); /* in any case (autoreload and singleshot) remove timer from active list */
      itr_timer->clbck(itr_timer->param); /* call the timer callback, from this callback it is allowed to call the os_timer_destroy */
      if( itr_timer->ticks_reload > 0 ) {
         list_append(&list_autoreload, &(itr_timer->list)); /* if timer is autoreload, add the timer to temporary list */
      }
      itr = next;
   }

   /* now re-add all autoreload timers from temporaty list
      we need a temporary list because we keep all timers sorted, and we cannot
      figure out the position of timers which we autoreload until we finish
      processing of previous triger sequence */
   while( NULL != (itr = list_detachfirst(&list_autoreload)) )
   {
      itr_timer = os_container_of(itr, os_timer_t, list);
      itr_timer->ticks_rem = itr_timer->ticks_reload;
      os_timer_add(itr_timer); /* add timer at the proper place */
   }
}

/* \TODO unit test missing */
os_ticks_t os_ticks_diff(os_ticks_t* restrict ticks_start)
{
  arch_criticalstate_t cristate;
  os_ticks_t ret;

  arch_critical_enter(cristate);
  if(*ticks_start > os_ticks_cnt)
    {
      ret = ARCH_TICKS_MAX - *ticks_start + 1 + os_ticks_cnt;
    }
  else
    {
      ret = os_ticks_cnt - *ticks_start;
    }
  *ticks_start = os_ticks_cnt;
  arch_critical_exit(cristate);

  return ret;
}

/* \TODO unit test missing */
void os_timeout_start(os_timeout_t* restrict timeout, os_ticks_t ticks)
{
  os_ticks_now(&(timeout->ticks_start));
  timeout->ticks_rem = ticks;
}

/* \TODO unit test missing */
int os_timeout_check(os_timeout_t* restrict timeout)
{
  os_ticks_t ticks_tmp;

  ticks_tmp = os_ticks_diff(&(timeout->ticks_start));
  if(ticks_tmp >= timeout->ticks_rem)
    {
      timeout->ticks_rem = 0;
      return 1;
    }
  else
    {
      timeout->ticks_rem -= ticks_tmp;
      return 0;
    }
}

