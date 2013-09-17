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
static os_sem_t global_sem;

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

   test_debug("Task %u started", data->idx);

   while(0 == data->a) {
      data->b = 0;
      while(1)
      {
         os_waitqueue_prepare(&global_wait_queue, OS_TIMEOUT_INFINITE);
         test_debug("Task %u spins ...", data->idx);
         ++(data->c); /* signalize that we performed condition test */
         if(0 != data->b) {
            break;
         }
         ret = os_waitqueue_wait(); /* condition not meet, go to sleep */
         test_assert(OS_OK == ret);
      }
      test_debug("Task %u found condition meet", data->idx);
      (data->d)++;
   }
   test_debug("Task %u exiting", data->idx);

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
   os_retcode_t retcode;
   unsigned i;

   /* clear out memory */
   memset(worker_tasks, 0, sizeof(worker_tasks));

   /* initialize variables */
   os_waitqueue_create(&global_wait_queue);
   os_sem_create(&global_sem, 0);
   for(i = 0; i < TEST_TASKS; i++) {
      worker_tasks[i].idx = i + 1;
   }

   /* create tasks and perform tests */
   for(i = 0; i < TEST_TASKS; i++) {
      worker_tasks[i].idx = i + 1;
      os_task_create(
         &(worker_tasks[i].task), 9 == i ? 2 : 1, /* task 10 will have priority 2
                                                     all other will have priority 1 */
         worker_tasks[i].task1_stack, sizeof(worker_tasks[i].task1_stack),
         test1_task_proc, &(worker_tasks[i]));
   }

   /* we thread this (main) thread as IRQ and HW
    * so we will perform all actions here in busy loop to simulate the
    * uncontroled enviroment. Since this thread has highest priority even
    * preemption is not able to force this task to sleep */

   /* wait until all threads will be prepared for sleep */
   for(i = 0; i < TEST_TASKS; i++) {
      test_debug("Spin count for task %u is %u", i, worker_tasks[i].c);
      if(0 == worker_tasks[i].c) {
         i = -1; /* start check loop from begining */
         /* give time for test threads to run, we use this sem only for timeout,
          * nobody will rise this sem */
         retcode = os_sem_down(&global_sem, 10); /* 10 ticks of sleep */
         test_assert(OS_TIMEOUT == retcode);
      }
   }

   /* all threads had been started, they should now sleep on waitqueue and wait
    * for signal, after signal they will spin and check the condition
    * wake up all tasks and check if they had spinned around condition check */
   test_debug("Main task signalizes all slaves");
   os_waitqueue_wakeup(&global_wait_queue, TEST_TASKS);
   /* give time for test threads to run (main is most prioritized, eat all CPU */
   retcode = os_sem_down(&global_sem, 10); /* 10 ticks of sleep */
   test_assert(OS_TIMEOUT == retcode);
   /* verify that all threads have spinned */
   for(i = 0; i < TEST_TASKS; i++) {
      test_debug("Spin count for task %u is %u", i, worker_tasks[i].c);
      test_assert(2 == worker_tasks[i].c);
   }

   /* task 0 has highest ptiority from slaves, it we notify only once this
    * should be the task which will woke up */
   test_debug("Main task signalizes single slave");
   os_waitqueue_wakeup(&global_wait_queue, 1);
   /* give time for test threads to run (main is most prioritized, eat all CPU */
   retcode = os_sem_down(&global_sem, 10); /* 10 ticks of sleep */
   test_assert(OS_TIMEOUT == retcode);
   /* verify that only thread 10 spinned */
   for(i = 0; i < TEST_TASKS; i++) {
      test_debug("Spin count for task %u is %u", i, worker_tasks[i].c);
      test_assert((9 == i ? 3 : 2) == worker_tasks[i].c);
   }

   /* \TODO check the timeout feature */

   /* join tasks and collect the results release the tasks from loop */
   test_debug("Main task joining slaves");
   for(i = 0; i < TEST_TASKS; i++) {
      worker_tasks[i].a = 1;
      worker_tasks[i].b = 1;
   }
   /* signalize all tasks */
   os_waitqueue_wakeup(&global_wait_queue, OS_WAITQUEUE_ALL);
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

void test_tickprint(void)
{
  test_debug("Tick");
}

void init(void)
{
   test_setuptick(test_tickprint, 50000000);

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

