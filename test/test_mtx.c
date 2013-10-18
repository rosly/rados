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

#include <stdlib.h>

#include "os.h"
#include "os_test.h"

#define container_of(_ptr, _type, _member) ({ \
   const typeof( ((_type *)0)->_member ) *_mptr = (_ptr); \
   (_type *)( (char *)_mptr - offsetof(_type,_member) );})

#define TEST_LOOPS ((unsigned)1000)

static os_task_t task_worker[4];
static OS_TASKSTACK task_stack[4][OS_STACK_MINSIZE];
static os_task_t task_coordinator;
static OS_TASKSTACK coordinator_stack[OS_STACK_MINSIZE];
static os_mtx_t test_mtx[2];
static os_sem_t test_sem[3];

static volatile sig_atomic_t test_atomic[2];

void idle(void)
{
   /* nothing to do */
}

/**
 * Test scenario:
 * Simple critical section. Implementation will be validated by counter constrains (test_assertions)
 */
int test_scen1_worker(void* param)
{
   int ret;
   unsigned i;
   long task_idx = (long)param;

   for(i = 0; i < TEST_LOOPS; i++) {
      ret = os_mtx_lock(&test_mtx[0]);
      test_assert(0 == ret);
      test_assert(0 == test_atomic[0]);
      test_assert(-1 == test_atomic[1]);
      test_atomic[0]++;
      test_atomic[1] = task_idx;
      test_reqtick(); /* force thread switch to check if we are secured by critical section - this can involve context switch but once remain task will try to enter the critical section we should go back to current task again */
      test_assert(1 == test_atomic[0]);
      test_assert(task_idx == test_atomic[1]);
      --test_atomic[0];
      test_atomic[1] = -1;
      os_mtx_unlock(&test_mtx[0]);

      if( 0 == (rand() % 2) ) test_reqtick(); /* force thread switch with 50% of probability */

      ret = os_mtx_lock(&test_mtx[0]);
      test_assert(0 == ret);
      test_assert(0 == test_atomic[0]);
      test_assert(-1 == test_atomic[1]);
      test_atomic[0]++;
      test_atomic[1] = task_idx;
      test_reqtick(); /* force thread switch to check if we are secured by critical section */
      test_assert(1 == test_atomic[0]);
      test_assert(task_idx == test_atomic[1]);
      --test_atomic[0];
      test_atomic[1] = -1;
      os_mtx_unlock(&test_mtx[0]);

      if( 0 == (rand() % 2) ) test_reqtick(); /* force thread switch with 50% of probability */
   }

   return 0;
}

/**
 * Test scenario:
 * Typpical priority inversion problem
 * Three tasks with different prio A (top prio) B (mid prio) C (low prio)
 * C locks the mtx first, then we artificaly switch the context to A which tries to lock the same mtx
 * C should boost the prio of A and while it blocks on mutex lock C should be scheduled instead of B (which will happend it prio boost will not work)
 * B should not be scheduled at all until we alow for that
 */
int test_scen2_workerA(void* OS_UNUSED(param))
{
   int ret;

   /* we need to hold on the A, to allow the C to lock the mutex */
   ret = os_sem_down(&test_sem[0], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* try to lock the mtx, this should boost the prio of C */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);

   /* signalize that B can be scheduled now
      finish the task */
   test_atomic[0] = 1;

   return 0;
}

int test_scen2_workerB(void* OS_UNUSED(param))
{
   int ret;

   /* B also has to allow C to lock the mutex, while then after it will be unblocked together with A it should not shedule until A will unlock the mtx */
   ret = os_sem_down(&test_sem[1], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* B should not be scheduled untill test procedure will end, so check the condition it this is the right time */
   test_assert(1 == test_atomic[0]);
   test_atomic[0] = 2;

   /* if test_assertion was meet this is the end of the test */
   return 0;
}


int test_scen2_workerC(void* OS_UNUSED(param))
{
   int ret;

   /* take the ownership of mutex to allow for prio boosting by A */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret );

   /* now signalize the A and B */
   os_sem_up(&test_sem[0]); /* this call will couse context switch to A since it has higher ptio than C */
   os_sem_up(&test_sem[1]); /* once A will call os_mtx_lock we will return here, and this call will not couse the context switch since A will have the boosted prio which will be higher than B */

   /* unlock the mtx
      we have signalized sem for A and B but since this (C) has boosted prio B shoudnt be scheduled until now
      when we unlock the mtx we will rvert the prio of C to base_prio and A should be scheduled
      remain test will be leaded by A */
   os_mtx_unlock(&test_mtx[0]); /* calling of this will cause the prio reset and swithich to A, then when A will allow B to schedule and test_result we will switch to B */

   /* here after return B shoud be finaly scheduled, lets check that and test_result */
   test_assert(2 == test_atomic[0]);

   return 0;
}

/**
 * Test scenario:
 * Chain priority boosting while blocking on mutex
 * Priority decrease/sustain while unlocking mutex
 * 4 tasks with 2 different prio A (top prio) B, C, D (low prio)
 * A blocks on sem[0]
 * B is scheduled and locks the mtx[0] then blocks on sem[1]
 * C is scheduled and locks the mtx[1] then tries to lock the mtx[0]
 * D is scheduled, it signalizes sem[1] (no context switch) and sem[0] which cause the context switch to A
 * A is scheduled, it tries to lock of mtx[1], this should cause the prio boost of C but since it is locked also the prio boost of B
 * B is scheduled (not D, also C is still blocked), chek the prio if it is the same as A, unlock the mtx[0], this will drop the prio to base and context will be switched to C (since it has boosted prio)
 * C is scheduled (it should have the prio of A, check it), unlock the mtx[0] (no context switch) and mtx[1] which will drop the prio to base and switch the context to A
 * A is scheduled, unlock the mtx[1] allow flag of D schedule test_result
 * if B is scheduled check if it has again the base prio test_result
 * if C is scheduled, chek if it has again the base prio, test_result
 * if D is scheduled, check the scheduling alowance flag and prio (all should be base), test_result
 *
 * the main point of the test it to check if priority boost is propagated through the blocking chain
 * D is not in blocking chain so it should not be sheduled until we allow for that (chedule allowance atom flag)
 * D and C should have the bootsed prio for some time (check that by accessing their os_task_t)
 */
int test_scen3_workerA(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow B, C and D to progress */
   ret = os_sem_down(&test_sem[0], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* mtx[1] is allready locked by B and C, so if A will try to lock it both B and C should get boosted prio, while A will lock on following operation */
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);

   /* when get here both B and C already ended, D should not be scheduled until now,
      signalize that D can be scheduled unlock the mtx[1] and test_result */
   test_atomic[0] = 1;
   os_mtx_unlock(&test_mtx[1]);

   return 0;
}

int test_scen3_workerB(void* OS_UNUSED(param))
{
   int ret;

   /* lock the mtx1 */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);

   /* wait on sem2, context will be scheduled to C */
   ret = os_sem_down(&test_sem[1], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* if we get here it means that D signalized sem[1]
      in mean time we should got boosted prio by A, check that */
   test_assert(2 == task_worker[1].prio_current);

   /* unlock the mtx[0], this will drop prio to base and ontext swith to C (since it has higher, boosted prio by A) */
   os_mtx_unlock(&test_mtx[0]);

   /*chek if prio is again as base */
   test_assert(1 == task_worker[1].prio_current);

   return 0;
}

int test_scen3_workerC(void* OS_UNUSED(param))
{
   int ret;

   /* lock the mtx1 then mtx0 (second will switch the context to D)*/
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);

   /* if we get here it means that B unlocked mtx[0] a it has the base prio
      while this C should have the boosted prio (check both mentioned conditions) */
   test_assert(1 == task_worker[1].prio_current);
   test_assert(2 == task_worker[2].prio_current);

   /* unlock the mtx[0], no context switch no prio drop (check) */
   os_mtx_unlock(&test_mtx[0]);
   test_assert(2 == task_worker[2].prio_current);
   /* unlock the mtx[1], prio drop and context switch to A*/
   os_mtx_unlock(&test_mtx[1]);
   test_assert(1 == task_worker[2].prio_current);

   return 0;
}

int test_scen3_workerD(void* OS_UNUSED(param))
{
   /* signalizes sem[1] (no context switch) and sem[0] which cause the context switch to A */
   os_sem_up(&test_sem[1]);
   os_sem_up(&test_sem[0]);

   /* check if D could be scheduled then test_result */
   test_assert(1 ==  test_atomic[0]);
   test_atomic[0] = 2;

   return 0;
}

/**
 * Test coordinator, runs all test in unit
 */
int test_coordinator(void* OS_UNUSED(param))
{
   unsigned i;

/* scenario 1 */
   os_mtx_create(&test_mtx[0]);
   test_atomic[0] = 0;
   test_atomic[1] = -1;
   for(i = 0; i < 4; i++)
   {
      /* created task will be not scheduled becouse current task has the highest available priority */
      os_task_create(
         &task_worker[i], 1,
         task_stack[i], sizeof(task_stack[i]),
         test_scen1_worker, (void*)(long)i);
   }
   /* we allow for worker schduling on first os_task_join call */
   for(i = 0; i < 4; i++)
   {
      os_task_join(&task_worker[i]);
   }

/* scenario 2 */
   os_taskproc_t scen2_worker_proc[] = { test_scen2_workerA, test_scen2_workerB, test_scen2_workerC };
   os_mtx_create(&test_mtx[0]);
   test_atomic[0] = 0;
   for(i = 0; i < 3; i++)
   {
      os_sem_create(&test_sem[i], 0);
       /* created task will be not scheduled becouse current task has the highest available priority */
      os_task_create(
         &task_worker[i], 3 - i,
         task_stack[i], sizeof(task_stack[i]),
         scen2_worker_proc[i], (void*)(long)i);
   }
   /* we allow for worker schduling on first os_task_join call */
   for(i = 0; i < 3; i++)
   {
      os_task_join(&task_worker[i]);
   }

/* scenario 3 */
   os_taskproc_t scen3_worker_proc[] = { test_scen3_workerA, test_scen3_workerB, test_scen3_workerC, test_scen3_workerD };
   os_sem_create(&test_sem[0], 0);
   os_sem_create(&test_sem[1], 0);
   os_mtx_create(&test_mtx[0]);
   os_mtx_create(&test_mtx[1]);
   test_atomic[0] = 0;
   for(i = 0; i < 4; i++)
   {
      /* created task will be not scheduled becouse current task has the highest available priority */
      os_task_create(
         &task_worker[i], (0 == i) ? 2 : 1,
         task_stack[i], sizeof(task_stack[i]),
         scen3_worker_proc[i], (void*)(long)i);
   }
   /* we allow for worker schduling on first os_task_join call */
   for(i = 0; i < 4; i++)
   {
      os_task_join(&task_worker[i]);
   }
   test_assert(2 == test_atomic[0]); /* check if D was scheduled in time */

   test_result(0);
   return 0;
}

void init(void)
{
   os_task_create(
      &task_coordinator, OS_CONFIG_PRIOCNT - 1,
      coordinator_stack, sizeof(coordinator_stack),
      test_coordinator, NULL);
}

int main(void)
{
   test_setupmain("Test_Mtx");
   os_start(init, idle);
   return 0;
}

/** /} */

