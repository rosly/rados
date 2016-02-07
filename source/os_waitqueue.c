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

#ifdef OS_CONFIG_WAITQUEUE

/** Pointer to wait_queue on which task_current prepared to suspend.
 * Set in os_waitqueue_prep(). After os_waitqueue_prep(), preemption is disabled.
 * If this pointer is != NULL we say that task_current is in 'prepared' state
 * (prepared for suspend).
 */
os_waitqueue_t *waitqueue_current = NULL;

/** Timer for suspend on waitqueue. Set in os_waitqueue_prep()
 * This timeout will be used in os_waitqueue_wait() if os_waitqueue_prep()
 * will not suspend the calling task.
 */
static os_ticks_t waitqueue_timeout;

/* private function forward declarations */
static void os_waitqueue_timerclbck(void *param);

/* --- public functions --- */
/* all public functions are documented in os_waitqueue.h file */

void os_waitqueue_create(os_waitqueue_t *queue)
{
   OS_ASSERT(!waitqueue_current); /* cannot call after os_waitqueue_prep() */

   memset(queue, 0, sizeof(os_waitqueue_t));
   os_taskqueue_init(&(queue->task_queue));
}

void os_waitqueue_destroy(os_waitqueue_t *queue)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   OS_ASSERT(!waitqueue_current); /* cannot call after os_waitqueue_prep() */

   arch_critical_enter(cristate);

   /* wake up all task which suspended on wait_queue */
   while ((task = os_taskqueue_dequeue(&(queue->task_queue)))) {
      os_blocktimer_destroy(task); /* destroy the tasks timer */
      task->block_code = OS_DESTROYED;
      os_task_makeready(task);
   }

   /* destroy all wait queue data, this can create problems if this wait_queue
    * was also used from interrupt context (feel warned) */
   memset(queue, 0, sizeof(os_waitqueue_t));

   /* schedule to make context switch in case os_waitqueue_destroy() was called
    * by lower priority task than tasks which we just woken up */
   os_schedule(1);

   arch_critical_exit(cristate);
}

/* /note this function can be alled only from citical section */
static os_retcode_t OS_WARN_UNUSEDRET os_waitqueue_intwait(
   os_waitqueue_t *wait_queue,
   os_ticks_t timeout_ticks)
{
   os_timer_t timer;

   /* true = sync, unlock scheduler but do not schedule() yet */
   os_scheduler_intunlock(true);

   if (OS_TIMEOUT_INFINITE != timeout_ticks) {
      os_blocktimer_create(&timer, os_waitqueue_timerclbck, timeout_ticks);
   }

   /* clear global 'prepare' flag since we will switch the context */
   waitqueue_current = NULL;
   os_task_block_switch(&(wait_queue->task_queue), OS_TASKBLOCK_WAITQUEUE);

   /* cleanup after return, destroy timeout if it was created */
   os_blocktimer_destroy(task_current);

   /* the block code is either OS_OK (set in os_waitqueue_wakeup_sync()) or
    * OS_TIMEOUT (set in os_waitqueue_timerclbck()) or OS_DESTROYED (set in
    * os_waitqueue_destroy()), we just need to pick it up and return */
   return task_current->block_code;
}

os_retcode_t os_waitqueue_prep(
   os_waitqueue_t *wait_queue,
   os_ticks_t timeout_ticks)
{
   arch_criticalstate_t cristate;
   os_retcode_t ret = OS_OK;
   os_ticks_t start_ticks;
   os_task_t *task;

   OS_ASSERT(0 == isr_nesting); /* cannot call from ISR */
   OS_ASSERT(task_current != &task_idle); /* IDLE task cannot block */
   OS_ASSERT(timeout_ticks > OS_TIMEOUT_TRY); /* timeout must be either specific or infinite */
   /* calling of blocking function while holding mtx will cause priority inversion */
   OS_ASSERT(list_is_empty(&task_current->mtx_list));
   /* check if task is not already subscribed on other wait_queue
    * currently we do not support waiting on multiple wait queues */
   OS_ASSERT(!waitqueue_current);

   /* access to queue->task_queue requires critical section */
   arch_critical_enter(cristate);

   /* check if any task is already waiting inside this waitqueue
    * and if yes than if it has higher priority than current task */
   task = os_taskqueue_peek(&wait_queue->task_queue);
   if (task && task->prio_current > task_current->prio_current) {
      /* since more prioritized task is waiting for the same event, we cannot
       * allow for less prioritized task to potentialy get the notification once
       * it leave os_waitqueue_prep(). This would be possible if in this
       * curent moment there is pending notification from IRQ */

      /* we need to calculate how many ticks task has already sleept */
      start_ticks = ticks_current;

      /* suspend the task */
      ret = os_waitqueue_wait(wait_queue, timeout_ticks);

      /* we was woken up - recalculate the remaining timeout */
      if (timeout_ticks != OS_TIMEOUT_INFINITE)
         timeout_ticks -= os_ticks_diff(start_ticks, ticks_current);
   }

   if (OS_OK == ret) {
      /* in case there was no task on waitqueue or curent task was woken-up, we
       * know that curent task is the most priritized one and we should try the
       * condition assosiated with waitqueue */

      /* disable preemption */
      os_scheduler_intlock();

      /* store the timeout for use of os_waitqueue_wait() */
      waitqueue_timeout = timeout_ticks;

      /* mark that we are prepared to suspend on wait_queue */
      waitqueue_current = wait_queue;
   }

   arch_critical_exit(cristate);

   return ret;
}

void os_waitqueue_break(void)
{
   OS_ASSERT(0 == isr_nesting); /* cannot call os_waitqueue_finish() from ISR */

   os_atomicptr_store(&waitqueue_current, (os_waitqueue_t*)NULL);
   /* false = nosync, unlock scheduler and schedule() to higher prio READY task
    * immediately */
   os_scheduler_intunlock(false);
}

os_retcode_t OS_WARN_UNUSEDRET os_waitqueue_wait(void)
{
   os_retcode_t ret;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* cannot call form ISR */
   OS_ASSERT(task_current != &task_idle); /* IDLE task cannot block */
   /* calling of blocking function while holding mtx will cause priority inversion */
   OS_ASSERT(list_is_empty(&task_current->mtx_list));

   /* we need to disable the interrupts since wait_queue may be signalized from
    * ISR (we need to add task to wait_queue->task_queue in atomic manner) there
    * is no sense to make some critical section optimizations here since this is
    * slow patch function (task decided to suspend) */
   arch_critical_enter(cristate);

   /* check if we are still in 'prepared' state
    * if not than it means that we were woken up by ISR in the mean time */
   if (waitqueue_current) {
      ret = os_waitqueue_intwait(waitqueue_current, waitqueue_timeout);
   }

   arch_critical_exit(cristate);

   return ret;
}

void os_waitqueue_wakeup_sync(
   os_waitqueue_t *queue,
   uint_fast8_t wakeup_cnt,
   bool sync)
{
   arch_criticalstate_t cristate;
   os_task_t *task;
   bool awoken = false;

   /* tasks cannot call any OS functions if they are are 'prepared' to suspend
    * on wait_queue, with exception to ISR's which can interrupt task_current.
    * In this case waitqueue_current will be in 'prepared' state (we need to
    * handle that case) */
   OS_ASSERT((isr_nesting > 0) || (!waitqueue_current));
   /* sync must be == false in case we are called from ISR */
   OS_ASSERT((isr_nesting == 0) || (sync == false));
   OS_ASSERT(wakeup_cnt > 0); /* number of tasks to wake up must be > 0 */

   arch_critical_enter(cristate);

   /* check if we are in ISR but we interrupted the task which is prepared to
    * suspend on the same wait_queue which we will signalize */
   if ((isr_nesting > 0) && (waitqueue_current == queue)) {
      /* We are trying to wake up the task_current from ISR.
       * task_current is in 'prepared' state and it is not pushed to any
       * task_queue. The only action which we need to do is communicate with
       * task_current that the suspend on wait_queue should not be done. We will
       * make this by clearing out the 'prepared' state */
      waitqueue_current = NULL;

      /* since we theoretically prevented from sleep one task, now we have one
       * task less to wake up */
      if (OS_WAITQUEUE_ALL != wakeup_cnt)
         --wakeup_cnt;
   }

   while ((OS_WAITQUEUE_ALL == wakeup_cnt) || (wakeup_cnt-- > 0)) {
      /* chose most prioritized task from wait_queue->task_queue (for task with
       * equal priority threat them in FIFO manner) */
      task = os_taskqueue_dequeue(&(queue->task_queue));
      if (!task) {
         /* there will be no more task to wake up, stop spinning */
         break;
      }

      /* we need to destroy the timer here, because otherwise it may fire right
       * after we leave the critical section */
      os_blocktimer_destroy(task);

      task->block_code = OS_OK; /* set the block code to NORMAL WAKEUP */
      os_task_makeready(task);
      awoken = true;
   }

   /* To wake up all task exactly once, we have to schedule() out of wakeup
    * loop. But do not call schedule() if user requested sync mode.
    * User code may call some other OS function right away which will trigger
    * the os_schedule(). Parameter 'sync' is used for such optimization
    * request */
   if (!sync && awoken) {
      /* switch to more prioritized READY task, if there is such (1 as param
       * in os_schedule() means just that */
      os_schedule(1);
   }

   arch_critical_exit(cristate);
}

/* --- private functions --- */

/**
 * Function called by timers module. Used for timeout of os_waitqueue_wait()
 * Callback to this function are done from context of timer_trigger().
 */
static void os_waitqueue_timerclbck(void *param)
{
   /* single timer has param in os_blocktimer_create() as pointer to task
    * structure */
   os_task_t *task = (os_task_t*)param;

   OS_SELFCHECK_ASSERT(TASKSTATE_WAIT == task->state);

   /* remove task from task queue (in os_waitqueue_wakeup() the
    * os_taskqueue_dequeue() does the same job */
   os_taskqueue_unlink(task);
   task->block_code = OS_TIMEOUT;
   os_task_makeready(task);
   /* we do not call the os_schedule() here, because this will be done at the
    * end of timer_trigger() */

   /* we do not destroy timer here since this timer was created on stack of
    * os_waitqueue_wait(), there is proper cleanup code in that function */
   /* timer is not auto reload so we don't have to worry about if it will call
    * this function again */
}

/**
 * Technical description of last change for wait_queue mechanics.
 *
 * Following template of code was used on receiver side:
 * 0: uint32_t test_condition; does not have to be atomic type
 *
 * 1: while(1) {
 * 2:   os_waitqueue_prep(&waitqueue, timeout);
 * 3:   if (test_condition) {
 * 4:      os_waitqueue_break();
 * 5:      break;
 * 6:   }
 * 7:   ret = os_waitqueue_wait();
 * 8: }
 *
 * Following template was used on notifier side:
 * 10: conditional_test = 1;
 * 11: os_waitqueue_wakeup(&waitqueue);
 *
 * Previous implementation allowed for preemption during step 3. This was not
 * valid solution since if the condition was 'true' and preemption happened in
 * 3, than such preempted task instead of going to ready_queue was pushed to
 * wait_queue->task_queue. The problem is that such task could be never woken
 * up, since the condition was already set to 'true' (the wakeup signal should
 * not be needed, receiver task should be able to detect the condition 'true')
 *
 * In current solution we forbid for preemption in step 3 by disabling the
 * scheduler. This simplify the code a lot, since the only case where task could
 * be woken up between 2 and 7 is the case of interrupt. In ISR we simply check
 * the global waitqueue_current pointer to see if ISR is trying to wakeup the
 * task_current. In other words we removed all complication from
 * os_task_makeready() (which was very ugly BTW).
 */

#endif

