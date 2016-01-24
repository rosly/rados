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

#ifndef __OS_TIMER_
#define __OS_TIMER_

/** Timers are gives possibility of postponed execution of given function.
 *
 * During timer creation, user must specify following parameters:
 * - pointer to timer object. Can be static or dynamic memory, which MUST be
 *   valid for whole lifetime (until timeout) of the timer. If timer is
 *   auto-reloaded, that memory must be valid until timer will be explicitly
 *   destroyed (for e.g. important in case for stack memory)
 * - timeout callback function which need to be following type:
 *   void timer_proc_t(void* param)
 * - parameter which will be passed to callback function
 * - number of system ticks until timeout
 * - number of system tick which will be used as next timeout in case of user
 *   would like to use auto-reload functionality (need to be 0 in case of single
 *   shot timers
 */

/** No-Timeout specifier used for semaphores and wait-queues. Use it for
 * os_sem_down() and os_waitqueue_wait() in case you don't want the timeout
 * limit for those calls. Cannot be used for os_timer_create() (does not make
 * sense for this particular call) */
#define OS_TIMEOUT_INFINITE (OS_TICKS_MAX)

/** Special timeout value for os_sem_down() which specify that call should not
 * block, but instead return with OS_WOULDBLOCK error code. Cannot be used for
 * os_timer_create() (does not make sense for this particular call) */
#define OS_TIMEOUT_TRY ((os_ticks_t)0)

/** Definition of timer callback function.  It takes single void* parameter
 * which value is specified during timer creation by timeout_ticks parameter of
 * os_timer_create() call */
typedef void (*timer_proc_t)(void *param);

/* Definition of timer structure */
typedef struct {
   list_t list;               /**< list header used for ordering timers */
   os_ticks_t ticks_rem;      /**< remaining ticks before burn off */
   os_ticks_t ticks_reload;   /**< reload value in case of auto reload */
   timer_proc_t clbck;        /**< timeout callback function pointer */
   void *param;               /**< parameter for timeout callback */
#ifdef OS_CONFIG_APICHECK
   uint_fast16_t magic;       /**< timer sanitization canary */
#endif
} os_timer_t;

/**
 * System tick function
 *
 * This function need to be called from system timer ISR. Periodic calls of this
 * function is the source of monotonic events for timers and preemption. The
 * frequency of os_tick() call is user defined (its usually called jiffy). All
 * timeouts for time guarded OS blocking functions are measured in system ticks.
 *
 * @pre can be called only from ISR
 *
 * /note The limitation of calling his function from ISR is because RadOS is
 *       preemptive kernel. In many cases os_ticks() will cause context switch.
 *       Because of that, calling this function from task context would require
 *       two context switches, since the user context itself has to be triggered
 *       by some source of interrupts. For now we forbid of using os_timer()
 *       from task context until someone will give reasonable justification for
 *       two context switches instead of one.  Also this assumption is base for
 *       using OS critical sections for os_timer_destroy() which has to
 *       synchronize with ISR code which call the os_tick() (we must prevent
 *       race conditions between timeout and os_timr_destroy())
 */
void OS_HOT os_tick(void);

/** Function creates the timer.
 *
 * Timer structure can be allocated by from any memory. Function initializes
 * timer structure given by @param sem. This can be static or dynamic memory
 * allocated by user (function does not allocate any additional memory). The
 * memory for timer structure MUST be valid for whole lifetime (until timeout)
 * of the timer. If timer is auto-reloaded, that memory must be valid until
 * timer will be explicitly destroyed (for e.g. important in case for stack
 * memory)
 *
 * @param timer pointer to timer
 * @param clbck timeout callback function which will be called on timeout
 * @param param parameter which will be passed to callback function
 * @param timeout_ticks number of system ticks until timeout
 * @param reload_ticks number of system tick which will be used as next timeout
 *        for auto-reload timers. User should use 0 in case of normal and != 0
 *        in case of request for creation of auto-reload timer
 *
 * @post It is forbidden to make consecutive calls to os_timer_create() with the
 *       with the same timer structure, without os_timer_destroy() in between.
 */
void os_timer_create(
   os_timer_t *timer,
   timer_proc_t clbck,
   void *param,
   os_ticks_t timeout_ticks,
   os_ticks_t reload_ticks);

/** Function destroys the timer
 *
 * Function stops the timer. Internally, this function removes the timer from
 * global timer list by which it prevents from future timeout. User can perform
 * multiple destroy operations on the same timer structure without wory about
 * consequences. The only requirement is that user make sure that the given
 * timer structure memory will be valid for each of such calls.  User MUST call
 * this function particular timer structure before it can be used again with
 * os_timer_create().
 *
 * @param timer pointer to timer structure
 *
 * /note In case OS_CONFIG_APICHECK this function uses canary to mark the timer
 *       as destroyed, which prevents improper use case scenarios.
 */
void os_timer_destroy(os_timer_t *timer);

/** Function return value of monotonic system tick counter.
 *
 * This function may be used to calculate the time difference between two
 * execution points.
 * Range of os_ticks_t is defined by architecture. Its size takes into account
 * the limitation of the CPU word size to minimize the cycle count required for
 * execution of os_tick(). Therefore user should take into account the overflow
 * of system tick counter. For easy calculation of time differences, use
 * os_ticks_diff() which takes the overflow scenario into account. The maximal
 * time interval which can be measured must be smaller than OS_TICKS_MAX.
 *
 * @return current value of monotonic system tick counter
 */
os_ticks_t os_ticks_now(void);

/** Function returns the time interval between two system ticks
 *
 * To get the current value of system tick counter, use the os_ticks_now()
 *
 * @param ticks_start The system tick counter value at the start of interval
 * @param ticks_now The system tick counter value at the end of interval
 *
 * @return Time interval measured in ticks between start and end
 */
os_ticks_t os_ticks_diff(
   os_ticks_t ticks_start,
   os_ticks_t ticks_end);

#endif

