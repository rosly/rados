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
 * /file Test os message box routines
 * /ingroup tests
 *
 * /{
 */

#include "os.h"
#include "os_test.h"

#define TEST_PRIO_LOW  (1)
#define TEST_PRIO_MED  (2)
#define TEST_PRIO_HIGH (3)
#define TEST_PRIO_CORD (OS_CONFIG_PRIOCNT - 1)
OS_STATIC_ASSERT(TEST_PRIO_CORD > TEST_PRIO_HIGH);

typedef struct {
   void* msg;
   bool  isr;
   bool  post_first;
   bool  push;
} test_postman_param_t;

static os_task_t task_worker[4];
static OS_TASKSTACK task_stack[4][OS_STACK_MINSIZE];
static os_task_t task_coordinator;
static OS_TASKSTACK coordinator_stack[OS_STACK_MINSIZE];

static os_mbox_t test_mbox;
static test_postman_param_t *isr_param;

void test_postman(test_postman_param_t *param)
{
   if (param->push) {
       void* prev_msg;

       /* we assume that mbox was empty */
       prev_msg = os_mbox_push(&test_mbox, param->msg, OS_NOSYNC);
       test_assert(prev_msg == NULL);
       /* in case post task has higher priority or we are in isr, than we should
        * be able to check if os_mbox_push() will return the same message which
        * we sent last time.
        * else, the recv thread should be scheduled and take out the msg before
        * we will be able to send msg second time */
          prev_msg = os_mbox_push(&test_mbox, param->msg, OS_NOSYNC);
          test_assert(
             prev_msg == ((param->post_first || param->isr) ?
                         param->msg : NULL));
   } else {
      os_retcode_t ret;

      ret = os_mbox_post(&test_mbox, param->msg, OS_NOSYNC);
      test_assert(ret == OS_OK);
   }
}

int test_receiver(void *param)
{
   os_retcode_t ret;
   void* msg;

   ret = os_mbox_pop(&test_mbox, &msg, OS_TIMEOUT_INFINITE);
   test_assert(OS_OK == ret);
   test_assert(param == msg);

   return 0;
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

void test_scen1(
   void* msg,
   bool isr,
   bool post_first,
   bool push)
{
   test_postman_param_t postman_param = {
      .msg = msg,
      .isr = isr,
      .post_first = post_first,
      .push = push };
   uint16_t i;

   os_mbox_create(&test_mbox, NULL);

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
      test_receiver, msg);

   /* scheduler will kick in and allow first task to run after following call */
   for (i = 0; i < 2; i++)
      os_task_join(&task_worker[i]);

   os_mbox_destroy(&test_mbox);
}

void test_stress(void)
{
   test_postman_param_t postman_param = {
      .msg = (void*)100,
      .isr = true,
      .post_first = false,
      .push = true };
   isr_param = &postman_param;
   uint16_t i;

   os_mbox_create(&test_mbox, NULL);

   /* for stress test we will randomly push messages from frequent ISR */
   /* \TODO This test does not really do what I wanted. The intention was that
    * tick ISR will execute at each CPU instruction, but in reality this has to
    * be achieved by some other mechanism such (x86 is very fast and we need to
    * use some single stepping mechanism) */
   test_setuptick(test_frequent_tick, 1);

   /* receive messages until high we reach number of interrupts == message posts */
   for (i = 0; i < 512; i++) {
      test_receiver((void*)100);
      test_verbose_debug("Msg %u received", i);
   }

   os_mbox_destroy(&test_mbox);
}

/**
 * Test coordinator, runs all test in unit
 */
int test_coordinator(void *OS_UNUSED(param))
{
   uint8_t i;

   /* testing post/push - pop in different combinations task/isr with pop before
    * push and vice versa */
   for (i = 0; i < 8; i++) {
      test_scen1((void*)(uintptr_t)(i + 1), i & 1, i & 2, i & 4);
      test_debug("os_mbox_%s() from %s %s os_mbox_pop() OK",
                 (i & 4) ? "push" : "post",
                 (i & 1) ? "ISR" : "Task",
                 (i & 2) ? "before" : "after");

   }

   /* next scenario the stress test from isr */
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

