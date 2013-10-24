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
 * /file Test os OS port (step 4)
 * /ingroup tests
 *
 * This is forth of basic test to check the port.
 * Two tasks and two semaphores. Both tasks will block on semaphores. The
 * semaphores should be signalized by timer ISR, by driving the timer freq all
 * tree types of switching may be tested, intresting case is when timer is
 * generated eah CPU cycle (can be used to test the critical sections)
 *
 * Test if following services are implemented corecly:
 * - test is arch_context_switch fully working (from preemptive point of view)
 * (will be called at each os_tick)
 * /{
 */

#include "os.h"
#include "os_test.h"

#define TEST_CYCLES ((unsigned)50)

typedef struct {
   os_sem_t sem;
   unsigned idx;
} task_data_t;

static os_task_t task1;
static os_task_t task2;
static OS_TASKSTACK task1_stack[OS_STACK_MINSIZE];
static OS_TASKSTACK task2_stack[OS_STACK_MINSIZE];
static task_data_t task_data[2];

void tick_clbck(void)
{
   os_sem_up(&(task_data[0].sem));
   os_sem_up(&(task_data[1].sem));
}

void test_idle(void)
{
   if((task_data[0].idx < TEST_CYCLES) || (task_data[1].idx < TEST_CYCLES))
   {
     return; /* this is not the end, continue */
   }

   /* both task reach its ends, finalize test */
   test_result(0);
}

int task_proc(void* param)
{
   int ret;
   task_data_t *data = (task_data_t*)param;

   while(data->idx < TEST_CYCLES)
   {
      (data->idx)++;
      ret = os_sem_down(&(data->sem), OS_TIMEOUT_INFINITE);
      test_assert(OS_OK == ret);
   }

   return 0;
}

void test_init(void)
{
   memset(&task_data[0], 0, sizeof(task_data_t));
   memset(&task_data[1], 0, sizeof(task_data_t));
   os_sem_create(&(task_data[0].sem), 0);
   os_sem_create(&(task_data[1].sem), 0);
   os_task_create(&task1, 1, task1_stack, sizeof(task1_stack), task_proc, &task_data[0]);
   os_task_create(&task2, 1, task2_stack, sizeof(task2_stack), task_proc, &task_data[1]);

   /* frequent ticks help test race conditions.  The best would be to call tick
    * ISR every instruction, 1ns tick should force almost flood of tick ISR on
    * any arch */
   /* but using 1ms for debuging */
   test_setuptick(tick_clbck, 1000000);
}

int main(void)
{
   test_setupmain("Test4");
   os_start(test_init, test_idle);
   return 0;
}

/** /} */

