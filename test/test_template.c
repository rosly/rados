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

#include "project.h"
#include "os.h"

#include <stdio.h>
#include <time.h>

static os_task_t task1;
static os_task_t task2;
static OS_TASKSTACK task1_stack[OS_STACK_MINSIZE];
static OS_TASKSTACK task2_stack[OS_STACK_MINSIZE];
static os_sem_t sem1;
static os_sem_t sem2;

static sig_atomic_t task1_i = 0;
static sig_atomic_t task2_i = 0;

void sig_alrm(int signum, siginfo_t *siginfo, void *ucontext);
static timer_t timer;

/* test for OS port
- test if task_procedure is called and if it can block on semaphore, test if idle procedure will be called (because of task block)
- create two tasks and two semaphores and test if task can be switched betwen each other while blocking on sem and signalizing the oposite, this will test if context swithichg is working
- two task with endles loop, add timer ISR and call os_tick inside, task should switch during tiemr ISR
- create two tasks and two semaphores, tasks should block on sem, the semaphores should be signalized by timer ISR, by driving the timer freq all tree types of switching may be tested, intresting case is when timer is generated eah CPU cycle (can be used to test the critical sections)
*/

void OS_ISR sig_alrm(int OS_UNUSED(signum), siginfo_t * OS_UNUSED(siginfo), void *ucontext)
{
   arch_contextstore_i(sig_alrm);
   os_sem_up(&sem1);
   os_sem_up(&sem2);
   os_tick();
   arch_contextrestore_i(sig_alrm);
}

void idle(void)
{
   /* do nothing */
}

int task1_proc(void* param)
{
   os_retcode_t ret;

   param = param;
   while(1)
   {
      task1_i++;
      //os_sem_up(&sem2);
      ret = os_sem_down(&sem1, OS_TIMEOUT_INFINITE);
      test_assert(OS_OK == ret);
   }
}

int task2_proc(void* param)
{
   os_retcode_t ret;

   param = param;
   while(1)
   {
      task2_i++;
      //os_sem_up(&sem1);
      ret = os_sem_down(&sem2, OS_TIMEOUT_INFINITE);
      test_assert(OS_OK == ret);

      if( 0 == (task2_i % 100) )
         test_debug("%d %d\n", task1_i, task2_i);
   }
}

void init(void)
{
   int ret;
   struct sigevent sev = {
      .sigev_notify = SIGEV_SIGNAL,
      .sigev_signo = SIGALRM,
   };
   struct itimerspec its = {
      .it_interval = {
         .tv_sec = 0,
         .tv_nsec = 1,
      },
      .it_value = {
         .tv_sec = 0,
         .tv_nsec = 1,
      }
   };
   struct sigaction tick_sigaction = {
      .sa_sigaction = sig_alrm,
      .sa_mask = { { 0 } }, /* additional (beside the current signal) mask (they will be added to the mask instead of set) */
      .sa_flags = SA_SIGINFO , /* use sa_sigaction instead of old sa_handler */
      /* SA_NODEFER could be used if we would like to have the nesting enabled right durring the signal handler enter */
      /* SA_ONSTACK could be sed if we would like to use the signal stack instead of thread stack */
   };

   os_sem_create(&sem1, 0);
   os_sem_create(&sem2, 0);

   os_task_create(&task1, 1, task1_stack, sizeof(task1_stack), task1_proc, NULL);
   os_task_create(&task2, 1, task2_stack, sizeof(task2_stack), task2_proc, NULL);

   ret = sigaction(SIGALRM, &tick_sigaction, NULL);
   test_assert(0 == ret);
   ret = timer_create(CLOCK_PROCESS_CPUTIME_ID, &sev, &timer);
   test_assert(0 == ret);
   ret = timer_settime(timer, 0, &its, NULL);
   test_assert(0 == ret);
}

int main(void)
{
   os_start(init, idle);
   return 0;
}

