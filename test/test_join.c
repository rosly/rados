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

/**
 * /file Test os semaphore rutines
 * /ingroup tests
 *
 * Check (while intensive prremption is in charge)
 * - task finalization and joining when no other task waits for it, and then we join it
 * - task finalization and joining when other task is waiting for it
 * /{
 */

#include "os.h"
#include "os_test.h"

#define TEST_TASKS ((size_t)10)
#define TEST_CYCLES ((os_atomic_t)1000)

static os_task_t task_worker1;
static os_task_t task_worker2;
static os_task_t task_join;
static os_sem_t sem;
static long int task_worker1_stack[OS_STACK_MINSIZE];
static long int task_worker2_stack[OS_STACK_MINSIZE];
static long int task_join_stack[OS_STACK_MINSIZE];

void OS_ISR sig_alrm(int OS_UNUSED(signum), siginfo_t * OS_UNUSED(siginfo), void *ucontext)
{
   arch_contextstore_i(sig_alrm);
   os_tick();
   arch_contextrestore_i(sig_alrm);
}

void idle(void)
{
   /* no actions */
}

/**
 * test procedure for task which we will join
 */
int task_proc_worker1(void* OS_UNUSED( param))
{
   return 100;
}

/**
 * test procedure for task which we will join
 */
int task_proc_worker2(void* OS_UNUSED( param))
{
   int ret;

   ret = os_sem_down(&sem, OS_SEMTIMEOUT_INFINITE);
   test_assert(0 == ret);
   return 200;
}

/**
 * test procedure for task which we will use to wait for join
 */
int task_proc_join(void* OS_UNUSED( param))
{
   int ret;

   /* first join the worker1
       waiting for finite number of ticks allow task_worker1 to be shceduled for enough amout of time it will be finished before we test_result from this blocking call
       so for worker1 we will perform os_task_join in time when worker1 was already finished */
   ret = os_sem_down(&sem, 1000); /* give wrker1 and worker2 enough time to be scheduled */
   test_assert(OS_TIMEOUT == ret);
   ret = os_task_join(&task_worker1); /* join already finished worker1 */
   test_assert(100 == ret); /* check the return code from worker1 */

   /* next join the worker2,
       we push the sem so worker2 will wake up (but not imidiately)
       next worker2 cycle will be only after we block on os_task_join, so we join worker2 before it was finished */
   os_sem_up(&sem);
   ret = os_task_join(&task_worker2);
   test_assert(200 == ret);

   test_result(0);
   return 0;
}

void init(void)
{
   test_setuptick(NULL, 1);

   os_sem_create(&sem, 0);
   os_task_create(
      &task_worker1, 1, /* low priority so task_join will be scheduled in case of scheduler decision point */
      task_worker1_stack, sizeof(task_worker1_stack),
      task_proc_worker1, NULL);
   os_task_create(
      &task_worker2, 2, /* low priority so task_join will be scheduled in case of scheduler decision point */
      task_worker2_stack, sizeof(task_worker2_stack),
      task_proc_worker2, NULL);
   os_task_create(
      &task_join, 3,
      task_join_stack, sizeof(task_join_stack),
      task_proc_join, NULL);
}

int main(void)
{
   test_setupmain("Test_Join");
   os_start(init, idle);
   return 0;
}

/** /} */

