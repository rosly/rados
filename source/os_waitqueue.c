/*
 * This file is a part of RadOs project
 * Copyright (c) 2013, Radoslaw Biernacki <radoslaw.biernacki@gmail.com>
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

#ifdef OS_CONFIG_WAITQUEUE

/**
 * Technical description of wait_queue mechanics.
 *
 * Following template of code is used on receiver side:
 * 0: uint32_t test_condition; does not have to be atomic type
 *
 * 1: while(1) {
 * 2:   os_waitqueue_prepare(&waitqueue, timeout);
 * 3:   if (test_condition) {
 * 4:      os_waitqueue_finish();
 * 5:      break;
 * 6:   }
 * 7:   ret = os_waitqueue_wait();
 * 8: }
 *
 * Following template is used on notifier side:
 * 10: conditional_test = 1;
 * 11: os_waitqueue_wakeup(&waitqueue);
 *
 * The main goal of wait_queue implementation is to wake the receiver while
 * mitigating the race condition between conditional_test and
 * os_waitqueue_wait() call (between line 3 and 7). If such race condition
 * arise it would mean that there was a preemption and wait_queue was notified
 * (either from other user task or from interrupt).
 * Wait_queue works by utilizing the fact that during task switch,
 * scheduler checks the value of waitqueue_current pointer. If that
 * pointer is set, it does not push the task_current into ready_queue (where it
 * will wait for future schedule()) but instead, it put it into
 * wait_queue->task_queue (a task queue associated with wait_queue). This action
 * itself is the same what happens during os_waitqueue_wait() call. It mens
 * that the receiver is either way putted into sleep.
 *
 * The proof of concept is following. We must to consider 3 places where
 * interrupt or preemption may kick-in. This allows other task/ISR to issue
 * lines 10 and 11 on notifier side.
 * 1) between line 2 and 3
 * 2) between line 6 and 7
 *
 * One additional spot is between lines 3 and 4 but there task already knows that
 * it was signalized (because condition was meet) and it just need to clean-up
 * with os_waitqueue_finish() call.
 *
 * There are two main cases depending if receiver task would be preempted or
 * interrupt will be issued.
 *
 * If receiver is cooperating with ISR, both lines 10 and 11 are issued
 * simultaneously form user task perspective. Additionally keep in mind that
 * during ISR which may happen between mentioned 3 points, task_current is set
 * to task which we consider as vulnerable. If we look at os_waitqueue_wakeup()
 * we will see that there is a special case for this.
 * 1) waitqueue_current is atomically set before ISR, so in ISR line 10
 *    and 11 are done atomically vs the user code. If we look at
 *    os_waitqueue_wakeup_sync it will show that there is special case that
 *    detects that we are in ISR and we preempted the task which waits on the
 *    same condition which we try to signalize. In this case we only set
 *    waitqueue_current = NULL
 * 2) if we look at os_waitqueue_wait() we will see that it verify if
 *    waitqueue_current is still set, if not it exits right away. It will be
 *    NULL since we set it to NULL in line 11 of ISR (inside os_waitqueue_wakeup())
 *
 * If receiver is cooperating with some other task, then in
 * os_waitqueue_wakeup() the task_current is set to different value then task
 * which wait for condition. In other words now it is different story comparing
 * to ISR. What is more important, line 10 and 11 may not be issued atomically.
 * 1) if we will have task preemption at this point it is enough that notifier
 *    execute line 10, since receiver will detect that in line 3 (and will skip
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

/* private function forward declarations */
static void os_waitqueue_timerclbck(void* param);

/* --- public functions --- */
/* all public functions are documented in os_waitqueue.h file */

void os_waitqueue_create(os_waitqueue_t* queue)
{
   OS_ASSERT(NULL == waitqueue_current); /* cannot call from wait_queue loop */

   memset(queue, 0, sizeof(os_waitqueue_t));
   os_taskqueue_init(&(queue->task_queue));
}

void os_waitqueue_destroy(os_waitqueue_t* queue)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   OS_ASSERT(NULL == waitqueue_current); /* cannot call from wait_queue loop */

   arch_critical_enter(cristate);

   /* wake up all task which waits on sem->task_queue */
   while (NULL != (task = os_task_dequeue(&(queue->task_queue))))
   {
      /* destroy the timer if it was associated with task we plan to wake up */
      os_blocktimer_destroy(task);

      task->block_code = OS_DESTROYED;
      os_task_makeready(task);
   }

   /* destroy all wait queue data, this can create problems if this wait_queue
    * was also used from interrupt context (feel warned) */
   memset(queue, 0, sizeof(os_waitqueue_t));

   /* schedule to make context switch in case os_waitqueue_destroy() was called by
    * lower priority task than tasks which we just woken up */
   os_schedule(1);

   arch_critical_exit(cristate);
}

void os_waitqueue_prepare(os_waitqueue_t *queue)
{
   OS_ASSERT(0 == isr_nesting); /* cannot call os_waitqueue_prepare() from ISR */
   OS_ASSERT(task_current->prio_current > 0); /* idle task cannot call blocking functions (will crash OS) */
   /* check if task is not already subscribed on other wait_queue
    * currently we do not support waiting on multiple wait queues */
   OS_ASSERT(NULL == waitqueue_current);

   /* disable preemption */
   os_scheduler_intlock();
   /* mark that we are prepared to suspend on wait_queue */
   os_atomicptr_write(waitqueue_current, queue);
}

void os_waitqueue_break(void)
{
   OS_ASSERT(0 == isr_nesting); /* cannot call os_waitqueue_finish() from ISR */

   os_atomicptr_write(waitqueue_current, NULL);
   os_scheduler_intunlock(false); /* false = nosync, unlock scheduler and
                                   * schedule() to higher prio READY task
                                   * immediately */
}

os_retcode_t OS_WARN_UNUSEDRET os_waitqueue_wait(os_ticks_t timeout_ticks)
{
   os_retcode_t ret;
   os_timer_t timer;
   os_waitqueue_t *wait_queue;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* cannot call os_waitqueue_wait() form ISR */
   OS_ASSERT(task_current->prio_current > 0); /* idle task cannot call blocking functions (will crash OS) */
   /* in case of timeout guard, we need to have waitobj ptr valid */
   OS_ASSERT(timeout_ticks > OS_TIMEOUT_TRY); /* timeout must be either specific or infinite */

   /* we need to disable the interrupts since wait_queue may be signalized from
    * ISR (we need to add task to wait_queue->task_queue in atomic manner) there
    * is no sense to make some critical section optimizations here since this is
    * slow patch function (task decided to suspend) */
   arch_critical_enter(cristate);

   os_scheduler_intunlock(true); /* true = sync, unlock scheduler but do not schedule() yet */

   /* check if we are still in 'prepared' state
    * if not than it means that we where woken up by ISR in the mean time */
   if (waitqueue_current)
   {
      if (OS_TIMEOUT_INFINITE != timeout_ticks)
      {
         os_blocktimer_create(&timer, os_waitqueue_timerclbck, timeout_ticks);
      }

      wait_queue = waitqueue_current;
      waitqueue_current = NULL;
      os_block_andswitch(&(wait_queue->task_queue), OS_TASKBLOCK_WAITQUEUE);

      /* cleanup after return, destroy timeout if it was created */
      os_blocktimer_destroy(task_current);
   }

   /* the block code is either OS_OK (set in os_waitqueue_wakeup_sync()) or
    * OS_TIMEOUT (set in os_waitqueue_timerclbck()) or OS_DESTROYED (set in
    * os_waitqueue_destroy()), we just need to pick it up and return */
   ret = task_current->block_code;

   arch_critical_exit(cristate);

   return ret;
}

void os_waitqueue_wakeup_sync(
   os_waitqueue_t *queue,
   uint_fast8_t nbr,
   bool sync)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   /* tasks cannot call any OS functions if they are in wait_queue suspend
    * loop, with exception to ISR's which can interrupt task_current during
    * spinning on wait_queue suspend loop */
   OS_ASSERT((isr_nesting > 0) || (NULL == waitqueue_current));
   /* sync must be == false in case we are called from ISR */
   OS_ASSERT((isr_nesting == 0) || (sync == false));
   OS_ASSERT(nbr > 0); /* number of tasks to wake up must be > 0 */

   arch_critical_enter(cristate);

   /* check if we are in ISR but we interrupted the task which is prepared to suspend
    * on the same wait_queue which we signalize */
   if ((isr_nesting > 0) && (waitqueue_current == queue))
   {
      /* We are trying to wake up the task_current. This task is in RUNNING
       * state. Therefore task_current is not in wait_queue->task_queue of
       * wait_queue. The only action which we need to do in scope of scheduling
       * is disassociate it from wait_queue */
      waitqueue_current = NULL;

      /* since we theoretically prevented from sleep one task, now we have one
       * task less to wake up */
      if (OS_WAITQUEUE_ALL != nbr)
      {
        --nbr;
      }

      /* \TODO \FIXME consider that task_current may be less prioritized than
       * tasks in wait_queue->task_queue, so preventing it from sleep is not
       * fair in scope of scheduling (to some degree). There may be some more
       * prioritized task which should be woken up first, while this task maybe
       * should go to sleep. Also this is even more important in case parameter
       * nbr = 1.
       * The most fair approach would be to peek the wait_queue->task_queue and
       * compare the priorities. In case peeked task would be higher prioritized
       * then we should allow task_current to suspend on
       * wait_queue->task_queue and wakeup the pick most prioritized one */
   }

   while ((OS_WAITQUEUE_ALL == nbr) || ((nbr--) > 0))
   {
      /* chose most prioritized task from wait_queue->task_queue (for task with
       * equal priority threat them in FIFO manner) */
      task = os_task_dequeue(&(queue->task_queue));
      if (NULL == task)
      {
         /* there will be no more task to wake up, stop spinning */
         break;
      }

      /* we need to destroy the timer here, because otherwise it may fire right
       * after we leave the critical section */
      os_blocktimer_destroy(task);

      task->block_code = OS_OK; /* set the block code to NORMAL WAKEUP */
      os_task_makeready(task);

      /* do not call schedule() if user have some plans to do so.
       * User code may call some other OS function right away which will trigger
       * the os_schedule(). Parameter 'sync' is used for such optimization
       * request */
      if (!sync)
      {
         /* switch to more prioritized READY task, if there is such (1 as param
          * in os_schedule() means just that */
         os_schedule(1);
     }
   }
   arch_critical_exit(cristate);
}

/* --- private functions --- */

/**
 * Function called by timers module. Used for timeout of os_waitqueue_wait()
 * Callback to this function are done from context of os_timer_tick().
 */
static void os_waitqueue_timerclbck(void* param)
{
   /* single timer has param in os_blocktimer_create() as pointer to task structure */
   os_task_t *task = (os_task_t*)param;

   OS_SELFCHECK_ASSERT(TASKSTATE_WAIT == task->state);

   /* remove task from semaphore task queue (in os_waitqueue_wakeup() the
    * os_task_dequeue() does that */
   os_task_unlink(task);
   task->block_code = OS_TIMEOUT;
   os_task_makeready(task);
   /* we do not call the os_schedule() here, because this will be done at the
    * end of os_tick() (which calls the os_timer_tick() which then calls this
    * callback function) */

   /* we do not destroy timer here since this timer was created on stack of
    * os_waitqueue_wait(), there is proper cleanup code in that function */
   /* timer is not auto reload so we don't have to worry about if it will call
    * this function again */
}

#endif

