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

#ifndef __OS_CONFIG_
#define __OS_CONFIG_

/** Define OS_CONFIG_CHECKSTACK if you would like to check the tasks stack
 * integrity. This requires a few additional resources (memory in os_task_t
 * structure and CU at each stack check operation (may be quite expensive)),
 * this define should be used during development while disabled for production
 * if you are sure that your tasks does not exceed assigned stacks */
#define OS_CONFIG_CHECKSTACK (1)

/** Define OS_CONFIG_APICHECK if you would like to check the API call parameters
 * and general system assertions. If defined while your application broke some
 * of OS rules you will have an assertion, if not defined while application
 * broke some OS rules the behaviour and consequences are undefined, this define
 * should be enabled during application development to verify the application
 * behaviour and removed for the production compilation to achieve maximal
 * performance */
#define OS_CONFIG_APICHECK (1)

/**< Define OS_CONFIG_SELFCHECKING to enable additional OS self checking
 * asserts. In comparison to OS_CONFIG_APICHECK this enables additional internal
 * OS state checks for implied rules. Those checks are placed around the OS code
 * to check the constrains during OS development. This define should be enabled
 * when developing the RadOS, this define does not have to be enabled for
 * application development */
#define OS_CONFIG_SELFCHECKING (1)

/** Maximal number of priorities. This number should be as low as possible, this
 * is because number of priorities significantly increase the memory consumption
 * (by increasing the task buckets count). Each synchronization primitive such
 * as mutex, semaphore etc. uses os_taskqueue_t which require task buckets */
#define OS_CONFIG_PRIOCNT ((unsigned)5)

/** Define to enable preemption. Disabling preemption can make kernel less
 * responsive but should make it faster, this can be beneficial for some very
 * constrained environments where we don't need preemption at all */
//TBD #define OS_CONFIG_PREEMPTION (1)

/** Define to enable semaphores. Keep in mind that semaphores is internally
 * used for os_task_join() call, if OS_CONFIG_SEMAPHORE is not defined
 * os_task_join() will return immediately. This may change the behaviour of
 * application even if it doesn't use semaphores explicitly but use
 * os_task_join() call */
//TBD #define OS_CONFIG_SEMAPHORE

/** Define to enable mutexes */
//TBD #define OS_CONFIG_MUTEX

/** Define to enable recursive mutexes */
//TBD #define OS_CONFIG_MUTEX_RECURSIVE

/** Define to enable priority inheritance for mutex */
#define OS_CONFIG_MUTEX_PRIO_INHERITANCE

/** Define to enable timers. Keep in mind that timers are used for time guard's
 * for blocking primitives such semaphores. This may change the behaviour of
 * application even if it doesn't use timers explicitly (eg not calling the
 * os_timer_x() functions but use timeouts on os_sem_x() functions). On the
 * other hand disabling timers should save some code space and also decrease the
 * os_tick() run time. This may be beneficial for some very constrained systems
 * where we both preemption and timers are not needed while still using the
 * scheduler for task switching */
//TBD #define OS_CONFIG_TIMER

/** Define to enable wait queues (synchronization primitive) */
#define OS_CONFIG_WAITQUEUE

/** Define to enable conditionals (synchronization primitive) */
//TBD #define OS_CONFG_CONDITIONAL

#endif /* __OS_CONFIG_ */

