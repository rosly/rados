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

#ifndef __OS_WAITQUEUE_
#define __OS_WAITQUEUE_

/**
 * Wait_queue is synchronization primitive simmilar to concept used in Linux kernel
 *
 * The idea behind wait_queue is to allow programer to synchronize os task, with
 * asynchronius events without need of pooling. In this case we usualy need to
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
 * conditional_test and os_waitqueue_wait() call. In some cases such problem
 * can be also solved by using of semaphore but wait_queues has several
 * advantages over semaphore.
 * - for producer-consumer scenarios where consumer task would just like to
 *   know if there was any notification (not how many of them) semaphore is not
 *   really the best choice from synchronization primitives. It's because in
 *   case multiple events will be signalized before receiver will suspend on
 *   semaphore, the cumulative semaphore value will be greater than 1.  This
 *   would require from receiver to spin several times in busy loop until it
 *   finally suspend on semaphore. The reason for that is receiver need to
 *   consume all signals from semaphore.
 * - wait_queue can be seen as "message box" or "message queue" form more
 *   traditional RTOS'es, but withound conceptual bounding to paticular data
 *   structure (like data pointer in case of message box or queue of data
 *   pointers for message queue). It means that wait_queue is more generic and
 *   data agnostic which allows to be used not only for data passing scheme.
 * - when using semaphores in scenarios where single notifier would like to wake
 *   up multiple receivers, we would have to know the number of receivers to be
 *   able to signalize the semaphore required number of times. In case of
 *   wait_queue we don't have to know the number of the receivers to wake all of
 *   them, notifier can simply pass OS_WAITQUEUE_ALL as nbr parameter of
 *   os_waitqueue_wakeup()
 * - os_waitqueue_prepare is very fast because it has optimized path for cases
 *   where task does not really need to sleep. Wait_queue is much faster then
 *   semaphore since we don't need to disable interrupts for
 *   os_waitqueue_prepare() (to be exact unless we specify timeout guard for
 *   that call).
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

#define OS_WAITQUEUE_ALL ((uint_fast8_t)UINT8_MAX)

typedef struct os_waitqueue_tag {
   /* task_queue for threads which are blocked or prepared to block on this wait
    * queue. This is typical task queue (no magic) it means that tasks in that
    * queue are in typical TASKSTATE_WAIT. For explanation how task_current is
    * placed in this task_queue instead of ready queue, read note around
    * task->wait_queue */
   os_taskqueue_t task_queue;

} os_waitqueue_t;

typedef struct {
   os_timer_t timer;
}os_waitobj_t;

void os_waitqueue_create(os_waitqueue_t* queue);
/** \NOTE calling this function for semaphores which are also used in ISR is
 *        highly forbiden since it will crash your kernel (ISR will access to
 *        data which will be destroyed) */
void os_waitqueue_destroy(os_waitqueue_t* queue);
void os_waitqueue_prepare(
   os_waitqueue_t *queue,
   os_waitobj_t *waitobj,
   os_ticks_t timeout_ticks);
void os_waitqueue_finish(void);
os_retcode_t OS_WARN_UNUSEDRET os_waitqueue_wait(void);
void os_waitqueue_wakeup_sync(
   os_waitqueue_t *queue,
   uint_fast8_t nbr,
   bool sync);
static inline void os_waitqueue_wakeup(os_waitqueue_t *queue, uint_fast8_t nbr)
{
  os_waitqueue_wakeup_sync(queue, nbr, false);
}

#endif

