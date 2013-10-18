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
 * /file Test os OS port (step 3)
 * /ingroup tests
 *
 * This is third of basic test to check the port.
 * Test checks it preemption is working by starting two CPU intensive tasks
 * which don't call any of OS functions.  In backgroung tick procedure should
 * kick the preemption and switch betwen tasks. If test_setuptick() is
 * implemented corectly to call os_tick(), tasks should  be forced to give up
 * the CPU to the other task. When both task will finish, test_idle task should be
 * scheduled() and check the results.
 *
 * Test in following services are
 * implemented corecly:
 * - test is arch_context_switch fully working (from preemptive point of view) (will be called at each os_tick)
 * - preemption and test_setupmain()
 * /{
 */

#include "os.h"
#include "os_test.h"

#define TEST_CYCLES ((unsigned)100)

static os_task_t task1;
static os_task_t task2;
static OS_TASKSTACK task1_stack[OS_STACK_MINSIZE];
static OS_TASKSTACK task2_stack[OS_STACK_MINSIZE];
/* keep it small to allow 8bit processor to increment in few cycles */
static uint8_t counter[2] = { 0, 0 };

void test_idle(void)
{
   /* check if both task was run to the end */
   test_assert(TEST_CYCLES == counter[0]);
   test_assert(TEST_CYCLES == counter[1]);

   test_result(0);
}

int task_proc(void* param)
{
   unsigned idx = (unsigned)(size_t)param;

   while((counter[idx]) < TEST_CYCLES) {
      (counter[idx])++;
   }
   /* check that second task was scheduled at least once while this task is
    * still in working state, this will confirm that both tasks share the
    * priority */
   test_assert(0 != counter[(idx + 1) % 2]);

   return 0;
}

void test_init(void)
{
   /* 1ns tick should force almost flood of tick ISR on any arch */
   /* but using 1ms for debuging */
   test_setuptick(NULL, 1000000);

   os_task_create(&task1, 1, task1_stack, sizeof(task1_stack), task_proc, (void*)0);
   os_task_create(&task2, 1, task2_stack, sizeof(task2_stack), task_proc, (void*)1);
}

int main(void)
{
   test_setupmain("Test3");
   os_start(test_init, test_idle);
   return 0;
}

/** /} */

