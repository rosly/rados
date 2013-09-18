/*
 * This file is a part of RadOs project
 * Copyright (c) 2013, Radoslaw Biernaki <radoslaw.spin_intcondiernacki@gmail.spin_spincntom>
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

#define test_verbose_debug test_debug
//#define test_verbose_debug(format, ...)

typedef struct {
   os_task_t task;
   long int task1_stack[OS_STACK_MINSIZE];
   unsigned idx;
   os_waitqueue_t wait_queue;
   volatile unsigned spin_intcond;
   volatile unsigned spin_extcond;
   volatile unsigned spin_spincnt;
   volatile unsigned spin_condmeetcnt;
   volatile unsigned spin_intcond_reload;
   uint_fast16_t timeout;
   os_retcode_t retcode;
} task_data_t;

static os_task_t task_main;
static long int task_main_stack[OS_STACK_MINSIZE];
static task_data_t worker_tasks[TEST_TASKS];
static os_waitqueue_t global_wait_queue;
static os_sem_t global_sem;
static unsigned test3_active = 0;
static unsigned test5_active = 0;
static unsigned global_tick_cnt = 0;

void idle(void)
{
   /* nothing to do */
}

/**
 * Simple test procedure for typicall waitloop usage.
 */
int slavetask_proc(void* param)
{
   task_data_t *data = (task_data_t*)param;

  /* we have two loops
   * internal one simulates waiting for external event and on that loop we focus
   * external one is used only for making multiple test until we decide that
   * task should be joined, therefore os_waitqueue_prepare() is inside the
   * outern loop */

   test_verbose_debug("Task %u started", data->idx);

   while(0 == data->spin_extcond) {
      if(data->spin_intcond_reload > 0) {
         --(data->spin_intcond_reload);
         data->spin_intcond = 1;
      } else {
         data->spin_intcond = 0;
      }
      while(1)
      {
         os_waitqueue_prepare(
           &global_wait_queue,
           (0 == data->timeout) ? OS_TIMEOUT_INFINITE : data->timeout);
         test_verbose_debug("Task %u spins ...", data->idx);
         ++(data->spin_spincnt); /* signalize that we performed condition test */
         if(0 != data->spin_intcond) {
            os_waitqueue_finish();
            break;
         }
         data->retcode = os_waitqueue_wait(); /* condition not meet, go to sleep */
         /* only timeout and success is allowed as return code */
         if(OS_TIMEOUT == data->retcode) {
            test_verbose_debug("Task %u timeouted on wait_queue", data->idx);
         } else {
            test_assert(OS_OK == data->retcode);
         }
      }
      test_verbose_debug("Task %u found condition meet", data->idx);
      (data->spin_condmeetcnt)++;
   }
   test_verbose_debug("Task %u exiting", data->idx);

   return 0;
}

/**
 * Simple test case, when multiple tasks is waiting for the same waitqueue
 */
int testcase_1(void)
{
   os_retcode_t retcode;
   unsigned i;

   /* wait until all tasks will be prepared for sleep */
   for(i = 0; i < TEST_TASKS; i++) {
      test_verbose_debug("Spin count for task %u is %u", i, worker_tasks[i].spin_spincnt);
      if(0 == worker_tasks[i].spin_spincnt) {
         i = -1; /* start check loop from begining */
         /* give time for test tasks to run, we use this sem only for timeout,
          * nobody will rise this sem */
         retcode = os_sem_down(&global_sem, 10); /* 10 ticks of sleep */
         test_assert(OS_TIMEOUT == retcode);
      }
   }

   /* all tasks had been started, they should now sleep on waitqueue and wait
    * for signal, after signal they will spin and check the condition
    * wake up all tasks and check if they had spinned around condition check */
   test_verbose_debug("Main task all slaves slaves");
   os_waitqueue_wakeup(&global_wait_queue, OS_WAITQUEUE_ALL);
   /* give time for test tasks to run (main is most prioritized, eat all CPU */
   retcode = os_sem_down(&global_sem, 10); /* 10 ticks of sleep */
   test_assert(OS_TIMEOUT == retcode);
   /* verify that all tasks have spinned */
   for(i = 0; i < TEST_TASKS; i++) {
      test_verbose_debug("Spin count for task %u is %u", i, worker_tasks[i].spin_spincnt);
      test_assert(2 == worker_tasks[i].spin_spincnt);
   }

   /* last time we woken up all task with OS_WAITQUEUE_ALL
      lest try to woke up all tasks by specifing the exact amount */
   test_verbose_debug("Main task all slaves slaves");
   os_waitqueue_wakeup(&global_wait_queue, TEST_TASKS);
   /* give time for test tasks to run (main is most prioritized, eat all CPU */
   retcode = os_sem_down(&global_sem, 10); /* 10 ticks of sleep */
   test_assert(OS_TIMEOUT == retcode);
   /* verify that all tasks have spinned */
   for(i = 0; i < TEST_TASKS; i++) {
      test_verbose_debug("Spin count for task %u is %u", i, worker_tasks[i].spin_spincnt);
      test_assert(3 == worker_tasks[i].spin_spincnt);
   }

   return 0;
}

/**
 * Simple test case, when multiple tasks is waiting for the same waitqueue
 */
int testcase_2(void)
{
   os_retcode_t retcode;
   unsigned i;

   /* task 0 has highest ptiority from slaves, it we notify only once this
    * should be the task which will woke up */
   test_verbose_debug("Main task signalizes single slave");
   os_waitqueue_wakeup(&global_wait_queue, 1);
   /* give time for test tasks to run (main is most prioritized, eat all CPU */
   retcode = os_sem_down(&global_sem, 10); /* 10 ticks of sleep */
   test_assert(OS_TIMEOUT == retcode);
   /* verify that only task 10 spinned */
   for(i = 0; i < TEST_TASKS; i++) {
      test_verbose_debug("Spin count for task %u is %u", i, worker_tasks[i].spin_spincnt);
      test_assert((9 == i ? 4 : 3) == worker_tasks[i].spin_spincnt);
   }

   return 0;
}

/**
 * Simple test case, when multiple tasks is waiting for the same waitqueue
 */
int testcase_3(void)
{
   os_retcode_t retcode;
   unsigned i;

   /* enable special threatment for this test inside tick calback */
   test3_active = 1;
   /* modify the timeout of each task and */
   for(i = 0; i < TEST_TASKS; i++) {
      worker_tasks[i].timeout = 1 + i * 2; /* in number of ticks */
   }
   /* wake up tasks to allow them to use timeout in next prepare call */
   test_verbose_debug("Main signalizing slaves for timeout reload");
   os_waitqueue_wakeup(&global_wait_queue, OS_WAITQUEUE_ALL);
   /* give time for test tasks to run (main is most prioritized, eat all CPU */
   retcode = os_sem_down(&global_sem, 1);
   test_assert(OS_OK == retcode);

   /* prepare sampling data for test */
   for(i = 0; i < TEST_TASKS; i++) {
      worker_tasks[i].spin_spincnt = 0;
   }
   /* now slave tasks waits on timeout condition, we dont whant that in next
    * loop after signalization they also use timeout, so we clear up the tiemout
    * variables */
   for(i = 0; i < TEST_TASKS; i++) {
      worker_tasks[i].timeout = 0;
   }

   test_verbose_debug("Main wait until slaves will timeout");
   /* verify that only desired task will timeout at each tick count */
   while(global_tick_cnt < (TEST_TASKS * 2) + 1) {
      retcode = os_sem_down(&global_sem, OS_TIMEOUT_INFINITE);
      test_assert(OS_OK == retcode);
      test_verbose_debug("Main woken up global_tick_cnt = %u", global_tick_cnt);
      for(i = 0; i < TEST_TASKS; i++) {
        test_verbose_debug("Spin count for task %u is %u", i, worker_tasks[i].spin_spincnt);
        if(i < global_tick_cnt / 2) {
           /* this task should already timeout and spin the loop */
           test_assert(1 == worker_tasks[i].spin_spincnt);
           test_assert(OS_TIMEOUT == worker_tasks[i].retcode);
        } else {
          /* this task should not timeout */
          test_assert(0 == worker_tasks[i].spin_spincnt);
        }
      }
   }


   /* disable special threatment for this test inside tick calback */
   test3_active = 0;

   return 0;
}

/**
 * Regresion test case
 * I forgot to add os_waitqueue_clean API call
 * This test is about to show that without this call we dont remove task from
 * wait_queu in case when condition will not allow to call os_waitqueue_wait
 */
int testcase_4regresion(void)
{
   os_retcode_t retcode;
   unsigned i;

   /* WARNING task 10 is excluded from this test since it has bigger priority than other tasks
      if it will run, it will not allow other tasks to schedule */

   /* prepare data for test */
   for(i = 0; i < TEST_TASKS - 1; i++) {
      worker_tasks[i].spin_intcond = 1; /* all task should found condition meet without calling os_waitqueue_wait */
      worker_tasks[i].spin_spincnt = 0;
      worker_tasks[i].spin_intcond_reload = 10; /* reload set to 10 will cause 10 spins with cond meet in row */
   }

   /* wake up all tasks and sleep 1 tick
    * this will cause multiple spins of external loop sine worker_tasks[i].spin_extcond is
    * still 0 while worker_tasks[i].spin_intcond is 1 */
   test_verbose_debug("Main allowing slaves to spin multiple times on external loop");
   os_waitqueue_wakeup(&global_wait_queue, OS_WAITQUEUE_ALL);
   /* give time for test tasks to run (main is most prioritized, eat all CPU */
   retcode = os_sem_down(&global_sem, 10);
   test_assert(OS_TIMEOUT == retcode);

   /* check that all task have spinned multiple times */
   for(i = 0; i < TEST_TASKS - 1; i++) {
      test_assert(worker_tasks[i].spin_spincnt > 1);
   }

   /* clean up after test, stop the spinning of threads */
   for(i = 0; i < TEST_TASKS - 1; i++) {
      worker_tasks[i].spin_intcond_reload = 0;
   }

   return 0;
}

/**
 * Regresion test case
 * os_waitqueue_finalize() had a bug. It dont take into account timeouts.
 */
int testcase_5regresion(void)
{
   unsigned local_tick_cnt;

   /* enable spectial threatment */
   local_tick_cnt = global_tick_cnt = 0;
   test5_active = 1;

   /* use waitqueue with timeout (2 ticks) and finish it right away (simulate
    * early condition meet) */
   test_debug("Main sleeping on waitqueue with timeout, then finish right away");
   os_waitqueue_prepare(&global_wait_queue, 2);
   os_waitqueue_finish();
   /* spin and wait for timeout */
   while(1) {
      if(global_tick_cnt > 5) {
         /* break after 5 ticks */
         break;
      }
      if(local_tick_cnt != global_tick_cnt) {
         test_debug("Main detected tick increase");
      }
      local_tick_cnt = global_tick_cnt;
   }
   test_debug("Main finishing test5");

   /* disable spectial threatment */
   test5_active = 0;

   return 0;
}

/* \TODO write bit banging on two threads and waitqueue as test5
 * this will be the stress proff of concept */

/**
 * The main task for tests manage
 */
int mastertask_proc(void* OS_UNUSED(param))
{
   int ret;
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
         slavetask_proc, &(worker_tasks[i]));
   }

   /* we threat this (main) task as IRQ and HW
    * so we will perform all actions here in busy loop to simulate the
    * uncontroled enviroment. Since this task has highest priority even
    * preemption is not able to force this task to sleep */

   do
   {
      ret = testcase_1();
      if(ret) break;
      ret = testcase_2();
      if(ret) break;
      ret = testcase_3();
      if(ret) break;
      ret = testcase_4regresion();
      if(ret) break;
      ret = testcase_5regresion();
      if(ret) break;
   } while(0);

   /* join tasks and collect the results release the tasks from loop */
   test_verbose_debug("Main task joining slaves");
   for(i = 0; i < TEST_TASKS; i++) {
      worker_tasks[i].spin_extcond = 1;
      worker_tasks[i].spin_intcond = 1;
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

   test_result(ret);
   return 0;
}

void test_tickprint(void)
{
  test_verbose_debug("Tick");
  if(test3_active) {
     global_tick_cnt++;
     /* at each tick wake up the main task to allow it to check the test results
      * */
     os_sem_up(&global_sem);
  } else if(test5_active) {
     global_tick_cnt++;
  }
}

void init(void)
{
   test_setuptick(test_tickprint, 50000000);

   os_task_create(
      &task_main, OS_CONFIG_PRIOCNT - 1,
      task_main_stack, sizeof(task_main_stack),
      mastertask_proc, NULL);
}

int main(void)
{
   test_setupmain("Test_Waitqueue");
   os_start(init, idle);
   return 0;
}

/** /} */

