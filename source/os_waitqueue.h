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
 *   natively support such scenario (to wake up all the suspended tasks, nbr
 *   parameter from os_waitqueue_wakeup() should be given as OS_WAITQUEUE_ALL,
 *   no mater how many of task are currently suspended on wait_queue, all of
 *   them will be woken up excluding future wakeups of those which were not
 *   suspended in moment of os_waitqueue_wakeup() call.
 * - wait_queue does not disable interrupts in case receiver does not have to
 *   suspend nor use timeouts. This is because os_waitqueue_prepare() nor
 *   os_waitqueue_finalize() does not disable interrupts in such cases. Such
 *   approach mitigate adding of unnecessary jitter to interrupt service delay.
 */

/**
 * Definition of "wake up all tasks" operation given in nbr parameter of
 * os_waitqueue_wakeup()
 */
#define OS_WAITQUEUE_ALL ((uint_fast8_t)UINT8_MAX)

/** Definition of wait_queue structure */
typedef struct os_waitqueue_tag {
   /** Queue of tasks suspended on this wait_queue. This task_queue is also used
    * in os_task_makeready() to push task_current into connected
    * wait_queue->task_queue in case of preemption. Description of this process
    * is at os_waitqueue.c */
   os_taskqueue_t task_queue;

} os_waitqueue_t;

/** Definition of wait_object structure. This object is given to
 * os_waitqueue_wait() by task which would like to suspend on wait_queue */
typedef struct {
   /** timer used in case of timeout guard for os_waitqueue_wait() */
   os_timer_t timer;
}os_waitobj_t;

/**
 * Function creates the wait_queue
 *
 * Wait_queue structure can be allocated by from any memory. Function
 * initializes wait_queue structure given by @param queue (does not use dynamic
 * memory of any kind)
 *
 * @param queue pointer to wait_queue
 */
void os_waitqueue_create(os_waitqueue_t* queue);

/**
 * Function destroys the wait_queue
 *
 * Function overwrite wait_queue structure memory. As same as with
 * os_sem_create() it does not refer to any dynamic memory.
 *
 * @param queue pointer to wait_queue
 *
 * @pre wait_queue must be initialized prior call of this function
 * @pre this function cannot be called from ISR

 * @post wait_queue will be uninitialized after this call. Such wait_queue cannot
 *       be used by any other function until it will be initialized again. If
 *       wait_queue is also used from ISR (like for signaling) calling of
 *       os_waitqueue_destroy() may create race conditions. User must design
 *       application in a way which will prevent such cases to be possible (use
 *       after destroy). Tasks which was suspended on wait_queue prior call of
 *       os_waitqueue_destroy() will be released with OS_DESTROYED as return
 *       code from os_waitqueue_down(). But calls of os_waitqueue_down() after
 *       os_waitqueue_destroy() have been returned are forbidden.
 * @post this function may cause preemption since this function wakes up tasks
 *       suspended on wait_queue (possibly with higher priority than calling
 *       task)
 */
void os_waitqueue_destroy(os_waitqueue_t* queue);

/**
 * function prepares the task for suspend on wait_queue. After return from this
 * function task may safely execute any condition code to decide if it really
 * need to suspend. In case task would like to suspend by calling
 * os_waitqueue_wait() than just before that call ISR or another task would
 * change the condition and notify the wait_queue, the suspending task will
 * be woken up imidiately. In other words such construction prevents from race
 * conditions which would lead to loosing the wakeup notification.
 *
 * Following code sniplet shows proper usage of wait_queue API
 * 1: while(1) {
 * 2:   os_waitqueue_prepare(&waitqueue, timeout);
 * 3:   if(test_condition) {
 * 4:      os_waitqueue_finish();
 * 5:      break;
 * 6:   }
 * 7:   ret = os_waitqueue_wait();
 * 8: }
 *
 * @param queue pointer to wait_queue
 * @param waitobj wait_object needed in case of using timeout guard
 * @param timeout_ticks number of jiffies (os_tick() call count) before
 *        operation will time out. If user would like to not use of timeout, than
 *        @param timeout should be OS_TIMEOUT_INFINITE. Timeout is counted
 *        starting from entry to os_waitqueue_prepare(). If timeout will burn
 *        off before task calls os_waitqueue_wait() than os_waitqueue_wait()
 *        will immediately return with OS_TIMEOUT return code.
 *
 * @pre wait_queue must be initialized prior call of this function (please look
 *      at description of possible race conditions with os_waitqueue_destroy()
 * @pre this function cannot be used from ISR nor idle task
 *
 * @post after return from os_waitqueue_prepare(), task MUST either call
 *       os_waitqueue_finish() or os_waitqueue_wait(). Until those two calls
 *       task is threated as it stills evaluating suspend condition. Calling of
 *       other OS function at this period is forbidden (results are undefined).
 */
void os_waitqueue_prepare(
   os_waitqueue_t *queue,
   os_waitobj_t *waitobj,
   os_ticks_t timeout_ticks);

/**
 * Function breaks the suspend loop for wait_queue. Call to this function is
 * mandatory if task would like to finish the suspend loop without real
 * suspending.
 *
 * @pre os_waiqueue_prepare() MUST be called before os_waitqueue_finish().
 *      Results are undefined if task does not follow this rule.
 *
 * @post After return from this function, task may again use other OS functions.
 */
void os_waitqueue_finish(void);

/**
 * Function suspends task on wait_queue, until another task or ISR would wake it
 * up by os_waitqueue_wakeup() or until timeout burns off (timeout is defined by
 * timeout_ticks param of os_waiqueue_prepare()). The wait_queue on which task
 * will suspend is defined by queue param of os_waitqueue_prepare().
 *
 * @pre os_waitqueue_prepare() must be called prior this function.
 *      Results are undefined if task does not follow this rule.
 * @pre this function cannot be used from ISR nor idle task
 *
 * @return OS_OK in case task was woken up by os_waitqueue_wakeup() called from
 *         other task or ISR.
 *         OS_DESTROYED in case wait_queue was destroyed while task was
 *         suspended on wait_queue. Please read about race condition while
 *         destroying wait_queue described at documentation of
 *         os_waitqueue_destroy()
 *         OS_TIMEOUT in case operation timeout expired. Timeout is counted
 *         starting from entry to os_waitqueue_prepare(). If timeout will burn
 *         off before task calls os_waitqueue_wait() than os_waitqueue_wait()
 *         will immediately return with OS_TIMEOUT return code.
 * @note user code should always check the return code of os_waitqueue_wait()
 */
os_retcode_t OS_WARN_UNUSEDRET os_waitqueue_wait(void);

/**
 * Function wakes up tasks suspended on wait_queue.
 *
 * @param queue pointer to wait_queue from which we will wakeup taks
 * @param nbr number of task to wakeup. Must be > 0.
 * @param sync by passing 'true' in this parameter, user application will force
 *        signaling of wait_queue in synchronized mode. It means that there will be
 *        no context switches during os_waitqueue_wakeup_sync() call even if
 *        some higher priority task would be woken up. By passing 'false' in
 *        this parameter application specifies that context switches are
 *        allowed. This feature can be used in application code which will
 *        trigger the scheduler anyway after return from
 *        os_waitqueue_wakeup_sync() by some other OS API call. This helps to
 *        save CPU cycles by preventing from unnecessary context switches.
 *        This parameter must be set to 'false' in case function is called from
 *        ISR.
 *
 * @pre this function CANNOT be called by tasks which try to suspend on given
 *      wait_queue. In other words task cannot call os_waitqueue_wakeup() in
 *      case it already called os_waitqueue_prepare() on given wait_queue.
 * @pre this function CAN be called from ISR. This is one of basic use cases for
 *      wait_queues.
 */
void os_waitqueue_wakeup_sync(
   os_waitqueue_t *queue,
   uint_fast8_t nbr,
   bool sync);

/**
 * Function wakes up tasks suspended on wait_queue.
 * This is simplified version of os_waitqueue_wakeup_sync(). It is translated to
 * os_waitqueue_wakeup_sync(queue, nbr, false)
 *
 * @param queue pointer to wait_queue from which we will wakeup tasks
 * @param nbr number of task to wakeup. Must be > 0.
 *
 * @pre this function CANNOT be called by tasks which try to suspend on given
 *      wait_queue. In other words task cannot call os_waitqueue_wakeup() in
 *      case it already called os_waitqueue_prepare() on given wait_queue.
 * @pre this function CAN be called from ISR. This is one of basic use cases for
 *      wait_queues.
 */
static inline void os_waitqueue_wakeup(os_waitqueue_t *queue, uint_fast8_t nbr)
{
  os_waitqueue_wakeup_sync(queue, nbr, false);
}

#endif

