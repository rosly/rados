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
 * THIS SOFTWARE IS PROVIDED BY THE RADOS PROJET AND CONTRIBUTORS "AS IS" AND
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

/* If defined the stack will be checked in some points to check for stack overflow */
#define OS_CONFIG_CHECKSTACK (1) /**< Define this if you would like to check the tasks stack integrity, this requires a few additional resources (memory in os_task_t structure and CU at each stak check operation (may be quite expensive)), this define should be used during development while disabled before production if you are sure that your app does not exceed the assigned stacks */
#define OS_CONFIG_APICHECK (1) /**< Define this if you would like to check the API calls and general system assumption, if defined while your application broke some of important rules you will have an assertion, if not defined while appliation broke some rules the behaviour is not defined, this define should be enabled durring the development to chech the application behaviour and removed during the production compilation to achiv maximal performance (but keep in minthat if you dont test your app deeply before you can encounter serious problems) */
#define OS_CONFIG_PRIOCNT ((size_t)4) /**< Maximal number of priorities, this number should be as low as possible, not larget than required for paticular application compleity, this is because number of priorities significantly increase the memory consumption. Each mutex, semaphore etc uses a os_taskqueue_t which require task buckets, in count of available priorities */

/* in future ;) */
//#define OS_CONFIG_PREEMPTION (1) /**< Define if you whant to have preemption, disabling preemption can make kernel less featuread/responsive but should make it faster, this can be beneficial for some very onstrained enviroments where we dont have preemption at all */
//#define OS_CONFIG_SEMAPHORE /**< Define if you whant to have mutexes */
//#define OS_CONFIG_MUTEX /**< Define if you whant to have semaphores, keep in mind that semaphores is internaly used for task termination os_task_join call, if not defined os_task_join will return imidiately, this may change the bahaviour of aplication if it dont use semaphores explicitly bus use os_task_join call */
//#define OS_CONFG_CONDITIONAL
//#define OS_CONFIG_TIMER /**< remove if you dont whant to use timers, keep in mind that this will also remove all timeguards for blocking primitives such semaphores, this can change the bahaviour of aplication if it dont use timers explicitly (eg not calling the os_timer_x functions but use timeouts on os_sem_x functions), this may same some code spae and also decrease the os_tick run time, may be beneficial os mome very constrained systems where we dont what to have the preemprion and timers while still use the scheduler for task switching */

#endif /* __OS_CONFIG_ */

