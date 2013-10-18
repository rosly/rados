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
 * /file Test os OS port (step 2)
 * /ingroup tests
 *
 * This is second of basic test to check the port.
 * This test verify arch_context_switch() implementation and checks if task can
 * be switched betwen two task while blocking on sem and signalizing the
 * oposite, Also test some minor features like task param, task return value,
 * and task finalization.
 * Test if following services are implemented corecly:
 * - arch_context_switch implemented corectly (will be called at each task blocking call)
 * - arch_task_init implemented correcly(at least param passing and calling
 *   os_task_test_result at the end of task lifetime) /{
 */

#include "os.h"
#include "os_test.h"

#define TEST_CYCLES ((unsigned)100)

typedef struct {
   os_sem_t sem;
   unsigned loop;
} task_data_t;

static os_task_t    task1;
static os_task_t    task2;
static OS_TASKSTACK task1_stack[OS_STACK_MINSIZE];
static OS_TASKSTACK task2_stack[OS_STACK_MINSIZE];
static task_data_t  task_data[2];

void test_idle(void)
{
   /* both task must run exacly X times, smaller value means OS scheduling bug */
   test_assert(TEST_CYCLES == task_data[0].loop);
   test_assert(TEST_CYCLES == task_data[1].loop);
   test_result(0);
}

int task_proc(void* param)
{
   int ret;
   unsigned idx = (unsigned)(size_t)param;

   while(task_data[idx].loop < TEST_CYCLES)
   {
      ++(task_data[idx].loop);
      os_sem_up(&(task_data[(idx + 1) % 2].sem));
      ret = os_sem_down(&(task_data[idx].sem), OS_TIMEOUT_INFINITE);
      test_assert(0 == ret);
   }

   return 0;
}

void test_init(void)
{
   memset(&task_data[0], 0, sizeof(task_data_t));
   memset(&task_data[1], 0, sizeof(task_data_t));
   os_sem_create(&(task_data[0].sem), 0);
   os_sem_create(&(task_data[1].sem), 0);
   os_task_create(&task1, 1, task1_stack, sizeof(task1_stack), task_proc, (void*)0);
   os_task_create(&task2, 1, task2_stack, sizeof(task2_stack), task_proc, (void*)1);
}

int main(void)
{
   test_setupmain("Test2");
   os_start(test_init, test_idle);
   return 0;
}

/** /} */

