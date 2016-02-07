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
 * /file Test os semaphore rutines
 * /ingroup tests
 *
 * /{
 */

#include "os.h"
#include "os_test.h"

#define TEST_TASKS ((unsigned)10)

static volatile unsigned global_tick_cnt = 0;
static volatile unsigned irq_trigger_tick = 0;
static os_waitqueue_t *irq_trigger_waitqueue = NULL;
static bool sleeper_wokenup = false;

static os_task_t task_main;
static os_task_t task_helper;
static os_task_t task_sleeper;
static os_task_t task_victim[3];
static OS_TASKSTACK task_main_stack[OS_STACK_MINSIZE];
static OS_TASKSTACK task_helper_stack[OS_STACK_MINSIZE];
static OS_TASKSTACK task_sleeper_stack[OS_STACK_MINSIZE];
static OS_TASKSTACK task_victim_stack[3][OS_STACK_MINSIZE];

typedef struct {
   os_waitqueue_t *waitqueue;
   bool wait;
   bool timeout;
} helper_task_param_t;

typedef struct {
   os_waitqueue_t *waitqueue;
   os_ticks_t timeout;
   bool timeouted;
} sleeper_task_param_t;

typedef struct {
   os_waitqueue_t *waitqueue;
   size_t idx;
   bool wokenup;
   bool repeat;
} victim_task_param_t;

void test_idle(void)
{
   /* nothing to do */
}

int sleeper_task_proc(void *param)
{
   sleeper_task_param_t *p = (sleeper_task_param_t*)param;
   os_retcode_t ret;

   test_verbose_debug(
      "sleeper os_waitqueue_prep(%s)",
      p->timeout == OS_TIMEOUT_INFINITE ? "TIMEOUT_INFINITE" :
      "some_timeout_val");
   os_waitqueue_prep(p->waitqueue, p->timeout);
   test_verbose_debug( "sleeper os_waitqueue_wait()");
   ret = os_waitqueue_wait();
   sleeper_wokenup = true;
   test_assert(ret == (p->timeouted ? OS_TIMEOUT : OS_OK));

   return 0;
}

void start_sleeper_task(
   os_waitqueue_t *waitqueue,
   os_ticks_t timeout,
   bool timeouted)
{
   static sleeper_task_param_t param;

   param.waitqueue = waitqueue;
   param.timeout = timeout;
   param.timeouted = timeouted;

   test_verbose_debug("creating sleeper task");
   sleeper_wokenup = false;
   os_task_create(
      &task_sleeper, OS_CONFIG_PRIOCNT - 4,
      task_sleeper_stack, sizeof(task_sleeper_stack),
      sleeper_task_proc, &param);
}

int join_sleeper_task(void)
{
   test_verbose_debug("joining sleeper task");
   return os_task_join(&task_sleeper);
}

int testcase_task_wakeup(void)
{
   os_waitqueue_t waitqueue;
   os_sem_t sem;
   os_retcode_t ret;

   os_waitqueue_create(&waitqueue);
   os_sem_create(&sem, 0);

   /* simple wakeup from task (not IRQ) */
   start_sleeper_task(&waitqueue, 5, false);
   ret = os_sem_down(&sem, 1); /* sleep for a while and allow sleeper to run */
   test_assert(OS_TIMEOUT == ret);
   test_verbose_debug("waking up sleeper task");
   os_waitqueue_wakeup(&waitqueue, 1); /* wake it up after 1 tick - before timeout */
   /* since we have higher priority than sleeper we need to suspend, this would
    * allow sleeper to run. Up to now sleeper should not be scheduled */
   test_assert(false == sleeper_wokenup);

   /* suspend and allow sleeper to run */
   (void)join_sleeper_task();
   test_assert(true == sleeper_wokenup);

   /* wakeup after timeout */
   start_sleeper_task(&waitqueue, 3, true);
   ret = os_sem_down(&sem, 5); /* sleep for 5 ticks and allow sleeper to timeout */
   test_assert(OS_TIMEOUT == ret);
   test_verbose_debug("waking up sleeper task");
   os_waitqueue_wakeup(&waitqueue, 1); /* wake it up after 5 tick - after timeout */
   /* since we have higher priority than sleeper we need to suspend, this would
    * allow sleeper to run. Up to now sleeper should not be scheduled */
   test_assert(true == sleeper_wokenup);

   /* suspend and allow sleeper to run */
   (void)join_sleeper_task();

   os_sem_destroy(&sem);
   os_waitqueue_destroy(&waitqueue);

   return 0;
}

/**
 * Testing os_waitqueue_wait() and os_waitqueue_break() implementation with
 * timeout guard.
 */
int testcase_isr_wakeup_impl(
   bool main,
   os_waitqueue_t *waitqueue,
   bool wait,
   bool timeout)
{
   unsigned local_tick_cnt = 0;
   os_retcode_t ret;

   main = main; /* to silence compilation warning in case of suppressed debugs */

   ret = os_waitqueue_prep(waitqueue, 5);
   test_assert(ret == OS_OK);

   while (1) {
      if (local_tick_cnt != global_tick_cnt) {
         test_verbose_debug("%s detected tick increase %u != %u",
                            main ? "main" : "helper",
                            local_tick_cnt, global_tick_cnt);
         if (local_tick_cnt != 0) {
            test_verbose_debug("%s exit from spin on condition",
                               main ? "main" : "helper");
            break; /* break after one tick */
         }
         local_tick_cnt = global_tick_cnt;
      }
   }

   if (wait) {
      test_verbose_debug("%s calling os_waitqueue_wait()",
                         main ? "main" : "helper");
      ret = os_waitqueue_wait();
      test_assert(ret == (timeout ? OS_TIMEOUT : OS_OK));
      test_verbose_debug("%s returned from os_waitqueue_wait() -> %s",
                         main ? "main" : "helper",
                         timeout ? "OS_TIMEOUT": "OS_OK");
   } else {
      test_verbose_debug("%s calling os_waitqueue_break()",
                         main ? "main" : "helper");
      os_waitqueue_break();
   }

   /* spin with yield() for some time to be able to test the OS status after
    * test will stabilize (all timeouts will burnoff IRS will wakeup etc) */
   test_verbose_debug("%s yield() loop for verification", main ? "main" : "helper");
   while (1) {
      os_yield();
      if (global_tick_cnt > 10)
         break;
      if (local_tick_cnt != global_tick_cnt) {
         test_verbose_debug("%s detected tick increase %u != %u",
                            main ? "main" : "helper",
                            local_tick_cnt, global_tick_cnt);
         local_tick_cnt = global_tick_cnt;
      }
   }

   /* verify the cleanup (following two will assert if waitqueue_current will no
    * be properly cleared */
   test_verbose_debug("%s verify results",
                      main ? "main" : "helper");
   os_waitqueue_prep(waitqueue, OS_TIMEOUT_INFINITE);
   os_waitqueue_break();
   test_assert(!task_current->timer);

   return 0;
}

int helper_task_proc(void *param)
{
   helper_task_param_t *p = (helper_task_param_t*)param;

   return testcase_isr_wakeup_impl(false, p->waitqueue, p->wait, p->timeout);
}

void start_helper_task(
   os_waitqueue_t *waitqueue,
   bool wait,
   bool timeout)
{
   static helper_task_param_t param;

   param.waitqueue = waitqueue;
   param.wait = wait;
   param.timeout = timeout;
   test_verbose_debug("creating helper task");
   os_task_create(
      &task_helper, OS_CONFIG_PRIOCNT - 3,
      task_helper_stack, sizeof(task_helper_stack),
      helper_task_proc, &param);
}

int join_helper_task(void)
{
   test_verbose_debug("joining helper task");
   return os_task_join(&task_helper);
}

int testcase_isr_wakeup(void)
{
   os_waitqueue_t waitqueue;

   os_waitqueue_create(&waitqueue);

   /* testing os_waitqueue_break on single thread - no wakeup */
   test_verbose_debug("testing os_waitqueue_break() - no wakeup");
   global_tick_cnt = 0; /* reset tickcnt's */
   irq_trigger_waitqueue = NULL;
   irq_trigger_tick = 0;
   testcase_isr_wakeup_impl(true, &waitqueue, false, false);

   /* testing os_waitqueue_wait() on single thread - no wakeup */
   test_verbose_debug("testing os_waitqueue_wait() timeout after 5 ticks - no wakeup");
   global_tick_cnt = 0; /* reset tickcnt's */
   irq_trigger_waitqueue = NULL;
   irq_trigger_tick = 0;
   testcase_isr_wakeup_impl(true, &waitqueue, true, true);

   /* testing os_waitqueue_wait() on single thread - wakeup in tick 3, after
    * os_waitqueue_wait() */
   test_verbose_debug("testing os_waitqueue_wait() - wakeup after wait()");
   global_tick_cnt = 0; /* reset tickcnt's */
   irq_trigger_waitqueue = &waitqueue;
   irq_trigger_tick = 4;
   testcase_isr_wakeup_impl(true, &waitqueue, true, false);

   /* testing os_waitqueue_wait() on single thread - wakeup in tick 1, before
    * os_waitqueue_wait() */
   test_verbose_debug("testing os_waitqueue_wait() - wakeup before wait() after"
                      " prepare()");
   global_tick_cnt = 0; /* reset tickcnt's */
   irq_trigger_waitqueue = &waitqueue;
   irq_trigger_tick = 1;
   testcase_isr_wakeup_impl(true, &waitqueue, true, false);

   /* the same with 3 threads */
   test_verbose_debug("testing using 3 threads\n");
   start_sleeper_task(&waitqueue, OS_TIMEOUT_INFINITE, false);

   /* testing os_waitqueue_break - no wakeup */
   test_verbose_debug("testing multi os_waitqueue_break() - no wakeup");
   global_tick_cnt = 0; /* reset tickcnt's */
   irq_trigger_waitqueue = NULL;
   irq_trigger_tick = 0;
   start_helper_task(&waitqueue, false, false);
   testcase_isr_wakeup_impl(true, &waitqueue, false, false);
   join_helper_task();

   /* testing os_waitqueue_wait() - no wakeup */
   test_verbose_debug("testing multi os_waitqueue_wait() timeout after 5 ticks - no wakeup");
   global_tick_cnt = 0; /* reset tickcnt's */
   irq_trigger_waitqueue = NULL;
   irq_trigger_tick = 0;
   start_helper_task(&waitqueue, true, true);
   testcase_isr_wakeup_impl(true, &waitqueue, true, true);
   join_helper_task();

   /* testing os_waitqueue_wait() - wakeup in tick 3, after os_waitqueue_wait() */
   test_verbose_debug("testing multi os_waitqueue_wait() - wakeup after wait()");
   global_tick_cnt = 0; /* reset tickcnt's */
   irq_trigger_waitqueue = &waitqueue;
   irq_trigger_tick = 4;
   start_helper_task(&waitqueue, true, false);
   testcase_isr_wakeup_impl(true, &waitqueue, true, false);
   join_helper_task();

   /* we cannot test 2 threads wakeup's before they call os_waitqueue_wait()
    * since after os_wait_prepare() preemption is disabled.
    * Only single thread will be scheduled until os_waitqueue_wait() will be
    * called */

   /* in multi task test, we wakeup two task at the same time from ISR
    * we also had 3 task suspended on waitqueue since we used sleeper
    * the point is to check if sleeper was not woken up until now, so we make
    * sure that task counting durring wakeup works as expected */
   test_assert(false == sleeper_wokenup);

   /* finalize the test */
   irq_trigger_waitqueue = NULL;
   irq_trigger_tick = 0;

   /* join sleeper task */
   os_waitqueue_wakeup(&waitqueue, 1);
   (void)join_sleeper_task();
   test_assert(true == sleeper_wokenup);

   os_waitqueue_destroy(&waitqueue);

   return 0;
}

int victim_task_proc(void *param)
{
   victim_task_param_t *p = (victim_task_param_t*)param;
   os_retcode_t ret;

   test_verbose_debug("victim[%zu] os_waitqueue_prep()", p->idx);
   ret = os_waitqueue_prep(p->waitqueue, OS_TIMEOUT_INFINITE);
   test_assert(ret == OS_OK);
   test_verbose_debug("victim[%zu] os_waitqueue_wait(TIMEOUT_INFINITE)", p->idx);
   ret = os_waitqueue_wait();
   p->wokenup = true;
   test_assert(ret == OS_DESTROYED);

   return 0;
}

/**
 * Testing os_waitqueue_destroy() with tasks which suspend with time guards
 */
int testcase_destroy(void)
{
   int ret;
   size_t i;
   os_waitqueue_t waitqueue;
   os_sem_t sem;
   os_retcode_t retcode;
   victim_task_param_t param[3];

   os_waitqueue_create(&waitqueue);
   os_sem_create(&sem, 0);

   test_verbose_debug("creating victim tasks");
   for (i = 0; i < 3; i++) {
      param[i].waitqueue = &waitqueue;
      param[i].idx = i;
      param[i].wokenup = false;

      os_task_create(
         &task_victim[i], OS_CONFIG_PRIOCNT - 4,
         task_victim_stack[i], sizeof(task_victim_stack[i]),
         victim_task_proc, &param[i]);
   }

   test_verbose_debug("main going to suspend for 1 tick");
   retcode = os_sem_down(&sem, 1);
   test_assert(OS_TIMEOUT == retcode);

   test_verbose_debug("main destroying waitqueue");
   os_waitqueue_destroy(&waitqueue);

   test_verbose_debug("joining victim tasks");
   for (i = 0; i < 3; i++) {
      ret = os_task_join(&task_victim[i]);
      test_assert(0 == ret);
      test_assert(true == param[i].wokenup);
   }

   os_sem_destroy(&sem);

   return 0;
}

int hiprio_task_proc(void *param)
{
   victim_task_param_t *p = (victim_task_param_t*)param;
   os_retcode_t ret;

   do {
      test_verbose_debug("hiprio[%zu] os_waitqueue_prep(OS_TIMEOUT_INFINITE)", p->idx);
      ret = os_waitqueue_prep(p->waitqueue, OS_TIMEOUT_INFINITE);
      test_assert(ret == OS_OK);
      test_verbose_debug("hiprio[%zu] os_waitqueue_wait()", p->idx);
      ret = os_waitqueue_wait();
      p->wokenup = true;
      test_assert(ret == OS_OK);
   } while (p->repeat);

   return 0;
}

/* this test checks if os_waitqueue_wakeup() wakes up all suspended task exactly
 * once. In this test we wake up tasks with higher priority than main task. In
 * case wakeup will switch context right after making the woken up task READY,
 * we will stuck in infinite loop. If os_waitqueue_wakeup() will be implemented
 * correctly, it will make all tasks READY, but the context switch will be done
 * out of wake up loop. So each task will be woken up exactly once!! */
int testcase_wakeup_hiprio(void)
{
   int ret;
   size_t i;
   os_waitqueue_t waitqueue;
   victim_task_param_t param[3];

   os_waitqueue_create(&waitqueue);

   test_verbose_debug("creating hiprio tasks");
   for (i = 0; i < 2; i++) {
      param[i].waitqueue = &waitqueue;
      param[i].idx = i;
      param[i].wokenup = false;
      param[i].repeat = true;

      os_task_create(
         &task_victim[i], OS_CONFIG_PRIOCNT - (i > 0 ? 1 : 2),
         task_victim_stack[i], sizeof(task_victim_stack[i]),
         hiprio_task_proc, &param[i]);
   }

   test_verbose_debug("waking up all hiprio");
   os_waitqueue_wakeup_sync(&waitqueue, OS_WAITQUEUE_ALL, false);

   for (i = 0; i < 2; i++) {
      test_assert(true == param[i].wokenup);
      param[i].repeat = false;
   }
   test_verbose_debug("waking up all hiprio - to exit");
   os_waitqueue_wakeup_sync(&waitqueue, OS_WAITQUEUE_ALL, false);

   test_verbose_debug("joining victim tasks");
   for (i = 0; i < 2; i++) {
      ret = os_task_join(&task_victim[i]);
      test_assert(0 == ret);
   }

   os_waitqueue_destroy(&waitqueue);

   return 0;
}

int lowprio_task_proc(void *param)
{
   victim_task_param_t *p = (victim_task_param_t*)param;
   os_retcode_t ret;

   do {
      test_verbose_debug("lowprio[%zu] os_waitqueue_prep()", p->idx);
      ret = os_waitqueue_prep(p->waitqueue, OS_TIMEOUT_INFINITE);
      test_assert(ret == OS_OK);

      /* spin until tick */
      while (global_tick_cnt < 2);

      test_verbose_debug("lowprio[%zu] os_waitqueue_wait()", p->idx);
      ret = os_waitqueue_wait();
   } while (p->repeat);

   return 0;
}

/* in this test we check if waitqueue provides the fair scheduling policy
 * when hiprio task is already waiting on waitqueue while in the same time
 * waitqueue is notified from IRQ and the lowprio task is checking the
 * assosiated condition */
int testcase_lowprio_prepare(void)
{
   os_waitqueue_t waitqueue;
   uint8_t i;
   int ret;
   victim_task_param_t param[3];

   os_waitqueue_create(&waitqueue);

   global_tick_cnt = 0; /* reset tickcnt's */
   irq_trigger_waitqueue = &waitqueue;
   irq_trigger_tick = 1;

   test_verbose_debug("creating hiprio task");
   param[0].waitqueue = &waitqueue;
   param[0].idx = 0;
   param[0].wokenup = false;
   param[0].repeat = false;
   os_task_create(
      &task_victim[0], OS_CONFIG_PRIOCNT - 1,
      task_victim_stack[0], sizeof(task_victim_stack[0]),
      hiprio_task_proc, &param[0]);

   test_verbose_debug("creating lowprio task");
   param[1].waitqueue = &waitqueue;
   param[1].idx = 0;
   param[1].wokenup = false;
   param[1].repeat = false;
   os_task_create(
      &task_victim[1], OS_CONFIG_PRIOCNT - 2,
      task_victim_stack[1], sizeof(task_victim_stack[1]),
      lowprio_task_proc, &param[1]);

   /* if we get here this means that hiprio task has ended while lowprio task is
    * suspended, disable the IRQ wakeup */
   irq_trigger_waitqueue = NULL;
   irq_trigger_tick = 0;

   /* we should be now in tick 3, the hiprio should be wokenup while the lowprio
    * should be in os_waitqueue_wait. Thos would mean that lowprio didnt stole
    * the notification from nose of hiprio even if it was running while ISR
    * notified the waitqueue */
   test_assert(param[0].wokenup);
   test_assert(!(param[1].wokenup));

   test_verbose_debug("waking up lowprio");
   os_waitqueue_wakeup_sync(&waitqueue, 1, false);

   test_assert(param[0].wokenup);
   test_assert(param[1].wokenup);

   test_verbose_debug("joining tasks");
   for (i = 0; i < 2; i++) {
      ret = os_task_join(&task_victim[i]);
      test_assert(0 == ret);
   }

   os_waitqueue_destroy(&waitqueue);

   return 0;
}

/* \TODO write bit banging on two threads and waitqueue as test5
 * this will be the stress proff of concept */

/**
 * The main task for tests manage
 */
int mastertask_proc(void *OS_UNUSED(param))
{
   int retv;

   retv = testcase_task_wakeup();
   test_debug("wakeup from task OK");
#if 0
   retv |= testcase_isr_wakeup();
   test_debug("wakeup from ISR OK");
#endif
   retv |= testcase_destroy();
   test_debug("wakeup from destroy() OK");
   retv |= testcase_wakeup_hiprio();
   test_debug("wakeup hiprio OK");

   test_result(retv);
   return 0;
}

void test_tick(void)
{
   test_verbose_debug("Tick %u", global_tick_cnt);

   global_tick_cnt++;

   if (irq_trigger_waitqueue && (irq_trigger_tick == global_tick_cnt)) {
      /* passing 2 as nbr will wake up main and helper task, but not the sleeper */
      test_verbose_debug("wakeup from ISR!!!");
      os_waitqueue_wakeup(irq_trigger_waitqueue, 2);
   }
}

void test_init(void)
{
   test_setuptick(test_tick, 50000000);

   os_task_create(
      &task_main, OS_CONFIG_PRIOCNT - 3,
      task_main_stack, sizeof(task_main_stack),
      mastertask_proc, NULL);
}

int main(void)
{
   os_init();
   test_setupmain("Test_Waitqueue");
   test_init();
   os_start(test_idle);

   return 0;
}

/** /} */

