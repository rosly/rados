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

/**
 * /file Test OS port (step 3)
 * /ingroup tests
 *
 * This is third of basic test to check the port.
 * Test checks if preemption is working by starting two CPU intensive tasks
 * which don't call any of OS functions. From time to time os_tick() procedure
 * should kick the preemption and switch between tasks. If test_setuptick() is
 * implemented correctly to call os_tick(), tasks should  be forced to give up
 * the CPU to the other task. When both task will finish, idle task will be
 * scheduled() and check the results.
 *
 * Test in following services are implemented correctly:
 * - test is arch_context_switch fully working (from preemptive point of view,
 *   os_schedule() will be called at each os_tick)
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
static volatile uint8_t counter[2] = { 0, 0 };

void test_idle(void)
{
   /* check if both task was run to the end */
   test_assert(TEST_CYCLES == counter[0]);
   test_assert(TEST_CYCLES == counter[1]);

   test_result(0);
}

/* this task will progres only if task2 will keep up with task1 */
int task1_proc(void* OS_UNUSED(param))
{
   while ((counter[0]) < TEST_CYCLES)
   {
      if (counter[0] != counter[1])
      {
         os_yield();
      }
      (counter[0])++;
   }

   return 0;
}

/* this task will progress only if it is behind task1 but it will not yield the
 * processor. So we make sure that only preemption can switch the context back to
 * task1. There fore we test if preemption from os_tick() is working */
int task2_proc(void* OS_UNUSED(param))
{
   while ((counter[1]) < TEST_CYCLES)
   {
      if (counter[1] < counter[0])
      {
         (counter[1])++;
      }
   }

   return 0;
}
void test_init(void)
{
   /* it would be better to use 1ns tick since it can force "tick ISR flood" on any arch
    * but using 1ms for easier debugging */
   test_setuptick(NULL, 1000000);

   os_task_create(&task1, 1, task1_stack, sizeof(task1_stack), task1_proc, NULL);
   os_task_create(&task2, 1, task2_stack, sizeof(task2_stack), task2_proc, NULL);
}

int main(void)
{
   test_setupmain("Test3");
   os_start(test_init, test_idle);
   return 0;
}

/** /} */

