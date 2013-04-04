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

static list_t timers;
static uint_fast16_t timer_tick_unsynch = 0;
/* /todo In future we need some smarter algorithm than list for active timers, list will not scale good so it will be only efective in case of small amout of timers
   for that I have  following idea:
   Instead traversing all list of timers, check only the first which should burnoff and after it burnoff decrease the remain timers timeout by value of this first timer
   TO implement that we need additional variable (for instance timer_pending) and timer list need to be keept in sorted order
   At each os_timer_tick increment only the timer_pending and check only the first timer, if timer_pending < timer->ticks_rem then do nothing
   In oposite case triger all timers that have the same timeout value (traversing by list up to timer with different ticks_rem). Then readd timers in neccesary.
   Even in this case we dont have to synchronize the timerpengind with remain timers ticks_rem. This action is needed only when we add timer to list or readd timer due autoreaload.
   Since that this operation should be names os_timer_synchronize while timers should be added to queue (and soting should be done there) by os_timer_add */

static void OS_HOT os_timer_add(os_timer_t *add_timer);
static void OS_HOT os_timer_synch(void);
static void OS_HOT os_timer_triger(void);

/* this function can be called only from os_start() */
void os_timers_init(void)
{
   list_init(&timers);
}

/* /note timer_proc_clbck cannot cal the os_sched, this will be done at the end of os_tick(), (which calls the os_timer_tick()) */
void os_timer_create(os_timer_t* timer, timer_proc_t clbck, void* param, uint_fast16_t timeout_ticks, uint_fast16_t reload_ticks)
{
   arch_criticalstate_t cristate;

   OS_ASSERT(timeout_ticks > 0); /* tiemout must be at least 1 tick in future */
   /* currently I assume that timers may be created from ISR, but I'm not sure for 100% */

   //memset(timer, 0, sizeof(os_timer_t));
   list_init(&(timer->list));
   timer->ticks_rem = timeout_ticks;
   timer->ticks_reload = reload_ticks;
   timer->clbck = clbck;
   timer->param = param;

   arch_critical_enter(cristate); /* timer list is iterated from ISR, we need to disable the interrupts */
   os_timer_add(timer);
   arch_critical_exit(cristate);
}

/* this function is designed in way that allows to call it multiple times
   until the memory for timer is valid this is safe */
void os_timer_destroy(os_timer_t* timer)
{
   arch_criticalstate_t cristate;

   arch_critical_enter(cristate); /* timer list is iterated from ISR, we need to disable the interrupts */
   if( timer->ticks_rem > 0 )
   {
      /* only still active timers need some actions */
      list_unlink(&(timer->list)); /* detach from active timer list */
      timer->ticks_rem = 0; /* marking timer as expired, also prevents from destroying two times */
      timer->ticks_reload = 0; /* also clearing the autoreload field, this will allow for safe destroy of timers from the timer_callback (timer will not be restarted if this field is 0)*/
   }
   arch_critical_exit(cristate);
}

/* this function can be called only from os_tick()
   in other words this function can be called only from critical section */
void OS_HOT os_timer_tick(void)
{
   list_t *head;
   os_timer_t* head_timer;

   ++timer_tick_unsynch;
   head = list_peekfirst(&timers);
   if( NULL != head ) {
      head_timer = os_container_of(head, os_timer_t, list);
      if( OS_UNLIKELY(timer_tick_unsynch >= head_timer->ticks_rem) ) {
         os_timer_synch();
         os_timer_triger();
      }
   }
}

/* function add the timer to the timer list
   the list is keept as sorted */
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

/**
   Function pushes the timer_tick_unsynch to each timer->ticks_rem on list
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

/**
   Function trigers the timeouted timers and reads them if they are autoreload
   */
static void OS_HOT os_timer_triger(void)
{
   list_t *itr;
   list_t *next;
   os_timer_t* itr_timer;
   list_t list_autoreload;

   list_init(&list_autoreload); /* init the temporary list */
   itr = list_itr_begin(&timers);
   while( false == list_itr_end(&timers, itr) ) /* start te iteration from begin of the list */
   {
      next = itr->next; /* the list will be modified, so we have to refer the next right here before modification */
      itr_timer = os_container_of(itr, os_timer_t, list);
      if( 0 != itr_timer->ticks_rem ) {
         break; /* first timer which does not burnoff, end of trigering loop */
      }
      list_unlink(&(itr_timer->list)); /* in any case (autoreload and singleshot) remove timer from active list */
      itr_timer->clbck(itr_timer->param); /* call the timer callback, from this callback it is allowed to call the os_timer_destroy */
      if( itr_timer->ticks_reload > 0 ) {
         list_append(&list_autoreload, &(itr_timer->list)); /* add the timer to temporary list */
      }
      itr = next;
   }

   /* now readd all autoreload timers from temporaty list
      this is necessary to do this in this sequence becouse we cannot read the timers while we traverse through the list
      if we do so, we could reach the same timer twice (create endless loop and other side effects) */
   while( NULL != (itr = list_detachfirst(&list_autoreload)) )
   {
      itr_timer = os_container_of(itr, os_timer_t, list);
      itr_timer->ticks_rem = itr_timer->ticks_reload;
      os_timer_add(itr_timer); /* add timer at the proper place */
   }
}

