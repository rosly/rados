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
 * /file Test os timer rutines
 * /ingroup tests
 *
 * /{
 */

#include <stdlib.h>
#include <time.h>

#include <os.h>
#include <os_test.h>

#define TEST_TIMER_NBR ((size_t)512)

static os_task_t task_main;
static long int task_main_stack[OS_STACK_MINSIZE];
static os_task_t task_worker;
static long int task_worker_stack[OS_STACK_MINSIZE];
static os_timer_t timers[TEST_TIMER_NBR];
static bool timer_clbck[TEST_TIMER_NBR];

void idle(void)
{
   /* nothing to do */
}

static void timer_proc(void* param)
{
   size_t i = (size_t)param;

   test_assert(false == timer_clbck[i]); /* calback cannot be caled twice, this will mean bug */
   timer_clbck[i] = true;
}

static void timer_proc2(void* OS_UNUSED(param))
{
   /* nothing to do */
}

/**
 *  Test1 task procedure
 */
int task_test1_proc(void* OS_UNUSED(param))
{
   size_t i, j;

   for(i = 0; i < TEST_TIMER_NBR; i++) {
      os_timer_create(&timers[i], timer_proc, (void*)i, i + 1, 0);
   }

   /* now each os_tick should generate the proper timer callbacks */
   for(i = 0; i < TEST_TIMER_NBR; i++) {
      /* clean the clbck mark table */
      memset(timer_clbck, 0, sizeof(timer_clbck));

      /* generate the os_tick */
      raise(SIGALRM);

      /* check if only one paticular timer was called drring the cycle */
      for(j = 0; j < TEST_TIMER_NBR; j++) {
         test_assert(((i == j) ? true : false) == timer_clbck[j]);
      }
   }

   for(i = 0; i < TEST_TIMER_NBR; i++) {
      os_timer_destroy(&timers[i]);
   }

   return 0;
}

/**
 *  Test2 task procedure
 */
int task_test2_proc(void* OS_UNUSED(param))
{
   size_t i, j;

   /* create autoreaload timers in i periods */
   for(i = 1; i < TEST_TIMER_NBR; i++) {
      os_timer_create(&timers[i], timer_proc, (void*)i, i, i);
   }

   /* now each os_tick should generate the proper timer callbacks */
   for(i = 1; i < TEST_TIMER_NBR * 100; i++) {
      /* clean the clbck mark table */
      memset(timer_clbck, 0, sizeof(timer_clbck));

      /* generate the os_tick */
      raise(SIGALRM);

      /* verify the clbck mark table - each timer have to be called in its period */
      for(j = 1; j < TEST_TIMER_NBR; j++) {
         test_assert(((0 == (i % j)) ? true : false) == timer_clbck[j]);
      }
   }

   for(i = 0; i < TEST_TIMER_NBR; i++) {
      os_timer_destroy(&timers[i]);
   }

   return 0;
}

/**
 *  Test3 task procedure
 */
int task_test3_proc(void* OS_UNUSED(param))
{
   size_t i;
   struct timespec timer_last;
   struct timespec timer_curr;
   unsigned int tmp;

   /* initialize */
   memset(&timer_last, 0, sizeof(timer_last));

   /* create autoreaload timers in i periods */
   srand(0); /* we use rand with the same init number for pseudo-random stream, this should gice us predictable test enviroment */
   for(i = 10; i < TEST_TIMER_NBR; i++) {
      tmp = (rand() % TEST_TIMER_NBR * 100) + 1;
      os_timer_create(&timers[i], timer_proc2, (void*)i, tmp, tmp);
   }

   /* now each os_tick should generate the proper timer callbacks */
   for(i = 1; i < TEST_TIMER_NBR * 100; i++) {
      /* generate the os_tick */
      raise(SIGALRM);

      if( 0 == (i % 1000) ) {
         (void)clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timer_curr);
         //if( timer_curr.tv_nsec  < timer_last.tv_nsec ) {
         //   timer_curr.tv_sec -= 1;
         //   timer_curr.tv_nsec += 1000000000;
        // }

         test_debug("%zu\t%ld\t%ld\n", i, timer_curr.tv_sec - timer_last.tv_sec, timer_curr.tv_nsec - timer_last.tv_nsec);
         timer_last = timer_curr;
      }
   }

   test_debug("finish...\n");
   for(i = 0; i < TEST_TIMER_NBR; i++) {
      os_timer_destroy(&timers[i]);
   }

   return 0;
}

int task_main_proc(void* OS_UNUSED(param))
{
   os_task_create(
      &task_worker, 1,
      task_worker_stack, sizeof(task_worker_stack),
      task_test1_proc, NULL);
   os_task_join(&task_worker);

   os_task_create(
      &task_worker, 1,
      task_worker_stack, sizeof(task_worker_stack),
      task_test2_proc, NULL);
   os_task_join(&task_worker);

   os_task_create(
      &task_worker, 1,
      task_worker_stack, sizeof(task_worker_stack),
      task_test3_proc, NULL);
   os_task_join(&task_worker);

   test_debug("Test timer: passed\n");
   test_result(0);
   return 0;
}

void init(void)
{
   os_task_create(
      &task_main, OS_CONFIG_PRIOCNT - 1,
      task_main_stack, sizeof(task_main_stack),
      task_main_proc, NULL);
}

int main(void)
{
   os_start(init, idle);
   return 0;
}

/** /} */

