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

      /* destroy the timer if it was assosiated with task which we plan to wake
       * up */
      os_blocktimer_destroy(task);

      task->wait_queue = NULL;
      task->block_code = OS_DESTROYED;
      os_task_makeready(task);
   }

   /* finaly we destroy all wait queue data, this should arrise problems if this
    * queue was used also in interupts (feel warned) */
   memset(queue, 0, sizeof(os_waitqueue_t));

   /* schedule to make context switch in case os_waitqueue_destroy was called by
    * some low priority task */
   os_schedule(1);

   arch_critical_exit(cristate);
}

void os_waitqueue_prepare(
   os_waitqueue_t *queue,
   os_waitobj_t *waitobj,
   os_ticks_t timeout_ticks)
{
   OS_ASSERT(0 == isr_nesting); /* this function may be called only form user code */
   OS_ASSERT(task_current->prio_current > 0); /* idle task cannot call blocking
                                                 functions (will crash OS) */
   /* check if task is not already subscribed to other wait_queue
      currently we do not support waiting on multiple wait queues */
   OS_ASSERT(NULL == task_current->wait_queue);
   /* in case of timeout use, we need to have waitobj */
   OS_ASSERT((OS_TIMEOUT_INFINITE == timeout_ticks) || (NULL != waitobj));

   /* assosiate task with wait_queue, this will change the bechaviour inside
    * os_task_makeready() and instead of ready_queue taks will be added to
    * task_queue of assosiated wait_queue */
   os_atomicptr_write(task_current->wait_queue, queue);
   task_current->block_code = OS_OK;

   /* create timer which will count time starting from this moment. By doing
    * this time keeping is done for both, the condition checking code and sleep
    * time inside os_waitqueue_wait() */
   if( OS_TIMEOUT_INFINITE != timeout_ticks ) {
      arch_criticalstate_t cristate;

      arch_critical_enter(cristate);
      os_blocktimer_create(&(waitobj->timer), os_waitqueue_timerclbck, timeout_ticks);
      arch_critical_exit(cristate);
   }
}

void os_waitqueue_finish(void)
{
   OS_ASSERT(0 == isr_nesting); /* this function may be called only form user code */

   /* just peek if we have timer for timeout assigned */
   if(NULL != os_atomicptr_read(task_current->timer)) {
      arch_criticalstate_t cristate;

      arch_critical_enter(cristate);

      /* remove wait_queue assosiation from task */
      task_current->wait_queue = NULL;

      /* destroy timeout assosiated with task good to note is that we check
       * task_curent->timer second time inside os_blocktimer_destroy() since
       * previus checking was done outside critical section (things may change
       * from that moment) */
      os_blocktimer_destroy(task_current);

      arch_critical_exit(cristate);

   } else {
      /* remove wait_queue assosiation from task */
      os_atomicptr_write(task_current->wait_queue, NULL);
   }
}

os_retcode_t OS_WARN_UNUSEDRET os_waitqueue_wait(void)
{
   os_retcode_t ret;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* this function may be called only form user code */
   /* idle task cannot call blocking functions (will crash OS) */
   OS_ASSERT(task_current->prio_current > 0);

   /* we need to disable the interrupts since wait_queue may be signalized from
    * ISR (we need to add task to wait_queue->task_queue in atomic maner) */
   arch_critical_enter(cristate);
   do
   {
      /* check if we really was prepared for waiting on wait_queue
         if not this may mean that we where woken up in mean time */
      if(NULL == task_current->wait_queue) {
        /* cleanup, destroy timeout assosiated with task if it was created */
        os_blocktimer_destroy(task_current);
        break;
      }

      /* now block and change switch the context */
      os_block_andswitch(&(task_current->wait_queue->task_queue), OS_TASKBLOCK_WAITQUEUE);

      /* cleanup, destroy timeout assosiated with task if it was created */
      os_blocktimer_destroy(task_current);

   }while(0);

   /* the block code is either OS_OK (set in os_waitqueue_wakeup_sync()) or
    * OS_TIMEOUT (set in os_waitqueue_timerclbck()) or OS_DESTROYED (set in
    * os_waitqueue_destroy()) */
   ret = task_current->block_code;

   arch_critical_exit(cristate);

   return ret;
}

/* this function can be called from ISR (one of the basic functionality of wait_queue) */
void os_waitqueue_wakeup_sync(
   os_waitqueue_t *queue,
   uint_fast8_t nbr,
   bool sync)
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

      /* we need to destroy the timer here, because otherwise it may fire right
       * after we leave the critical section */
      os_blocktimer_destroy(task_current);

      /* since we teoreticaly prevented from sleep one task, now we have one
       * task less to wake up */
      if(OS_WAITQUEUE_ALL != nbr) {
        --nbr;
      }

      /* \TODO \FIXME consider that task_current may be less prioritazed than
       * tasks in wait_queue->task_queue, so preventing it from sleep is not
       * fait in scope of scheduling. There may be some more prioritized task
       * which should be woken up first, while this task maybe should go to
       * sleep for instance in nbr = 1
       * One additional note: look at os_schedule() at bottom of this function,
       * this will not help either, because if nbr was 1, then this bottom
       * section will not even fire
       * we should put task_current into wait_queue->task_queue and then pick
       * most prioritized one */
   }

   while((OS_WAITQUEUE_ALL == nbr) || ((nbr--) > 0)) {
     /* chose most prioritized task from wait_queue->task_queue and for
      * task with same priority chose in FIFO manner */
     task = os_task_dequeue(&(queue->task_queue));
     /* check if there was task which waits on queue */
     if( NULL == task ) {
        break; /* there will be no more task to wake, stop spinning */
     }

     /* we need to destroy the timer here, because otherwise it may fire right
      * after we leave the critical section */
     os_blocktimer_destroy(task);

     task->wait_queue = NULL; /* disassociate task from wait_queue */
     task->block_code = OS_OK; /* set the block code to NORMAL WAKEUP */
     os_task_makeready(task);

     /* do not call schedule() if we will do it in some other os function
      * call. This is marked by sync parameter flag */
     if(!sync) {
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

   /* remove the wait_queue assosiation since we timeouted
    * only then we can call os_task_makeready(), check internals */
   task->wait_queue = NULL;
   task->block_code = OS_TIMEOUT;

   /* unlike to semaphores, timeout for waitqueues are created when task is
    * still running in os_waitqueue_prepare. It may happened that this timer
    * will burn off while task is still runinng and not yet reached
    * os_waitqueue_wait() function.
    * It means that task for this timer may be in any state, WAIT, READY or even
    * RUNNING. Therefore we do not check for
    * OS_SELFCHECK_ASSERT(TASKSTATE_WAIT == task->state);
    *
    * It will also mean that it can be linked at some task_list only if it state
    * is WAIT */
   if(TASKSTATE_WAIT == task->state) {
     os_task_unlink(task);
     os_task_makeready(task);
   }
   /* we do not call the os_sched here, because this will be done at the
    * os_tick() (which calls the os_timer_tick which call this function) */
   /* we do not need to destroy timer here, since it will be destroyed by
    * cleanup code inside the woken up task */
   /* timer is not auto reload so we dont have to wory about it here (it will
    * not call this function again, also we can safely call os_timer_destroy
    * multiple times for such destroyed timer unles memory for timer structure
    * will not be invalidated*/
}

