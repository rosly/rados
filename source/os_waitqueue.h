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

#ifndef __OS_WAITQUEUE_
#define __OS_WAITQUEUE_

/**
 * Wait_queue is synchronization primitive used for all variety of
 * notifier-receiver scenarios including those that involve data exchange like
 * producer-consumer
 *
 * Following template of code is used on receiver side:
 * 0: uint32_t test_condition; does not have to be atomic type
 *
 * 1: while(1) {
 * 2:   os_waitqueue_prepare(&waitqueue, timeout);
 * 3:   if(test_condition) {
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
 * Wait_queue has following characteristics:
 * - it is a building block for notifier/receiver scenario where notifier side
 *   triggers notification and receiver side is being notified about event
 * - could be used as synchronization mechanism for data exchange scenario
 *   between parties but it is not bounded to data type being exchanged or
 *   exchange scheme.
 * - the notifier side calls os_waitqueue_wakeup() in order to notify the
 *   receiver.
 * - the receiver side calls two functions in row, os_waitqueue_prepare() and
 *   than os_waitqueue_wait() in order to suspend for event or
 *   os_waitqueue_finish() in order to break from suspend loop
 * - wait_queue does not accumulate notifications. In other words any
 *   notification posted when no receiver is waiting on wait_queue will be lost.
 *   It means that notification will not make any effects on future of receiver
 *   actions in case notifier calls os_waitqueue_wakeup() before receiver enter
 *   to os_waitqueue_prepare() or already exit from os_waitqueue_wait().  But if
 *   notifier calls os_waitqueue_wakeup() after receiver entered to
 *   os_waitqueue_prepare() and before return from os_waitqueue_wait() it is
 *   guarantied to wake up the receiver task (is is guarantied that
 *   os_waitqueue_wait() will force receiver to return).
 * - usage of work_queues eliminates the race condition which may arise between
 *   receiver check the test_condition and suspend on os_waitqueue_wait().
 * - it also means that condition in line 3 does not have to be atomic. It only
 *   cannot give false positives. There is guarantee that after calling
 *   os_waitqueue_prepare() the receiver task will either see the condition_test
 *   to be true or it will be woken up to check the condition_test again.
 *
 * - wait_queue should be used instead of semaphore in scenarios where receiver
 *   task would just like to know if there was any notification (not how many of
 *   them). If we really would like to use semaphore for that case, than in case
 *   multiple events will be signalized before receiver will suspend on
 *   semaphore, the cumulative semaphore value will be greater than 1.  This
 *   would potentially require that receiver should spin several times in busy
 *   loop to detect that there was no new event before finally suspend on
 *   empty semaphore. Semaphore limit also does not solve the case since we
 *   would have to modify the condition than signalize the semaphore. In case of
 *   two notifications in row it is possible that receiver will preempt notifier
 *   between condition change and second signalization of semaphore. Receiver
 *   will than check status of second condition change and could wrap around back
 *   to suspend on semaphore. After context switch to notifier it will finish
 *   the remain signalization of semaphore which will unnecessary wake up
 *   receiver.
 * - wait_queue also allows to implement more sophisticated scenarios like where
 *   single notifier would like to wake up multiple receivers. Again semaphore
 *   cannot be really used for that, since we would have to know the number of
 *   receivers to be able to signalize the semaphore required number of times.
 *   Even so there is no guarantee that all tasks will be woken up since one or
 *   more tasks may consume more than one signal from semaphore. In case of
 *   wait_queue we don't have to know the number of the receivers since it
 *   natively support such scenario (to wake up all the suspended threads, nbr
 *   parameter from os_waitqueue_wakeup() should be given as OS_WAITQUEUE_ALL,
 *   no mater how many of task are currently suspended on wait_queue, all of
 *   them will be woken up excluding future wakeups of those which were not
 *   suspended in moment of os_waitqueue_wakeup() call.
 * - wait_queue does not disable interrupts in case receiver does not have to
 *   suspend nor use timeouts. This is because os_waitqueue_prepare() nor
 *   os_waitqueue_finalize() does not disable interrupts in such cases. Such
 *   approach mitigate adding of unnecessary jitter to interrupt service delay.
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

