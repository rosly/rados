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
 * /file Test os OS port (step 4)
 * /ingroup tests
 *
 * This is forth of basic test to check the port.
 * Two tasks and two waitqueues. Both tasks will block on waitqueues. The
 * waitqueues should be signalized by timer ISR, by driving the timer freq all
 * tree types of switching may be tested, interesting case is when timer is
 * generated each CPU cycle (can be used to test the critical sections)
 *
 * Test if following services are implemented correctly:
 * - test is arch_context_switch fully working (from preemptive point of view,
 *   os_schedule() will be called at each os_tick)
 * /{
 */

#include "os.h"
#include "os_test.h"

#define TEST_CYCLES ((unsigned)100)
#define TEST_TASK_CNT ((uint8_t)3)

typedef struct {
   os_task_t task;
   OS_TASKSTACK stack[OS_STACK_MINSIZE];
   volatile unsigned cnt;
} task_def_t;

static task_def_t task_def[TEST_TASK_CNT];
os_waitqueue_t wq;
volatile unsigned glob_cnt;

void tick_clbck(void)
{
   os_waitqueue_wakeup(&wq, OS_WAITQUEUE_ALL);
}

void test_idle(void)
{
   uint8_t i;

   for (i = 0; i < TEST_TASK_CNT; i++)
   {
      if(task_def[i].cnt < TEST_CYCLES)
      {
         return; /* this is not the end, continue */
      }
   }
   /* if waitqueue critical section properly prevented from race conditions than ... */
   test_assert(glob_cnt == (TEST_TASK_CNT * TEST_CYCLES));

   /* both task reach its ends, finalize test */
   test_result(0);
}

int task_proc(void* param)
{
   int ret;
   volatile unsigned *cnt = (volatile unsigned*)param;

   while(*cnt < TEST_CYCLES)
   {
      os_waitqueue_prepare(&wq);
      /* only ticks that trigger exacly here will wakeup the task */
      glob_cnt++;
      ret = os_waitqueue_wait(OS_TIMEOUT_INFINITE);
      test_assert(OS_OK == ret);
      (*cnt)++;
   }

   return 0;
}

void test_init(void)
{
   uint8_t i;

   os_waitqueue_create(&wq);
   for (i = 0; i < TEST_TASK_CNT; i++)
   {
      os_task_create(&(task_def[i].task), 1, task_def[i].stack,
                     sizeof(task_def[i].stack),
                     task_proc, (void*)(&(task_def[i].cnt)));
   }

   /* frequent ticks help test race conditions.  The best would be to call tick
    * ISR every instruction, 1ms tick should be optimal */
   test_setuptick(tick_clbck, 1000000);
}

int main(void)
{
   test_setupmain(OS_PROGMEM_STR("Test4"));
   os_start(test_init, test_idle);
   return 0;
}

/** /} */

