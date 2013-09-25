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

/**
 * Implementation of waitqueue, simmilar to Linux kernel concept
 *
 * The idea behind waitqueue is to allow programer to synchronize os task, with
 * asynchronius events while avoiding pooling. In this case we usualy need to
 * chek kind of condition and if it is not meet we need to sleep the task. The
 * problem is that betwen condition check and some sort of sleep call, the
 * asynchronius event may hit, and receiver code will not have oportunity to be
 * notified about it. The result will be missup of this event, since we will
 * need just another one to wake up the task from sleep call.
 *
 * In wait queue following sniplet of code can be used on receiver side:
 * 0: os_atomic_t conditional_test;
 *
 * 1: while(1) {
 * 2:   os_waitqueue_prepare(&waitqueue, timeout);
 * 3:   if(conditional_test) {
 * 4:      os_waitqueue_finish();
 * 5:      break;
 * 6:   }
 * 7:   ret = os_waitqueue_wait();
 * 8: }
 *
 * On notifier side following sniplet can be used:
 * 10: conditional_test = true;
 * 11: os_waitqueue_wakeup(&waitqueue);
 *
 * The main goal is to wake the receiver while avoiding the race ondition betwen
 * conditional_test and os_waitqueue_wait() call. Such problem also can be
 * solved by semaphore and not using the condition test at all, but waitqueues
 * has several advantages.
 * - os_waitqueue_prepare is very fast while whole construction is focused on
 *   conditional_test == true case. If this is true then waitqueue is much
 *   faster then semaphore since we dont need to disable interrupts for
 *   os_waitqueue_prepare (if we dont create timeout for that call).
 * - if we use single notifier and multiple receivers, we dont have to know the
 *   number of the receivers to wake all of them (for that OS_WAITQUEUE_ALL can
 *   be used as nbr parameter of os_waitqueue_wakeup())
 * - if multiple events will came before receiver task will ask for them,
 *   semaphore value will be bringd-up multiple times. If event is type of "all
 *   or nothing", this semaphore will cause receiver to spin several times in
 *   bussy loop until it finaly seetle for sleep. The reason for that is
 *   receiver need to consume all signalization of semaphore.
 *
 * This primitive works by utilizing the fact that task durring task switch,
 * scheduler schecks the task_current->wait_queue pointer. If that pointer is
 * set, it does not push the task_current into ready_queue (where it will wait
 * for futire schedule()) but instead, it put it into wait_queue->task_queue (a
 * task queue asosiated with wait_queue).
 *
 * The proff of concept is following. We must to consider 3 places where
 * preemption may kick-in and ISR or some other task may issue lines 10 and 11
 * on notifier side.
 * 1) betwen line 2 and 3
 * 2) betwen line 6 and 7
 *
 * One additional spot is betwen lines 3 and 4 but there task already knows that
 * it was signalized (because condition was meet) and it just need to clean-up
 * with os_waitqueue_finish() call
 *
 * If receiver is cooperating with ISR, both lines 10 and 11 are issued
 * simoutaniusly form user task perspective.i Additionaly keep in mind that
 * durring ISR which preempt mentioned 3 points, task_current is set to task
 * which we consider as wounerable. If we look at os_waitqueue_wakeup() we will
 * see that there is a special case for this.
 * 1) task_current->wait_queue is atomicaly set before ISR, so in ISR line 10
 *    and 11 are done atomicaly to user code. If we look at
 *    os_waitqueue_wakeup_sync it will show that there is special case that
 *    detects that we are in ISR and we preempted the task which waits on the
 *    same condition which we try to signalize. In this case we only set
 *    task_current->wait_queue = NULL
 * 2) if we look at os_waitqueue_wait() we will see that it veiry if
 *    task_current->wait_queue is still set, if not it exits righ away. It will be
 *    NULL since we set it to NULL in line 11 of ISR (inside os_waitqueue_wakeup())
 *
 * If receiver is cooperating with some other task, then in
 * os_waitqueue_wakeup() the task_current is set to different value then task
 * which wait for condition. In other words now it is different story comparing
 * to ISR. What is more important, line 10 and 11 may not be issued atomicaly.
 * 1) if we will have task preemption at this point it is enough that notifier
 *    execute line 10, since receiver will detect that in line 3 (and will skipp
 *    the sleep). If notifier will execute also line 11 it will see the
 *    receiver task in wait_queue->task_queue since scheduler puts it there while
 *    preempting (look at os_task_makeready() and NULL != task->wait_queue
 *    condition in that function). This means that receiver task will be woken
 *    up by os_waitqueue_wakeup() and it will continue in line 3, while
 *    condition is already set.
 * 2) receiver already passes the condition, so executing line 10 in notifier
 *    will not have any effect. But here also we need to pass scheduler in case of
 *    preemption. It means that os_task_makeready() will again put the receiver
 *    into wait_queue->task_list before line 10 and 11 in notifier and this will
 *    mean that it is almost the same as in 1). The only difference is that after
 *    receiver is woken up, it will return to line 8 instead of 3, so we must to
 *    make a loop and check condition again.
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

      /* we need to destroy the timer here, because otherwise we will be
       * vulnerable for race conditions from timer callbacks (ISR) */
      os_timeout_destroy(task); 

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
      os_timeout_create(&(waitobj->timer), os_waitqueue_timerclbck, timeout_ticks);
      arch_critical_exit(cristate);
   }
}

void os_waitqueue_finish(void)
{
   OS_ASSERT(0 == isr_nesting); /* this function may be called only form user code */
   /* we check that task_current was assigned to some queue this helps to force
    * user to write corect and efficient code since calling
    * os_waitqueue_finish() after os_waitqueue_wait() is a waste of CPU cycles
    */
   OS_ASSERT(NULL != task_current->wait_queue);

   /* just peek if we have timer for timeout assigned */
   if(NULL != os_atomicptr_read(task_current->timer)) {
      arch_criticalstate_t cristate;

      arch_critical_enter(cristate);

      /* remove wait_queue assosiation from task */
      task_current->wait_queue = NULL;

      /* destroy timeout assosiated with task good to note is that we check
       * task_curent->timer second time inside os_timeout_destroy() since
       * previus checking was done outside critical section (things may change
       * from that moment) */
      os_timeout_destroy(task_current); 

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
        break;
      }

      /* now block and change switch the context */
      os_block_andswitch(&(task_current->wait_queue->task_queue), OS_TASKBLOCK_WAITQUEUE);

      /* destroy timeout assosiated with task if it was created */
      os_timeout_destroy(task_current); 

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
   } else {
       while((OS_WAITQUEUE_ALL == nbr) || ((nbr--) > 0)) {
         /* chose most prioritized task from wait_queue->task_queue and for
          * task with same priority chose in FIFO manner */
         task = os_task_dequeue(&(queue->task_queue));
         /* check if there was task which waits on queue */
         if( NULL == task ) {
            break; /* there will be no more task to wake, stop spinning */
         }

         /* we need to destroy the timer here, because otherwise we will be
          * vulnerable for race conditions from timer callbacks (ISR) */
         os_timeout_destroy(task); 

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
   }
   arch_critical_exit(cristate);
}

/* --- private functions --- */

static void os_waitqueue_timerclbck(void* param)
{
   os_task_t *task = (os_task_t*)param;

   /* unlike to semaphores, timeout for waitqueues are created when task is
    * still running in os_waitqueue_prepare. It may happened that this timer
    * will burn off while task is still runinng and not yet reached
    * os_waitqueue_wait() function.  It will mean that is will be not linked at
    * any task_list so we should not try to unlik it. */
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
    * not call this function again, also we can safely call os_timer_destroy
    * multiple times for such destroyed timer unles memory for timer structure
    * will not be invalidated*/
}

