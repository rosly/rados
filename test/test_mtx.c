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
 * /file Test os semaphore routines
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
 * Simple critical section. Implementation will be validated by checking shared
 * variable state with forced preemption
 */
int test_scen1_worker(void* param)
{
   int ret;
   unsigned i;
   long task_idx = (long)param;

   for(i = 0; i < TEST_LOOPS; i++) {
      ret = os_mtx_lock(&test_mtx[0]);
      test_assert(0 == ret);

      /* verity then set some magic numbers in variables protected by mtx */
      test_assert(0 == test_atomic[0]);
      test_assert(-1 == test_atomic[1]);
      test_atomic[0]++;
      test_atomic[1] = task_idx;

      /* force thread switch to check if mtx properly secures the critical
       * section - this can involve context switch but once remain task call
       * os_mtx_lock() we should go back to current task again */
      test_reqtick();

      /* verify if nobody entered the critical section */
      test_assert(1 == test_atomic[0]);
      test_assert(task_idx == test_atomic[1]);

      /* reset the magic numbers in protected variables for another thread */
      --test_atomic[0];
      test_atomic[1] = -1;
      os_mtx_unlock(&test_mtx[0]);

      /* add randomness to test, force thread switch with 50% of probability */
      if( 0 == (rand() % 2) ) test_reqtick();
   }

   return 0;
}

/**
 * Test scenario:
 * Classic priority inversion problem
 * Three tasks with different prio H (high prio) M (mid prio) L (low prio)
 * L locks the mtx as a first, then we force context switch to H which tries to
 * lock the same mtx. H should boost the prio of L and block on mtx.
 * Next L should be scheduled instead of M (which will happened it prio boost will
 * not work). Sequence of scheduling is verified by tracking execution sequence
 * in global variable.
 */
int test_scen2_workerH(void* OS_UNUSED(param))
{
   int ret;

   /* we need to sleep to allow the L to lock the mutex */
   ret = os_sem_down(&test_sem[0], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* try to lock the mtx
    * this should boost the prio of L since it already lock it */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);

   /* signalize that M can be scheduled now
    * finish the task */
   test_atomic[0] = 1;

   return 0;
}

int test_scen2_workerM(void* OS_UNUSED(param))
{
   int ret;

   /* M also need goes to sleep to allow L to lock the mutex,
    * after it will be woken up (simultaneously with L) it should not schedule
    * until L will unlock the mtx */
   ret = os_sem_down(&test_sem[1], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* B should be scheduled after H
    * Verify that H finishes his thread function */
   test_assert(1 == test_atomic[0]);
   test_atomic[0] = 2;

   /* if test_assertion was meet this is the end of the test */
   return 0;
}

int test_scen2_workerL(void* OS_UNUSED(param))
{
   int ret;

   /* take the ownership of mutex, as we would be owner of mtx H should boost
    * our prio during their lock for mtx */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret );

   /* now signalize/wake up the H and M */
   /* following call will cause context switch to H and prio boost of this thread */
   os_sem_up(&test_sem[0]);
   /* once H will call os_mtx_lock we will return here (since it will block on
    * mtx). Since now we have effective prio equal to H (prio boost) following
    * call should not cause context switch to M */
   os_sem_up(&test_sem[1]);

   /* unlock the mtx and reset the effective prio of this thread to L
    * following call should switch context to H which ten will finish (exit the
    * thread function). This should then cause context switch to M, which will
    * verify values in test_atomic[0] and also exit the thread function. */
   os_mtx_unlock(&test_mtx[0]);

   /* finaly after yet another context switch we should return here
    * verify that M finished thread function */
   test_assert(2 == test_atomic[0]);

   return 0;
}

/**
 * Test scenario:
 * Recursive priority inversion problem
 * Priority decrease/sustain while unlocking mutex
 * 4 tasks with 2 different prio H (top prio) M, LM, L (low prio)
 * H blocks on sem[0]
 * M is scheduled and locks the mtx[0] then blocks on sem[1]
 * LM is scheduled and locks the mtx[1] then tries to lock the mtx[0]
 * L is scheduled, it signalizes sem[1] (no context switch) and sem[0] which cause the context switch to H
 * H is scheduled, it tries to lock of mtx[1], this should cause the prio boost of LM but since it is locked also the prio boost of M
 * M is scheduled (not L, also LM is still blocked), chek the prio if it is the same as H, unlock the mtx[0], this will drop the prio to base and context will be switched to LM (since it has boosted prio)
 * LM is scheduled (it should have the prio of H, check it), unlock the mtx[0] (no context switch) and mtx[1] which will drop the prio to base and switch the context to H
 * H is scheduled, unlock the mtx[1] allow flag of L schedule test_result
 * if M is scheduled check if it has again the base prio test_result
 * if LM is scheduled, chek if it has again the base prio, test_result
 * if L is scheduled, check the scheduling alowance flag and prio (all should be base), test_result
 *
 * the main point of the test it to check if priority boost is propagated through the blocking chain
 * L is not in blocking chain so it should not be sheduled until we allow for that (chedule allowance atom flag)
 * L and LM should have the bootsed prio for some time (check that by accessing their os_task_t)
 */
int test_scen3_workerH(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow M, LM and L to progress */
   ret = os_sem_down(&test_sem[0], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* mtx[1] is already locked by M and LM, so if H will try to lock it both M and LM should get boosted prio, while H will lock on following operation */
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);

   /* when get here both M and LM already ended, L should not be scheduled until now,
      signalize that L can be scheduled unlock the mtx[1] and test_result */
   test_atomic[0] = 1;
   os_mtx_unlock(&test_mtx[1]);

   return 0;
}

int test_scen3_workerM(void* OS_UNUSED(param))
{
   int ret;

   /* lock the mtx1 */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);

   /* wait on sem2, context will be scheduled to LM */
   ret = os_sem_down(&test_sem[1], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* if we get here it means that L signalized sem[1]
      in mean time we should got boosted prio by H, check that */
   test_assert(2 == task_worker[1].prio_current);

   /* unlock the mtx[0], this will drop prio to base and ontext swith to LM (since it has higher, boosted prio by H) */
   os_mtx_unlock(&test_mtx[0]);

   /*chek if prio is again as base */
   test_assert(1 == task_worker[1].prio_current);

   return 0;
}

int test_scen3_workerLM(void* OS_UNUSED(param))
{
   int ret;

   /* lock the mtx1 then mtx0 (second will switch the context to L)*/
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);

   /* if we get here it means that M unlocked mtx[0] a it has the base prio
      while this LM should have the boosted prio (check both mentioned conditions) */
   test_assert(1 == task_worker[1].prio_current);
   test_assert(2 == task_worker[2].prio_current);

   /* unlock the mtx[0], no context switch no prio drop (check) */
   os_mtx_unlock(&test_mtx[0]);
   test_assert(2 == task_worker[2].prio_current);
   /* unlock the mtx[1], prio drop and context switch to H*/
   os_mtx_unlock(&test_mtx[1]);
   test_assert(1 == task_worker[2].prio_current);

   return 0;
}

int test_scen3_workerL(void* OS_UNUSED(param))
{
   /* signalizes sem[1] (no context switch) and sem[0] which cause the context switch to H */
   os_sem_up(&test_sem[1]);
   os_sem_up(&test_sem[0]);

   /* check if L could be scheduled then test_result */
   test_assert(1 ==  test_atomic[0]);
   test_atomic[0] = 2;

   return 0;
}

int test_deadlock_workerA(void* OS_UNUSED(param))
{
   test_assert(0);
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
      /* created task will be not scheduled because current task has the highest available priority */
      os_task_create(
         &task_worker[i], 1,
         task_stack[i], sizeof(task_stack[i]),
         test_scen1_worker, (void*)(long)i);
   }
   /* scheduler will kick in after following call */
   for(i = 0; i < 4; i++)
   {
      os_task_join(&task_worker[i]);
   }

/* scenario 2 */
   os_taskproc_t scen2_worker_proc[] = { test_scen2_workerH, test_scen2_workerM, test_scen2_workerL };
   os_mtx_create(&test_mtx[0]);
   test_atomic[0] = 0;
   for(i = 0; i < 3; i++)
   {
      os_sem_create(&test_sem[i], 0);
       /* created task will be not scheduled because current task has the highest available priority */
      os_task_create(
         &task_worker[i], 3 - i,
         task_stack[i], sizeof(task_stack[i]),
         scen2_worker_proc[i], (void*)(long)i);
   }
   /* scheduler will kick in after following call */
   for(i = 0; i < 3; i++)
   {
      os_task_join(&task_worker[i]);
   }

/* scenario 3 */
   os_taskproc_t scen3_worker_proc[] = {
	   test_scen3_workerH,
	   test_scen3_workerM,
	   test_scen3_workerLM,
	   test_scen3_workerL
   };
   os_sem_create(&test_sem[0], 0);
   os_sem_create(&test_sem[1], 0);
   os_mtx_create(&test_mtx[0]);
   os_mtx_create(&test_mtx[1]);
   test_atomic[0] = 0;
   for(i = 0; i < 4; i++)
   {
      /* created task will be not scheduled because current task has the highest available priority */
      os_task_create(
         &task_worker[i], (0 == i) ? 2 : 1,
         task_stack[i], sizeof(task_stack[i]),
         scen3_worker_proc[i], (void*)(long)i);
   }
   /* scheduler will kick in after following call */
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

