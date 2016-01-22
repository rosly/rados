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
 * /file Test os timer rutines
 * /ingroup tests
 *
 * /{
 */

#include "os.h"
#include "os_test.h"

#define TEST_TIMER_NBR ((size_t)256)

static os_task_t task_main;
static OS_TASKSTACK task_main_stack[OS_STACK_MINSIZE];
static os_timer_t timers[TEST_TIMER_NBR];
static bool timer_clbck[TEST_TIMER_NBR];

void test_idle(void)
{
   /* nothing to do */
}

static void timer_proc(void *param)
{
   size_t i = (size_t)param;

   test_assert(false == timer_clbck[i]); /* calback cannot be caled twice, this
                                          * will mean bug */
   timer_clbck[i] = true;
}

/**
 * Test1 task procedure
 * Check if timers expires at proper timeout, we test that by creating many
 * timers with differrent timeouts
 */
int task_test1_proc(void *OS_UNUSED(param))
{
   size_t i, j;

   for (i = 0; i < TEST_TIMER_NBR; i++)
      os_timer_create(&timers[i], timer_proc, (void*)i, i + 1, 0);

   /* now each call of test_reqtick (which after tick procedure calls os_tick)
    * should generate single, specific timer callback */
   for (i = 0; i < TEST_TIMER_NBR; i++) {
      /* clean the clbck mark table */
      memset(timer_clbck, 0, sizeof(timer_clbck));

      /* generate the os_tick */
      test_reqtick();

      /* check if only one paticular (indexed by i) timer was called drring this
       * cycle */
      for (j = 0; j < TEST_TIMER_NBR; j++)
         test_assert(((i == j) ? true : false) == timer_clbck[j]);
   }

   for (i = 0; i < TEST_TIMER_NBR; i++)
      os_timer_destroy(&timers[i]);

   test_debug("subtest 1 OK");
   return 0;
}

/**
 * Test1 task procedure
 * Regresion test for itimer unsynch algorithm feature
 */
int task_test1a_proc(void *OS_UNUSED(param))
{
   /* clean the clbck mark table */
   memset(timer_clbck, 0, sizeof(timer_clbck));

   /* create timer with 2 tick timeout */
   os_timer_create(&timers[0], timer_proc, (void*)0, 2, 0);

   /* generate 1 tick */
   test_reqtick();

   /* create timer with 2 tick timeout */
   os_timer_create(&timers[1], timer_proc, (void*)1, 2, 0);

   /* generate 1 tick, timer[0] should expire but not timer[1] */
   test_reqtick();
   test_assert(true == timer_clbck[0]);
   test_assert(false == timer_clbck[1]); /* here was a BUG!! */

   /* generate 1 tick, timer[1] should expire */
   test_reqtick();
   test_assert(true == timer_clbck[1]);

   /* clean up */
   os_timer_destroy(&timers[0]);
   os_timer_destroy(&timers[1]);

   test_debug("subtest 1a OK");
   return 0;
}

/**
 * Test1 task procedure
 * Check if unsynch implementation is able to handle loong timeout periods
 */
int task_test1b_proc(void *OS_UNUSED(param))
{
   size_t i;

   /* clean the clbck mark table */
   memset(timer_clbck, 0, sizeof(timer_clbck));

   /* create timer with INT16_MAX tick timeout */
   os_timer_create(&timers[0], timer_proc, (void*)0, INT16_MAX, 0);

   /* generate INT16_MAX -1  ticks */
   for (i = 0; i < INT16_MAX - 1; i++)
      test_reqtick();

   /* check that this timer did not expired */
   test_assert(false == timer_clbck[0]);

   /* generate 1 additional tick, timer[0] should expire */
   test_reqtick();
   test_assert(true == timer_clbck[0]);

   /* clean up */
   os_timer_destroy(&timers[0]);

   test_debug("subtest 1b OK");
   return 0;
}

/**
 * Test1 task procedure
 * Test for timer unsynch algorithm feature during destroy
 */
int task_test1c_proc(void *OS_UNUSED(param))
{
   /* clean the clbck mark table */
   memset(timer_clbck, 0, sizeof(timer_clbck));

   /* create timer with 2 tick timeout */
   os_timer_create(&timers[0], timer_proc, (void*)0, 2, 0);

   /* generate 1 tick */
   test_reqtick();

   /* create timer with 2 tick timeout */
   os_timer_create(&timers[1], timer_proc, (void*)1, 2, 0);

   /* destroy timer[0] so unsynch should be handled properly in order to
    * represend valid value aginst timer[1] */
   os_timer_destroy(&timers[0]);

   /* generate 1 tick, timer[1] should not expire */
   test_reqtick();
   test_assert(false == timer_clbck[1]);

   /* generate 1 tick, timer[1] should now expire */
   test_reqtick();
   test_assert(true == timer_clbck[1]);

   /* clean up */
   os_timer_destroy(&timers[1]);

   test_debug("subtest 1c OK");
   return 0;
}

/**
 * Test2 task procedure
 * Check if timers are properly reloaded in defined periods
 * We test that by creating many timers and using both timeout and period, then
 * we check if timer expires and it is reloaded at apropriate timestamps
 */
int task_test2_proc(void *OS_UNUSED(param))
{
   size_t i, j;

   /* create autoreaload timers in i periods */
   for (i = 1; i < TEST_TIMER_NBR; i++)
      os_timer_create(&timers[i], timer_proc, (void*)i, i, i);

   /* now each call of test_reqtick (which after tick procedure calls os_tick)
    * should generate specific timer callbacks (may be more that one since we
    * use autoreload with period equal to timer index) */
   for (i = 1; i < TEST_TIMER_NBR * 100; i++) {
      /* clean the clbck mark table */
      memset(timer_clbck, 0, sizeof(timer_clbck));

      /* generate the os_tick */
      test_reqtick();

      /* verify the clbck mark table - each timer have to be called in its
       * period */
      for (j = 1; j < TEST_TIMER_NBR; j++)
         test_assert(((0 == (i % j)) ? true : false) == timer_clbck[j]);
   }

   for (i = 0; i < TEST_TIMER_NBR; i++)
      os_timer_destroy(&timers[i]);

   test_debug("subtest 2 OK");
   return 0;
}

int task_main_proc(void *OS_UNUSED(param))
{
   task_test1_proc(NULL);
   task_test1a_proc(NULL);
   task_test1b_proc(NULL);
   task_test1c_proc(NULL);
   task_test2_proc(NULL);

   test_result(0);
   return 0;
}

void test_init(void)
{
   /* for testing we will need just one task */
   os_task_create(
      &task_main, OS_CONFIG_PRIOCNT - 1,
      task_main_stack, sizeof(task_main_stack),
      task_main_proc, NULL);
}

int main(void)
{
   os_init();
   test_setupmain("Test_Timer");
   test_init();
   os_start(test_idle);

   return 0;
}

/** /} */

