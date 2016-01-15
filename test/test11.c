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

/**
 * /file Test OS port (step 1.1)
 * /ingroup tests
 *
 * This test check the context switch implementation in more detail. By yielding
 * of CPU to task with the same priority we can verify that context is properly
 * stored and restored while context switch. When both task ends, the idle task
 * will be scheduled for additional result verification.
 *
 * Test in following services are implemented correctly:
 * - test is arch_context_switch fully working
 * /{
 */

#include "os.h"
#include "os_test.h"

#define TEST_CYCLES ((unsigned)100)

static os_task_t task1;
static os_task_t task2;
static OS_TASKSTACK task1_stack[OS_STACK_MINSIZE];
static OS_TASKSTACK task2_stack[OS_STACK_MINSIZE];
static volatile uint8_t cnt1, cnt2;

void test_idle(void)
{
   /* check if both task was run to the end */
   test_assert(TEST_CYCLES == cnt1);
   test_assert(TEST_CYCLES == cnt2);

   test_result(0);
}

int task1_proc(void *OS_UNUSED(param))
{
   while ((cnt1) < TEST_CYCLES) {
      (cnt1)++;
      os_yield();
      test_assert(cnt1 == cnt2);
   }

   return 0;
}

int task2_proc(void *OS_UNUSED(param))
{
   while ((cnt2) < TEST_CYCLES) {
      (cnt2)++;
      os_yield();
   }

   return 0;
}

void test_init(void)
{
   os_task_create(&task1, 1, task1_stack, sizeof(task1_stack), task1_proc, NULL);
   os_task_create(&task2, 1, task2_stack, sizeof(task2_stack), task2_proc, NULL);
}

int main(void)
{
   test_setupmain(OS_PROGMEM_STR("Test1.1"));
   os_start(test_init, test_idle);
   return 0;
}

/** /} */

