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
 * /file Test os message queue routines
 * /ingroup tests
 *
 * /{
 */

#include "os.h"
#include "os_test.h"

#include <inttypes.h>

#define TEST_MQUEUE_SIZE ((arch_ridx_t)512)
#define TEST_POST_SIZE   ((arch_ridx_t)128)
#define TEST_STRESS_SIZE ((arch_ridx_t)32)

#define TEST_PRIO_LOW  (1)
#define TEST_PRIO_MED  (2)
#define TEST_PRIO_HIGH (3)
#define TEST_PRIO_CORD (OS_CONFIG_PRIOCNT - 1)
OS_STATIC_ASSERT(TEST_PRIO_CORD > TEST_PRIO_HIGH);

typedef struct {
   bool  isr;
   bool  post_first;
   arch_ridx_t cnt;
} test_postman_param_t;

static os_task_t task_worker[4];
static OS_TASKSTACK task_stack[4][OS_STACK_MINSIZE];
static os_task_t task_coordinator;
static OS_TASKSTACK coordinator_stack[OS_STACK_MINSIZE];

static os_mqueue_t test_mqueue;
static void* test_mqueue_buff[TEST_MQUEUE_SIZE];
static test_postman_param_t *isr_param;

void test_postman(test_postman_param_t *param)
{
   uint_fast8_t i;
   arch_ridx_t ret;
   void* obj[param->cnt];

   for (i = 0; i < param->cnt; i++) {
      obj[i] = (void*)(uintptr_t)i;
   }

   ret = os_mqueue_post(&test_mqueue, obj, param->cnt, OS_NOSYNC);
   test_assert(ret = param->cnt);
}

void test_receiver(test_postman_param_t *param)
{
   uint_fast8_t i;
   os_retcode_t ret;
   arch_ridx_t cnt;
   void* obj[param->cnt];

   cnt = param->cnt;
   ret = os_mqueue_pop(&test_mqueue, obj,
                       &cnt, OS_TIMEOUT_INFINITE);
   test_assert(OS_OK == ret);
   test_assert(cnt == param->cnt);

   for (i = 0; i < param->cnt; i++) {
      test_assert(obj[i] == (void*)(uintptr_t)i);
   }
}

void test_idle(void)
{
   /* nothing to do */
}

void test_manual_tick(void)
{
   test_postman(isr_param);
}

void test_frequent_tick(void)
{
   test_postman(isr_param);
}

void test_empty_tick(void)
{
}

/* this function post a msg either as a thread or by isr
 * it is important that thread which execute this function had proper prio so
 * required scheduling sequence (and test sequence) can be accomplished */
int test_post_task(void *param)
{
   test_postman_param_t *post_param = param;

   if (!post_param->isr) {
      test_postman(post_param);
   } else {
      isr_param = post_param;
      test_reqtick();
   }

   return 0;
}

int test_pop_task(void *param)
{
   test_postman_param_t *post_param = param;

   test_receiver(post_param);

   return 0;
}

void test_scen1(
   bool isr,
   bool post_first)
{
   test_postman_param_t postman_param = {
      .isr = isr,
      .post_first = post_first,
      .cnt = TEST_POST_SIZE };
   uintptr_t i;

   os_mqueue_create(&test_mqueue, test_mqueue_buff,
                    TEST_MQUEUE_SIZE, OS_MQUEUE_MPMQ);

   /* prepare tick callback but all ticks will be manually requested */
   test_setuptick(test_manual_tick, 0);

   /* depending on priority the task will be scheduled as firs or second but not
    * earlier when current task call os_task_join() */
   os_task_create(
      &task_worker[0], post_first ? TEST_PRIO_MED : TEST_PRIO_LOW,
      task_stack[0], sizeof(task_stack[0]),
      test_post_task, &postman_param);
   os_task_create(
      &task_worker[1], post_first ? TEST_PRIO_LOW : TEST_PRIO_MED,
      task_stack[1], sizeof(task_stack[1]),
      test_pop_task, &postman_param);

   /* scheduler will kick in and allow first task to run after following call */
   for (i = 0; i < 2; i++)
      os_task_join(&task_worker[i]);

   os_mqueue_destroy(&test_mqueue);
}

int test_stress_task(void *param)
{
   uintptr_t thri = (uintptr_t)param;
   uint_fast16_t sum = 0;
   arch_ridx_t cnt;
   arch_ridx_t cnt_ret;
   os_retcode_t ret;
   void* obj[TEST_STRESS_SIZE];

   do {
      cnt = TEST_STRESS_SIZE;
      ret = os_mqueue_pop(&test_mqueue, obj,
                          &cnt, OS_TIMEOUT_INFINITE);
      test_assert(OS_OK == ret);

      cnt_ret = os_mqueue_post(&test_mqueue, obj, cnt / 2, OS_NOSYNC);
      test_assert(cnt_ret == cnt / 2);
      os_yield();
      cnt_ret = os_mqueue_post(&test_mqueue, &obj[cnt / 2],
                               cnt - (cnt / 2), OS_NOSYNC);
      test_assert(cnt_ret == (cnt - (cnt / 2)));

      sum += cnt;
   } while (sum < 10);

   return (int)thri;
}

void test_stress(void)
{
   uint_fast8_t i;

   os_mqueue_create(&test_mqueue, test_mqueue_buff,
                    TEST_MQUEUE_SIZE, OS_MQUEUE_MPMQ);

   /* initialize mqueue with some objects
    * task will shuffle those, then we will verify if none of them was lost
    * this will verify that algorithm has no race conditions */
   do {
      arch_ridx_t ret;
      void* obj[TEST_POST_SIZE];

      for (i = 0; i < TEST_POST_SIZE; i++) {
         obj[i] = (void*)(uintptr_t)i;
      }

      ret = os_mqueue_post(&test_mqueue, obj, TEST_POST_SIZE, OS_NOSYNC);
      test_assert(ret = TEST_POST_SIZE);
   } while(0);

   /* create frequent tick to maximize the possibility of race conditions */
   test_setuptick(test_empty_tick, 1);

   for (i = 0; i < 4; i++) {
      os_task_create(
         &task_worker[i], TEST_PRIO_LOW,
         task_stack[i], sizeof(task_stack[i]),
         test_stress_task, (void*)(uintptr_t)i);
   }

   /* scheduler will kick in and allow first task to run after following call */
   for (i = 0; i < 4; i++)
      os_task_join(&task_worker[i]);


   do {
      arch_ridx_t cnt;
      arch_ridx_t ret;
      uint8_t checks[TEST_POST_SIZE] = { 0 };
      void* obj[TEST_POST_SIZE];

      cnt = TEST_POST_SIZE;
      ret = os_mqueue_pop(&test_mqueue, obj,
                          &cnt, OS_TIMEOUT_INFINITE);
      test_assert(OS_OK == ret);
      test_assert(cnt == TEST_POST_SIZE);

      for (i = 0; i < TEST_POST_SIZE; i++) {
         test_debug("msq %"PRIu16, (uint16_t)(uintptr_t)obj[i]);
      }
      for (i = 0; i < TEST_POST_SIZE; i++) {
         test_assert(checks[(uint_fast16_t)(uintptr_t)(obj[i])] == 0);
         checks[(uint_fast16_t)(uintptr_t)(obj[i])] = 1;
      }
   } while (0);

   os_mqueue_destroy(&test_mqueue);
}

/**
 * Test coordinator, runs all test in unit
 */
int test_coordinator(void *OS_UNUSED(param))
{
   uint_least8_t i;

   /* testing post/push - pop in different combinations task/isr with pop before
    * push and vice versa */
   for (i = 0; i < 4; i++) {
      test_scen1(i & 1, i & 2);
      test_debug("os_mqueue_%s() from %s os_mqueue_pop() OK",
                 (i & 1) ? "ISR" : "Task",
                 (i & 2) ? "before" : "after");

   }


   test_stress();

   test_result(0);
   return 0;
}

void test_init()
{
   /* coordination task has the most high prio */
   os_task_create(
      &task_coordinator, TEST_PRIO_CORD,
      coordinator_stack, sizeof(coordinator_stack),
      test_coordinator, NULL);
}

int main(void)
{
   os_init();
   test_setupmain("Test_Mbox");
   test_init();
   os_start(test_idle);

   return 0;
}

/** /} */

