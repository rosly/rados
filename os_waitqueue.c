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

/* private function forward declarations */
static void os_waitqueue_timerclbck(void* param);

void os_waitqueue_create(os_waitqueue_t* queue)
{
   memset(queue, 0, sizeof(os_waitqueue_t));
   os_taskqueue_init(&(queue->task_queue));
}

void os_waitqueue_destroy(os_waitqueue_t* queue)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   arch_critical_enter(cristate);

   /* wake up all task which waits on sem->task_queue */
   while( NULL != (task = os_task_dequeue(&(queue->task_queue))) ) {
      task->block_code = OS_DESTROYED;
      os_task_makeready(task);
   }

   /* finaly we destroy all wait queue data, this should arrise problems if this
    * queue was used also in interupts (feel warned) */
   memset(queue, 0, sizeof(os_waitqueue_t));

   arch_critical_exit(cristate);
}

void os_waitqueue_prepare(os_waitqueue_t* queue, uint_fast16_t timeout_ticks)
{
   os_timer_t timer;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* this function may be called only form user code */
   OS_ASSERT(task_current->prio_current > 0); /* idle task cannot call blocking
                                                 functions (will crash OS) */
   /* check if task is not already subscribed to other wait_queue
      currently we do not support waiting on multiple wait queues */
   OS_ASSERT(NULL == task_current->wait_queue);

   /* we need to disable the interrupts since wait_queue may be signalized from
    * ISR (we need to add task to wait_queue->task_queue in atomic maner) */
   arch_critical_enter(cristate);

   /* assosiate task with wait_queue, this will change the bechaviour inside
    * os_task_makeready() and instead of ready_queue taks will be added to
    * task_queue of assosiated wait_queue */
   task_current->wait_queue = queue;

   /* create timer which will count from this moment this will create time
    * condition from preparation step while allowing multiple spins for
    * assosiated wait condition */
   if( OS_TIMEOUT_INFINITE != timeout_ticks ) {
      os_timer_create(&timer, os_waitqueue_timerclbck, task_current, timeout_ticks, 0);
      task_current->timer = &timer;
   }

   /* in contrast to semaphores, wait_queues can timeout while task is
    * running (and checking the condition assosiated with wait_queue).
    * Therefore we must to set the return code before-hand to handle timeout
    * case properly */
   task_current->block_code = OS_OK;

   arch_critical_exit(cristate);
}

os_retcode_t OS_WARN_UNUSEDRET os_waitqueue_wait(void)
{
   os_retcode_t ret;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* this function may be called only form user code */
   OS_ASSERT(task_current->prio_current > 0); /* idle task cannot call blocking
                                                 functions (will crash OS) */

   /* we need to disable the interrupts since wait_queue may be signalized from
    * ISR (we need to add task to wait_queue->task_queue in atomic maner) */
   arch_critical_enter(cristate);
   do
   {
      /* check if we really was prepared for waiting on wait_queue
         if not this may mean that we where woken up in mean time */
      if(NULL == task_current->wait_queue) {
        break;
      }

      /* now block and change switch the context */
      os_block_andswitch(&(task_current->wait_queue->task_queue), OS_TASKBLOCK_WAITQUEUE);

      if( NULL != task_current->timer ) {
         /* seems that timer didn't exipre up to now, we need to clean it */
         os_timer_destroy(task_current->timer);
         task_current->timer = NULL;
      }

   }while(0);

   /* the block_code was set in os_waitqueue_destroy, timer callback or in
    * os_waitqueue_wakeup, in contrast to semaphores it may be even set while
    * task is in RUNNING state and it is checking the condition assosiated with
    * wait_queue */
   ret = task_current->block_code;

   arch_critical_exit(cristate);

   return ret;
}

/* this function can be called from ISR (one of the basic functionality of wait_queue) */
void os_waitqueue_wakeup(os_waitqueue_t *queue, uint_fast8_t nbr)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   /* this function may be called both from ISR and user code, but if called
    * from user code, task which calls this function cannot wait on the same
    * queue as passed in parameter (in other words it cannot wakeup itself) */
   OS_ASSERT((isr_nesting > 0) || (task_current->wait_queue != queue));

   arch_critical_enter(cristate);

   /* tasks which we need to consider during wakeup are placed in
    * wait_queue->task_queue. task_current will be missing in that task_queue
    * but sill we need to consider it if we are calling this function from
    * ISR (in this case we execute ISR code but still some task was
    * interrupted which may be asosiated with wait_queue).  It is in scope of
    * consideration if its task_current->wait_queue is the same queue as from
    * function parameter It will mean that task_current is right now spinning
    * and checking the condition assosiated with this wait_queue while ISR
    * was called. */
   if((isr_nesting > 0) && (task_current->wait_queue == queue)) {
      /* this means that we entered ISR while task_current associated with
       * this queue was spinning for condition.  It is worth to wakeup this
       * task instead in fisrt place (instead of any other) because this will
       * save CPU time, which could be wasted for unnecessary context
       * switching Since task_current is not in task_queue of wait_queue, all
       * what we need to do is disassociate the task from wait queue */
      task_current->wait_queue = NULL;
   } else {
       while((OS_WAITQUEUE_ALL == nbr) || ((nbr--) > 0)) {
         /* chose most prioritized task from wait_queue->task_queue and for
          * task with same priority chose in FIFO manner */
         task = os_task_dequeue(&(queue->task_queue));
         /* check if there was task which waits on queue */
         if( NULL == task ) {
            break; /* there will be no more task to wake, stop spinning */
         }

         /* check if task waits for wait_queue with timeout */
         if( NULL != task->timer ) {
            /* we need to destroy the timer here, because otherwise we will
             * be vulnerable for race conditions from timer callbacks (in
             * here callback are blocked because of critical section, but we
             * will possibly jump out of it while we will switch the tasks
             * during os_shedule) */
            os_timer_destroy(task->timer);
            task->timer = NULL;
         }

         task->wait_queue = NULL; /* disassociate task from wait_queue */
         task->block_code = OS_OK; /* set the block code to NORMAL WAKEUP */
         os_task_makeready(task);
         /* switch to more prioritized READY task, if there is such (1 param
          * in os_schedule means switch to other READY task which has greater
          * priority) */
         os_schedule(1);
       }
   }
   arch_critical_exit(cristate);
}

/* --- private functions --- */

static void os_waitqueue_timerclbck(void* param)
{
   os_task_t *task = (os_task_t*)param;

   /* we know the task since timer was assigned only to one task, by timer
    * param. But we need to make sure that this task is not running right now,
    * because it will mean that it is not in ready_queue, but instead it is in
    * TASKSTATE_RUNNING */
   if(TASKSTATE_RUNNING != task->state) {
     os_task_unlink(task);
   }
   /* remove the wait_queue assosiation since we timeouted */
   task->wait_queue = NULL;
   task->block_code = OS_TIMEOUT;
   os_task_makeready(task);
   /* we do not call the os_sched here, because this will be done at the
    * os_tick() (which calls the os_timer_tick which call this function) */
   /* timer is not auto reload so we dont have to wory about it here (it will
    * not call this function again, also we can safely call os_timer_destroy in
    * any time for timer assosiated with this task) */
}

