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
 * Two task with endles loop, add timer ISR and call os_tick inside, task should switch during timer ISR
 * Test in following services are implemented corecly:
 * - test is arch_context_switch fully working (from preemptive point of view) (will be called at each os_tick)
 * /{
 */

#include "os.h"
#include <os_test.h>

#define TEST_CYCLES ((os_atomic_t)1000000)

static os_task_t task1;
static os_task_t task2;
static long int task1_stack[OS_STACK_MINSIZE];
static long int task2_stack[OS_STACK_MINSIZE];
static os_atomic_t task1_idx = 0;
static os_atomic_t task2_idx = 0;

void idle(void)
{
   test_assert(TEST_CYCLES == task1_idx);
   test_assert(TEST_CYCLES == task2_idx);
   test_debug("Test3: passed\n");
   test_result(0);
}

int task_proc(void* param)
{
   os_atomic_t *idx = (os_atomic_t*)param;

   while((*idx) < TEST_CYCLES) {
      (*idx)++;
   }

   return 0;
}

void init(void)
{
   test_setuptick(NULL, 1);

   os_task_create(&task1, 1, task1_stack, sizeof(task1_stack), task_proc, &task1_idx);
   os_task_create(&task2, 1, task2_stack, sizeof(task2_stack), task_proc, &task2_idx);
}

int main(void)
{
   test_setupmain();
   os_start(init, idle);
   return 0;
}

/** /} */

