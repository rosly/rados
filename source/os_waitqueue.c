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
 * scheduler checks the value of task_current->wait_queue pointer. If that
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
 * 1) task_current->wait_queue is atomically set before ISR, so in ISR line 10
 *    and 11 are done atomically vs the user code. If we look at
 *    os_waitqueue_wakeup_sync it will show that there is special case that
 *    detects that we are in ISR and we preempted the task which waits on the
 *    same condition which we try to signalize. In this case we only set
 *    task_current->wait_queue = NULL
 * 2) if we look at os_waitqueue_wait() we will see that it verify if
 *    task_current->wait_queue is still set, if not it exits right away. It will be
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
   memset(queue, 0, sizeof(os_waitqueue_t));
   os_taskqueue_init(&(queue->task_queue));
}

void os_waitqueue_destroy(os_waitqueue_t* queue)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   arch_critical_enter(cristate);

   /* wake up all task which waits on sem->task_queue */
   while (NULL != (task = os_task_dequeue(&(queue->task_queue))))
   {
      /* destroy the timer if it was associated with task we plan to wake up */
      os_blocktimer_destroy(task);

      task->wait_queue = NULL;
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

void os_waitqueue_prepare(
   os_waitqueue_t *queue,
   os_waitobj_t *waitobj,
   os_ticks_t timeout_ticks)
{
   OS_ASSERT(0 == isr_nesting); /* cannot call os_waitqueue_prepare() from ISR */
   OS_ASSERT(task_current->prio_current > 0); /* idle task cannot call blocking functions (will crash OS) */
   /* check if task is not already subscribed on other wait_queue
    * currently we do not support waiting on multiple wait queues */
   OS_ASSERT(NULL == task_current->wait_queue);
   /* in case of timeout guard, we need to have waitobj ptr valid */
   OS_ASSERT((OS_TIMEOUT_INFINITE == timeout_ticks) || (NULL != waitobj));
   OS_ASSERT(timeout_ticks > OS_TIMEOUT_TRY); /* timeout must be either specific or infinite */

   /* associate/link task with wait_queue, this will change the behaviour inside
    * os_task_makeready() and instead of ready_queue, task will be added to
    * wait_queue (task queue form wait_queue wait_queue->task_queue) */
   os_atomicptr_write(task_current->wait_queue, queue);
   task_current->block_code = OS_OK;

   /* create and start timer (count time from now on). By doing so, time keeping
    * will be done for both, the user condition checking code and sleep time
    * inside os_waitqueue_wait() */
   if (OS_TIMEOUT_INFINITE != timeout_ticks)
   {
      arch_criticalstate_t cristate;

      /* timer addition requires os critical section - unfortunately ;( */
      arch_critical_enter(cristate);
      os_blocktimer_create(
         &(waitobj->timer), os_waitqueue_timerclbck, timeout_ticks);
      arch_critical_exit(cristate);
   }
}

/* \TODO change it to os_waitqueue_break() */
void os_waitqueue_finish(void)
{
   OS_ASSERT(0 == isr_nesting); /* cannot call os_waitqueue_finish() from ISR */

   /* sine os_waitqueue_finish() should be fast we should consider of cost
    * optimization by not entering critical section. For that we can check by
    * atomic operation if we had assigned timer for timeout guard. If so than we
    * will have to enter the critical section, if not than we can prevent from
    * interrupt handling jitter and also save some CPU cycles */
   if (NULL != os_atomicptr_read(task_current->timer))
   {
      arch_criticalstate_t cristate;

      /* timer manipulation requires os critical section - unfortunately ;( */
      arch_critical_enter(cristate);

      /* remove wait_queue to task association/link, since we are in os critical
       * section we do not have to use arch dependent atomicptr assignment, it
       * may (but for most arch will not) add additional overhead */
      task_current->wait_queue = NULL;

      /* destroy timeout associated with task. Good to note is that we check
       * task_curent->timer second time inside os_blocktimer_destroy(), since
       * previous checking was done outside critical section (things could
       * changed from that moment) */
      os_blocktimer_destroy(task_current);

      arch_critical_exit(cristate);

   } else {
      /* we can clear the wait_queue to task association/link without critical
       * section by using of atomic operation */
      os_atomicptr_write(task_current->wait_queue, NULL);
   }
}

os_retcode_t OS_WARN_UNUSEDRET os_waitqueue_wait(void)
{
   os_retcode_t ret;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* cannot call os_waitqueue_wait() form ISR */
   OS_ASSERT(task_current->prio_current > 0); /* idle task cannot call blocking functions (will crash OS) */

   /* we need to disable the interrupts since wait_queue may be signalized from
    * ISR (we need to add task to wait_queue->task_queue in atomic manner)
    * there is no sense to make some critical section optimizations here since
    * this is slow patch function (task decided to suspend) */
   arch_critical_enter(cristate);

   /* check if we are still in 'prepared' state
    * if not than it means that we where woken up in mean time by other task,
    * ISR or timer timeout callback */
   if (task_current->wait_queue)
   {
      /* there was no wakeup nor timeout, block and switch the context */
      os_block_andswitch(&(task_current->wait_queue->task_queue),
                         OS_TASKBLOCK_WAITQUEUE);
   }

   /* cleanup, destroy timeout if it was created */
   os_blocktimer_destroy(task_current);

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

   OS_ASSERT(nbr > 0); /* number of tasks to wake up must be > 0 */

   /* For mtx and sem we wake up tasks that have been placed in sem->task_queue.
    * But for wait_queue we also have to consider task which prepared for
    * sleeping but does not actually yet been suspended. Imagine scenario where
    * os_waitqueue_wakeup() is called from ISR and we interrupted the task,
    * which tried to suspend on the same wait_queue which the ISR is going to
    * signalize. It will mean that task_current is after os_waitqueue_prepare()
    * and right now spinning and checking the user condition while the interrupt
    * happened. */

   /* the golden rule is that tasks cannot wake his own wait_queues beside
    * situation where its looking like that by it because it was interrupted by ISR */
   OS_ASSERT((isr_nesting > 0) || (task_current->wait_queue != queue));
   /* sync must be == false in case we are called from ISR */
   OS_ASSERT((isr_nesting == 0) || (sync == false));

   arch_critical_enter(cristate);

   /* check if we are in ISR but we interrupted the task which is prepared to suspend
    * on wait_queue which one ISR is signalizing (and there was no timeout yet)
    */
   if ((isr_nesting > 0) && (task_current->wait_queue == queue))
   {
      /* We are trying to wake up the task_current. Since this task is already
       * in RUNNING state it would be waste of CPU power to force it to sleep
       * and then wakeup another task.  (unnecessary co next switches) in case
       * we put this task to ready queue and pick another some other one).  If
       * we allow it to run than we will not broke the scheduling semantics
       * (priorities and FIFO), since everything would be almost the same it the
       * interrupt would actually trigger just few cycles latter than in
       * reality, right ? Since task_current is not in task_queue of wait_queue,
       * the only action which we need to do in scope of scheduling is
       * disassociate it from wait_queue */
      task_current->wait_queue = NULL;

      /* we need to destroy the timer here, because otherwise it may fire right
       * after we leave the critical section */
      os_blocktimer_destroy(task_current);

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
       * One additional note: look at os_schedule() at bottom of this function,
       * this will not help either, because if nbr was 1, then this bottom
       * section will not even fire.
       * The most fair approach would be to put task_current into
       * wait_queue->task_queue and then pick most prioritized one */
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

      task->wait_queue = NULL; /* disassociate task from wait_queue */
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
   os_task_t *task = (os_task_t*)param;

   /* remove the wait_queue to task association
    * only then we can call os_task_makeready(), check internals of it */
   task->wait_queue = NULL;
   task->block_code = OS_TIMEOUT;

   /* unlike to semaphores, timeout for wait_queues are created and than task is
    * still in running state (we leave the OS critical section). It may happened
    * that this timer will burn off while task is still running and not yet
    * reached os_waitqueue_wait() function.
    * It means that task for this timer may be in any state, WAIT, READY or even
    * RUNNING. The WAIT state is most crucial since it will mean that this task
    * is on some task_list. As in typical wake up sequence we need to do
    * call os_task_makeready() then */
   if (TASKSTATE_WAIT == task->state)
   {
      /* remove task from wait_queue->task queue (in os_waitqueue_wakeup() the
       * os_task_dequeue() does that */
      os_task_unlink(task);
      os_task_makeready(task);
   }
   /* we do not call the os_schedule() here, because this will be done at the
    * end of os_tick() (which calls the os_timer_tick() which then calls this
    * callback function) */
   /* we do not need to destroy timer here, since it will be destroyed by
    * cleanup code inside the woken up task (at os_waitqueue_finish() or
    * os_waitqueue_wait()) */
   /* timer is not auto reload so we don't have to worry about if it will call
    * this function again */
}

