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

#ifndef __OS_SEM_
#define __OS_SEM_

/**
 * Semaphore is synchronization primitive. Comparing to mutex it has
 * following differences:
 * - semaphore can have multiple values (works as a counter) while mutex can
 *   have only two states: locked/unlocked (works as a flag)
 * - semaphore can be signaled from ISR (call of os_sem_up()), but you cannot
 *   call os_sem_down() from ISR
 * - semaphore is signalization primitive, in opposition to mutex nobody can own
 *   the semaphore. Thread can only wait until semaphore will be signaled but
 *   after signalization it does not make any permanent logic connection to this
 *   semaphore. The most common usage of semaphore is producer-consumer
 *   signalization mechanism.
 * - because of this semaphores does not have to (and in fact does not) prevent
 *   from priority inversion problem
 * - this clearly shows why semaphores should not be used for implementation of
 *   critical sections
 * - semaphores support timeout guard for os_sem_down() operation. The designed
 *   use case is to "wait for signal or timeout". In case of timeout the return
 *   code from os_sem_down() will be OS_TIMEOUTED
 */

/** Definition of semaphore structure */
typedef struct os_sem_tag {
   /** queue of threads suspended on this semaphore */
   os_taskqueue_t task_queue;

   /* Semaphore value, os_atomit_c since semaphores can be incremented from ISR */
   os_atomic_t value;

} os_sem_t;

/**
 * Function creates the semaphore.
 *
 * Semaphore structure can be allocated by from any memory. Function initializes
 * semaphore structure given by @param mtx (does not use dynamic memory of any
 * kind)
 *
 * @param sem pointer to semaphore
 * @param init_value initial value of semaphore, must be >= 0 and < * OS_ATOMIC_MAX
 */
void os_sem_create(os_sem_t* sem, os_atomic_t init_value);

/**
 * Function destroys the semaphore
 *
 * Function does overwite semaphore structure memory. As same as with
 * os_sem_create() it does not use any dynamic memory.
 *
 * @param sem pointer to semaphore
 *
 * @pre semaphore must be initialized prior call of this function
 * @pre this function cannot be called from ISR

 * @post semaphore will be uninitialized after this call. Such semaphore cannot
 *       be used by any other function until it will be initialized again. If
 *       semaphore is also used from ISR (like for signaling) calling of
 *       os_sem_destroy() may create race conditions. User must prevent design
 *       application in a way which will prevent such cases to be possible (use
 *       after destroy). Threads which was suspended on semaphore prior call of
 *       os_sem_destroy() will be released with OS_DESTROYED as return code from
 *       os_sem_down(). But calls of os_sem_down() after os_sem_destroy() have
 *       returned are forbidden.
 * @post this function may cause preemption since this function wakes up threads
 *       suspended on semaphore (possibly with higher priority than calling
 *       thread)
 */
void os_sem_destroy(os_sem_t* sem);

/**
 * Function consumes the single signal from semaphore. In case semaphore value
 * (signal counter) is 0 function will suspend calling thread on this semaphore.
 * Thread will be woken up if other thread will signal semaphore or when
 * requested timeout will burn off.
 *
 * @param sem pointer to semaphore
 * @param timeout_ticks number of jiffies (os_tick() call count) before
 *        operation will time out. If user would like to not use of timeout, than
 *        @param timeout should be OS_TIMEOUT_INFINITE.
 *        If user would like to perform TRY operation (which will return instead
 *        of suspending the thread) than @param timeout should be
 *        OS_TIMEOUT_TRY. In this scenario function return code will be
 *        OS_WOULDBLOCK.
 *
 * @pre semaphore must be initialized prior call of this function (please look at
 *      description of possible race conditions with os_sem_destroy()
 * @pre this function cannot be used from ISR nor idle task
 *
 * @return OS_OK in case signal was consumed from semaphore.
 *         OS_DESTROYED in case semaphore was destroyed while calling thread was
 *         suspended on semaphore
 *         OS_WOULDBLOCK in case semaphore did not contain any signals and
 *         @param timeout was OS_TIMEOUT_TRY
 *         OS_TIMEOUT in case operation timeout expired
 * @note user code should always check the return code of os_sem_down()
 */
os_retcode_t OS_WARN_UNUSEDRET os_sem_down(
   os_sem_t* sem,
   uint_fast16_t timeout_ticks);

/**
 * Function signalizes the semaphore
 *
 * @param sem pointer to semaphore
 * @param sync by passing 'true' in this parameter, user application will force
 *        semaphore signaling in synchronized mode. It means that there will be
 *        no context switches during os_sem_up_sync() call even if some higher
 *        priority task would be woken up. By passing 'false' in this parameter
 *        application specifies that context switches are allowed. This feature
 *        can be used in application code which will trigger the scheduler
 *        anyway after return from os_sem_up_sync() by some other API call. This
 *        helps to save CPU cycles by preventing from unnecessary context
 *        switches.
 *
 * @pre this function CAN be called from ISR. This is one of basic use cases for
 *      semaphore.
 */
void os_sem_up_sync(os_sem_t* sem, bool sync);

/**
 * Function signalizes the semaphore
 *
 * This is the simplified version of os_sem_up.
 * It is translated to os_sem_up_sync(sem, false)
 *
 * @param sem pointer to semaphore
 *
 * @pre this function CAN be called from ISR. This is one of basic use cases for
 *      semaphore.
 */
static inline void os_sem_up(os_sem_t* sem)
{
   os_sem_up_sync(sem, false);
}

#endif

