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

#ifndef __OS_MTX_
#define __OS_MTX_

/**
 * Mutex is synchronization primitive. Comparing to semaphores it has
 * following differences:
 * - mutex cannot be used from ISR
 * - mutex can have only two states: locked/unlocked (works as a flag),
 *   while semaphore can have multiple values (works as a counter)
 * - mutex has concept of ownership. The locked mutex can be unlocked only by
 *   the owner (task which recently lock it up), unlocking from another task will
 *   result in assertion, this may be seen as error checking feature of the mutex
 *   but it will assert only when OS_CONFIG_APICHECK is defined
 * - mutex prevents from priority inversion problem while semaphores does not.
 *   Priority inheritance will boost the priority of task that holds the mutex
 *   to level of most prioritized thread which try's to obtain the lock
 * - mutex support the recursive locks. In other words owner which try's to lock
 *   the mutex again will not be blocked but simply increment the level of
 *   recursive lock (mutex tracks the owner). To free the mutex it must be
 *   unlocked the same number of times as many lock operations were done.
 * - mutex lock operation does not have the timeout (they are not needed sine
 *   the first reason of timeout for mutexes in RTOS is mitigating deadlocks
 *   BUGS. Using timeout as a solution for deadlock BUGS is really bad idea.
 *   Other reasons where timeout for mtx are needed are usually when application
 *   is not well designed and it cannot meet real-time constrains.  All
 *   requirement's and limitation of CPU and OS should be known prior coding).
 */

/** Definition of mutex structure */
typedef struct {
   /** list header that allows to place this mtx on various lists */
   list_t listh;

   /** Task which currently owns the mutex */
   os_task_t *owner;

   /** Queue of threads suspended on this mutex */
   os_taskqueue_t task_queue;

   /** Recursive lock count
    * (it does not need to be sig_atomic_t since only the owner task can change
    * this value and using of mtx form ISR is forbidden */
   uint_fast8_t recur;

} os_mtx_t;

/**
 * Function initializes the mutex structure given by @param mtx
 *
 * @param pointer to mutex
 */
void os_mtx_create(os_mtx_t* mtx);

/**
 * Function destroys the mutex given by @param mtx
 *
 * @param pointer to mutex
 *
 * @pre mutex must be initialized prior call of this function
 * @pre only owner of the mutex should call this function. This also means that
 *      if given mutex is not locked, user code must prevent any race conditions
 *      between os_mtx_lock() and os_mtx_destroy(). Because of this well written
 *      code should lock the mutex before destroying it (even if
 *      os_mtx_destroy() function accept unlocked mutex)
 * @pre this function cannot be used from ISR
 *
 * @post mutex will be uninitialized after this call. Such mutex cannot be used
 *       by any other function until it will be initialized again. It means that
 *       user code has to prevent race conditions of accessing such mutex after
 *       return from os_mtx_destroy(). Threads which had waited for mutex before
 *       os_mtx_destroy() will be released with OS_DESTROYED return code from
 *       os_mtx_lock(). But calls of os_mtx_lock() after os_mtx_destroy() have
 *       returned are forbidden.
 *
 * @post this function also reset the prio of calling thread in case it was
 *       boosted by priority inheritance
 */
void os_mtx_destroy(os_mtx_t* mtx);

/**
 * Function locks the mutex. If the mutex is already locked the calling thread
 * will sleep until owner thread will call os_mtx_unlock(). Only single thread
 * can own the mutex at given time.
 *
 * @param pointer to mutex
 *
 * @pre mutex must be initialized prior call of this function (please look at to
 *      description of possible race conditions with os_mtx_destroy()
 * @pre this function cannot be used from ISR
 *
 * @return OS_OK in case mutex was successfully locked by calling thread
 *         OS_DESTROYED in case mutex was destroyed while calling thread waits
 *         for acquiring of the lock
 * @note user code should always check the return code of os_mtx_lock()
 */
os_retcode_t OS_WARN_UNUSEDRET os_mtx_lock(os_mtx_t* mtx);

/**
 * Function unlock the mutex. Only owner thread can call this function.
 *
 * @param pointer to mutex
 *
 * @pre mutex must be locked (owned) by thread that calls this function
 * @pre this function cannot be used from ISR
 */
void os_mtx_unlock(os_mtx_t* mtx);

#endif

