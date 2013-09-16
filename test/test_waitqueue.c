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
 * /file Test os semaphore rutines
 * /ingroup tests
 *
 * /{
 */

#include <os.h>
#include <os_test.h>

#define min(_x, _y) ({ \
   typeof(_x) _min1 = (_x); \
   typeof(_y) _min2 = (_y); \
   (void) (&_min1 == &_min2); \
   _min1 < _min2 ? _min1 : _min2; })

#define TEST_TASKS ((unsigned)10)

typedef struct {
   os_task_t task;
   long int task1_stack[OS_STACK_MINSIZE];
   unsigned idx;
   os_waitqueue_t wait_queue;
   volatile int a;
   volatile int b;
   volatile int c;
   volatile int d;
} task_data_t;

static os_task_t task_main;
static long int task_main_stack[OS_STACK_MINSIZE];
static task_data_t worker_tasks[TEST_TASKS];
static os_waitqueue_t global_wait_queue;

void idle(void)
{
   /* nothing to do */
}

/**
 * Simple test procedure for typicall waitloop usage.
 */
int test1_task_proc(void* param)
{
   int ret;
   task_data_t *data = (task_data_t*)param;

  /* we have two loops
   * internal one simulates waiting for external event and on that loop we focus
   * external one is used only for making multiple test until we decide that
   * thread should be joined, therefore os_waitqueue_prepare() is inside the
   * outern loop */

   while(0 == data->a) {
      data->b = 0;
      os_waitqueue_prepare(&(data->wait_queue), OS_TIMEOUT_INFINITE);
      while(0 == data->b) {
         /* signalize that we performed condition test */
         (data->c)++;
         /* go to sleep */
         ret = os_waitqueue_wait();
         test_assert(0 == ret);
      }
      (data->d)++;
   }

   return 0;
}

/**
 * Simple test case, when multiple tasks is waiting for the same waitqueue but
 * they have different success conditions. Simulation fo rthe same success
 * condition may be done by setting success for more thatn one task
 */
int testcase_1(void)
{
   int ret;
   unsigned i;

   /* clear out memory */
   memset(worker_tasks, 0, sizeof(worker_tasks));

   /* initialize variables */
   os_waitqueue_create(&global_wait_queue);
   for(i = 0; i < TEST_TASKS; i++) {
      worker_tasks[i].idx = i + 1;
   }

   /* create tasks and perform tests */
   for(i = 0; i < TEST_TASKS; i++) {
      worker_tasks[i].idx = i + 1;
      os_task_create(
         &(worker_tasks[i].task), min(i + 1, OS_CONFIG_PRIOCNT - 1),
         worker_tasks[i].task1_stack, sizeof(worker_tasks[i].task1_stack),
         test1_task_proc, &(worker_tasks[i]));
   }

   /* we thread this (main) thread as IRQ and HW
    * so we will perform all actions here in busy loop to simulate the
    * uncontroled enviroment. Since this thread has highest priority only
    * preemption is able to force this task to sleep */
   /* wait until all threads will be prepared for sleep */
   for(i = 0; i < TEST_TASKS; i++) {
      if(0 == worker_tasks[i].c) {
         i = -1; /* start check loop from begining */
      } else {
         test_debug("Thread %u marked", i);
      }
   }

   /* join tasks and collect the results */
   ret = 0;
   for(i = 0; i < TEST_TASKS; i++) {
      ret = os_task_join(&(worker_tasks[i].task));
      test_assert(0 == ret);
   }
   
   /* destroy the waitqueue */
   os_waitqueue_destroy(&global_wait_queue);

   return 0;
}

/**
 * The main task for tests manage
 */
int task_main_proc(void* OS_UNUSED(param))
{
   int ret;

   do
   {
      ret = testcase_1();
      if(ret) {
         test_debug("Testcase 1 failure");
         break;
      }
   } while(0);

   test_result(ret);
   return 0;
}

void init(void)
{
   test_setuptick(NULL, 300000000);

   os_task_create(
      &task_main, OS_CONFIG_PRIOCNT - 1,
      task_main_stack, sizeof(task_main_stack),
      task_main_proc, NULL);
}

int main(void)
{
   test_setupmain("Test_Waitqueue");
   os_start(init, idle);
   return 0;
}

/** /} */

