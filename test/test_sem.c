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
 * /file Test os semaphore rutines
 * /ingroup tests
 *
 * /{
 */

#include "os.h"
#include "os_test.h"

#define min(_x, _y) ({ \
   typeof(_x) _min1 = (_x); \
   typeof(_y) _min2 = (_y); \
   (void) (&_min1 == &_min2); \
   _min1 < _min2 ? _min1 : _min2; })

#define TEST_TASKS ((unsigned)10)
#define TEST_CYCLES ((os_atomic_t)1000)

typedef struct {
   os_task_t task;
   os_sem_t sem;
   OS_TASKSTACK task1_stack[OS_STACK_MINSIZE];
   unsigned idx;
   bool result;
} task_data_t;

static os_task_t task_main;
static OS_TASKSTACK task_main_stack[OS_STACK_MINSIZE];
static task_data_t worker_tasks[TEST_TASKS];

void idle(void)
{
   /* nothing to do */
}

/**
 * test procedure for os_sem_down with timeout
 */
int task_proc(void* param)
{
   int ret;
   task_data_t *data = (task_data_t*)param;

   data->result = false; /* reseting the test result */

   /* checking if sem have 0 */
   ret = os_sem_down(&(data->sem), OS_TIMEOUT_TRY);
   test_assert(OS_WOULDBLOCK == ret);
   /* sleeping for task assigned time */
   ret = os_sem_down(&(data->sem), data->idx);
   test_assert(OS_TIMEOUT == ret);

   data->result = true;

   return 0;
}

/**
 * test procedure for test1 task1
 */
int test1_task_proc1(void* OS_UNUSED(param))
{
   int ret;

   ret = os_sem_down(&(worker_tasks[0].sem), OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   return 0;
}

/**
 * test procedure for test1 task2
 */
int test1_task_proc2(void* OS_UNUSED(param))
{
   int ret;

   /* wait until timeout - (if still exist the bug will not update the priomax while wakup by timeout) */
   ret = os_sem_down(&(worker_tasks[0].sem), 10);
   test_assert(OS_TIMEOUT == ret);
   /* signalize the same sem to wake up the task1 */
   os_sem_up(&(worker_tasks[0].sem));

   return 0;
}

int testcase_1(void)
{
   int ret;
   unsigned i;

   /* clear out memory */
   memset(worker_tasks, 0, sizeof(worker_tasks));

   /* create tasks */
   for(i = 0; i < TEST_TASKS; i++) {
      worker_tasks[i].idx = i + 1;
      os_sem_create(&(worker_tasks[i].sem), 0);
      os_task_create(
         &(worker_tasks[i].task), min(i + 1, OS_CONFIG_PRIOCNT - 1),
         worker_tasks[i].task1_stack, sizeof(worker_tasks[i].task1_stack),
         task_proc, &(worker_tasks[i]));
   }

   /* join tasks and collect the results */
   ret = 0;
   for(i = 0; i < TEST_TASKS; i++) {
      ret = os_task_join(&(worker_tasks[i].task));
      test_assert(0 == ret);
      ret = worker_tasks[i].result ? 0 : 1;
      test_assert(0 == ret);
   }

   return ret;
}

int testcase_regresion(void)
{
   /* regresion test - two tasks hanged on semaphore, higher prioritized with
    * timeout, once timeout expire signalize the semaphore to wake up the low
    * prioritized, in case of bug low priority task will be not woken up because
    * issing priomax update in task_queue */
   os_sem_create(&(worker_tasks[0].sem), 0);
   os_task_create(
      &(worker_tasks[0].task), 1,
      worker_tasks[0].task1_stack, sizeof(worker_tasks[0].task1_stack),
      test1_task_proc1, NULL);
   os_task_create(
      &(worker_tasks[1].task), 2,
      worker_tasks[1].task1_stack, sizeof(worker_tasks[1].task1_stack),
      test1_task_proc2, NULL);
   /* finish the test */
   os_task_join(&(worker_tasks[0].task));
   os_task_join(&(worker_tasks[1].task));

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

      ret = testcase_regresion();
      if(ret) {
         test_debug("Testcase regresion failure");
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
   test_setupmain("Test_Sem");
   os_start(init, idle);
   return 0;
}

/** /} */

