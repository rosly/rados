/*
 * This file is a part of RadOs project
 * Copyright (c) 2013, Radoslaw Biernacki <radoslaw.biernacki@gmail.com>
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
static os_mtx_t test_mtx[3];
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

      /* force task switch to check if mtx properly secures the critical
       * section - this can involve context switch but once remain task call
       * os_mtx_lock() we should go back to current task again */
      test_reqtick();

      /* verify if nobody entered the critical section */
      test_assert(1 == test_atomic[0]);
      test_assert(task_idx == test_atomic[1]);

      /* reset the magic numbers in protected variables for another task */
      --test_atomic[0];
      test_atomic[1] = -1;
      os_mtx_unlock(&test_mtx[0]);

      /* add randomness to test, force task switch with 50% of probability */
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
    * Verify that H finishes his task function */
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
   test_assert(0 == ret);

   /* now signalize/wake up the H and M */
   /* following call will cause context switch to H and prio boost of this task */
   os_sem_up(&test_sem[0]);
   /* once H will call os_mtx_lock we will return here (since it will block on
    * mtx). Since now we have effective prio equal to H (prio boost) following
    * call should not cause context switch to M */
   os_sem_up(&test_sem[1]);

   /* unlock the mtx and reset the effective prio of this task to L
    * following call should switch context to H which ten will finish (exit the
    * task function). This should then cause context switch to M, which will
    * verify values in test_atomic[0] and also exit the task function. */
   os_mtx_unlock(&test_mtx[0]);

   /* finally after yet another context switch we should return here
    * verify that M finished task function */
   test_assert(2 == test_atomic[0]);

   return 0;
}

/**
 * Test scenario:
 * Recursive bounded priority inversion problem
 * To not duplicate description of the problem please refer to Comment 2 in
 * os_mtx.c
 *
 * Task L locks only mtx2
 * Task LN locks both mtx1 and mtx2
 * Task H locks only mtx1
 * Task M is woken up (critical moment)
 * Test succeeds if task L is scheduled instead task M
 */
int test_scen3_workerH(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow M, LM and L to progress */
   ret = os_sem_down(&test_sem[0], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* we should return here from L */
   test_assert(5 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 6;

   /* try to lock mtx0, this will preempt and switch to L */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);

   /* we should return here when LM will unlock mtx0 */
   test_assert(10 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 11;

   return 0;
}

int test_scen3_workerM(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow L to progress */
   ret = os_sem_down(&test_sem[0], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* we should return here when H will finish */
   test_assert(11 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 12;

   return 0;
}

int test_scen3_workerLM(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow LM and L to progress */
   ret = os_sem_down(&test_sem[1], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   test_assert(2 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 3;

   /* lock the mtx0 then mtx1 (second will switch the context to L)*/
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);
   test_assert(3 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 4;
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);

   /* we should return here when L will unlock mtx1 */
   test_assert(8 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 9;
   /* also this means that LM should still have p(H) */
   test_assert(4 == task_worker[2].prio_current);
   /* while L should have its original prio */
   test_assert(1 == task_worker[3].prio_current);

   /* unlock mtx1 and mtx0, last one should cause switch to H */
   os_mtx_unlock(&test_mtx[1]);
   /* no context switch */
   test_assert(9 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 10;
   os_mtx_unlock(&test_mtx[0]);

   /* we should return here when H and M will finish */
   test_assert(12 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 13;
   /* also this means that LM should have original prio */
   test_assert(2 == task_worker[2].prio_current);

   return 0;
}

int test_scen3_workerL(void* OS_UNUSED(param))
{
   int ret;

   test_assert(0 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 1;

   /* lock mtx1 */
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);

   test_assert(1 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 2;

   /* switch context to LM */
   os_sem_up(&test_sem[1]);

   /* we will return here when LM will lock on mtx1 */
   test_assert(4 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 5;
   /* also this means that L should now have p(LM) */
   test_assert(2 == task_worker[3].prio_current);

   /* switch context to LM */
   os_sem_up(&test_sem[0]);

   /* we will return here when H will lock on mtx1 */
   test_assert(6 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 7;
   /* also this means that L should now have p(H) and LM also should have p(H) */
   test_assert(4 == task_worker[2].prio_current);
   test_assert(4 == task_worker[3].prio_current);

   /* try to woke up the M but this should not cause context switch */
   os_sem_up(&test_sem[0]);

   test_assert(7 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 8;

   /* release mtx2, whis should cause switch to LM */
   os_mtx_unlock(&test_mtx[1]);

   /* we will return here when all task will finish */
   test_assert(13 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 14;
   /* also this means that L should now have original prio */
   test_assert(1 == task_worker[3].prio_current);

   return 0;
}

/**
 * Test scenario:
 * Recursive bounded priority inversion problem
 * Here we focus on os_unlock_mtx() implementation. We check if during call of
 * this function the task at the end of recursive locking chain gets the right
 * priority while prio_reset() operation.
 *
 * Task L locks mtx0 and mtx1
 * Task M locks mtx2 and mtx0
 * Task HM lock mtx1
 * Task H locks mtx2
 * Task L unlocks mtx1 (critical moment)
 * Test succeeds if during os_mtx_unlock() done by task L context is not
 * switched to task MH.
 */
int test_scen4_workerH(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow L to progress */
   ret = os_sem_down(&test_sem[2], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* we will be woken up from L */
   test_assert(7 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 8;

   /* lock mtx2, this will switch back to L */
   ret = os_mtx_lock(&test_mtx[2]);
   test_assert(0 == ret);

   /* we should switch from M */
   test_assert(12 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 13;

   /* prepare to finish task */
   os_mtx_unlock(&test_mtx[2]);
   test_assert(13 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 14;
   /* by finishing the task we should switch to HM */

   return 0;
}

int test_scen4_workerHM(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow L to progress */
   ret = os_sem_down(&test_sem[1], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* we will be woken up from L */
   test_assert(5 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 6;

   /* lock mtx1, this will switch back to L */
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);

   /* we should switch from H */
   test_assert(14 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 15;

   /* prepare to finish task (no ctx switch this time) */
   os_mtx_unlock(&test_mtx[1]);
   test_assert(15 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 16;
   /* by finishing the task we should switch to M */

   return 0;
}

int test_scen4_workerM(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow L to progress */
   ret = os_sem_down(&test_sem[0], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   /* we will return here from L */
   test_assert(2 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 3;

   /* lock mtx2 */
   ret = os_mtx_lock(&test_mtx[2]);
   test_assert(0 == ret);
   test_assert(3 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 4;

   /* lock mtx0 this will switch to L */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);

   /* we should switch from L */
   test_assert(10 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 11;
   /* the effective priority of this task should still be on H level */
   test_assert(4 == task_worker[2].prio_current);

   /* unlock mtx0, no context switch yet */
   os_mtx_unlock(&test_mtx[0]);
   test_assert(11 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 12;

   /* unlock mtx2, we should switch to H */
   os_mtx_unlock(&test_mtx[2]);

   /* we should return from HM */
   test_assert(16 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 17;

   /* prepare to finish task */
   /* by finishing the task we should switch to L */

   return 0;
}

int test_scen4_workerL(void* OS_UNUSED(param))
{
   int ret;

   test_assert(0 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 1;

   /* lock mtx0 and mtx1 */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);

   test_assert(1 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 2;

   /* switch context to M */
   os_sem_up(&test_sem[0]);

   /* we will return from M */
   test_assert(4 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 5;

   /* switch context to HM */
   os_sem_up(&test_sem[1]);

   /* we will return from HM */
   test_assert(6 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 7;

   /* switch context to H */
   os_sem_up(&test_sem[2]);

   /* we will return from H */
   test_assert(8 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 9;

   /* unlock mtx1, this should not cause switch to HM since we still holds mtx0
    * on which M is blocked which in turn holds mtx2 on which H is blocked. So
    * following call should not make context switch since L should remain on
    * boosted prio = p(H) */
   os_mtx_unlock(&test_mtx[1]);

   test_assert(9 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 10;
   /* the effective priority of this task should still be on H level */
   test_assert(4 == task_worker[3].prio_current);

   /* unlock mtx0, there should be context switch to M */
   os_mtx_unlock(&test_mtx[0]);

   /* we should switch from M */
   test_assert(17 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 18;

   return 0;
}

/**
 * Test scenario:
 * This is regression test. Typical scenario of priority inversion with single
 * mutex but L holds two of them (while only one is used for locking scheme).
 * BUG root cause was that part of implementation assumed that L could hold the
 * boosted prio until last mtx was unlocked. This in fact created priority
 * inversion if only one mtx preventing H from going forward.  Here we try to
 * lock H task on mtx owned by L which also owns other mtxs, then L unlocks the
 * mtx and H should preempt.
 *
 * L locks mtx0 and mtx1
 * H locks mtx1
 * L unlock mtx1 (critical moment)
 * We should see context switch to H even if L holds some other mtx
 * additional check is that M should not be scheduled until H exits
 */
int test_scen5_workerH(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow L to progress */
   ret = os_sem_down(&test_sem[1], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   test_assert(3 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 4;

   /* try to lock mtx0, we should switch to L */
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);

   /* we should now get mtx0 locked in L and we have mtx1 */
   test_assert(5 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 6;

   /* finish test we should switch to M */
   os_mtx_unlock(&test_mtx[1]);
   test_assert(6 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 7;

   return 0;
}

int test_scen5_workerM(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow L to progress */
   ret = os_sem_down(&test_sem[0], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   test_assert(2 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 3;

   /* switch context to H */
   os_sem_up(&test_sem[1]);

   /* finish test */
   test_assert(7 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 8;

   return 0;
}

int test_scen5_workerL(void* OS_UNUSED(param))
{
   int ret;

   test_assert(0 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 1;

   /* lock mtx0 and mtx1 */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);

   test_assert(1 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 2;

   /* switch context to M */
   os_sem_up(&test_sem[0]);

   /* we will return from H */
   test_assert(4 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 5;

   /* unlock mtx1. In properly implemented priority inversion this should wake
    * up the task H since nothing more block it from running */
   os_mtx_unlock(&test_mtx[1]);

   test_assert(8 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 9;

   /* finish test */
   os_mtx_unlock(&test_mtx[0]);
   test_assert(9 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 10;

   return 0;
}

/**
 * Test scenario:
 * This is the same scenario as in scen5 but here we use mtx1 instead of mtx0.
 * This makes situation where mutexes are unlocked in the same order as locking
 * order, which additionally checks is if OS does not force the unlock sequence.
 *
 * L locks mtx0 and mtx1
 * H locks mtx1
 * L unlock mtx0 (critical moment)
 * We should see context switch to H even if L holds some other mtx
 * IMPORTANT: unlocking mtx0 before mtx1 creates same unlock order as locking
 * order. Additional check is that M should not be scheduled until H exits
 */
int test_scen6_workerH(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow L to progress */
   ret = os_sem_down(&test_sem[1], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   test_assert(3 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 4;

   /* try to lock mtx0, we should switch to L */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);

   /* we should now get mtx0 locked in L and we have mtx1 */
   test_assert(5 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 6;

   /* finish test we should switch to M */
   os_mtx_unlock(&test_mtx[0]);
   test_assert(6 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 7;

   return 0;
}

int test_scen6_workerM(void* OS_UNUSED(param))
{
   int ret;

   /* block on sem, allow L to progress */
   ret = os_sem_down(&test_sem[0], OS_TIMEOUT_INFINITE);
   test_assert(0 == ret);

   test_assert(2 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 3;

   /* switch context to H */
   os_sem_up(&test_sem[1]);

   /* finish test */
   test_assert(7 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 8;

   return 0;
}

int test_scen6_workerL(void* OS_UNUSED(param))
{
   int ret;

   test_assert(0 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 1;

   /* lock mtx0 and mtx1 */
   ret = os_mtx_lock(&test_mtx[0]);
   test_assert(0 == ret);
   ret = os_mtx_lock(&test_mtx[1]);
   test_assert(0 == ret);

   test_assert(1 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 2;

   /* switch context to M */
   os_sem_up(&test_sem[0]);

   /* we will return from H */
   test_assert(4 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 5;

   /* unlock mtx0 we want to release mtx in the same order as we lock them (not
    * reverse order), in properly implemented priority inversion this should
    * wake up the task H since nothing more block it from running This is
    * allowed since mutex does not enforce any locking sequence. User should
    * prevent from making deadlock possible */
   os_mtx_unlock(&test_mtx[0]);

   test_assert(8 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 9;

   /* finish test */
   os_mtx_unlock(&test_mtx[1]);
   test_assert(9 == test_atomic[0]); /* verify the valid progress of test state */
   test_atomic[0] = 10;

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
         &task_worker[i], OS_CONFIG_PRIOCNT - 1 - i,
         task_stack[i], sizeof(task_stack[i]),
         scen3_worker_proc[i], (void*)(long)i);
   }
   /* scheduler will kick in after following call */
   for(i = 0; i < 4; i++)
   {
      os_task_join(&task_worker[i]);
   }
   test_assert(14 == test_atomic[0]);

/* scenario 4 */
   os_taskproc_t scen4_worker_proc[] = {
	   test_scen4_workerH,
	   test_scen4_workerHM,
	   test_scen4_workerM,
	   test_scen4_workerL
   };
   os_sem_create(&test_sem[0], 0);
   os_sem_create(&test_sem[1], 0);
   os_sem_create(&test_sem[2], 0);
   os_mtx_create(&test_mtx[0]);
   os_mtx_create(&test_mtx[1]);
   os_mtx_create(&test_mtx[2]);
   test_atomic[0] = 0;
   for(i = 0; i < 4; i++)
   {
      /* created task will be not scheduled because current task has the highest available priority */
      os_task_create(
         &task_worker[i], OS_CONFIG_PRIOCNT - 1 - i,
         task_stack[i], sizeof(task_stack[i]),
         scen4_worker_proc[i], (void*)(long)i);
   }
   /* scheduler will kick in after following call */
   for(i = 0; i < 4; i++)
   {
      os_task_join(&task_worker[i]);
   }
   test_assert(18 == test_atomic[0]);

/* scenario 5 */
   os_taskproc_t scen5_worker_proc[] = {
	   test_scen5_workerH,
	   test_scen5_workerM,
	   test_scen5_workerL
   };
   os_sem_create(&test_sem[0], 0);
   os_sem_create(&test_sem[1], 0);
   os_mtx_create(&test_mtx[0]);
   os_mtx_create(&test_mtx[1]);
   test_atomic[0] = 0;
   for(i = 0; i < 3; i++)
   {
      /* created task will be not scheduled because current task has the highest available priority */
      os_task_create(
         &task_worker[i], OS_CONFIG_PRIOCNT - 1 - i,
         task_stack[i], sizeof(task_stack[i]),
         scen5_worker_proc[i], (void*)(long)i);
   }
   /* scheduler will kick in after following call */
   for(i = 0; i < 3; i++)
   {
      os_task_join(&task_worker[i]);
   }
   test_assert(10 == test_atomic[0]);

/* scenario 6 */
   os_taskproc_t scen6_worker_proc[] = {
	   test_scen6_workerH,
	   test_scen6_workerM,
	   test_scen6_workerL
   };
   os_sem_create(&test_sem[0], 0);
   os_sem_create(&test_sem[1], 0);
   os_mtx_create(&test_mtx[0]);
   os_mtx_create(&test_mtx[1]);
   test_atomic[0] = 0;
   for(i = 0; i < 3; i++)
   {
      /* created task will be not scheduled because current task has the highest available priority */
      os_task_create(
         &task_worker[i], OS_CONFIG_PRIOCNT - 1 - i,
         task_stack[i], sizeof(task_stack[i]),
         scen6_worker_proc[i], (void*)(long)i);
   }
   /* scheduler will kick in after following call */
   for(i = 0; i < 3; i++)
   {
      os_task_join(&task_worker[i]);
   }
   test_assert(10 == test_atomic[0]);

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

