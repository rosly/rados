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

#include "os_private.h"

/* --- private functions --- */

/**
 * Assign mutex ownership to given task
 * This is equivalent to locking the mutex. Initial recursive lock value is set
 * to 1.
 */
static inline void os_mtx_set_owner(os_mtx_t *mtx, os_task_t *task)
{
   mtx->owner = task;
   /* add this mtx to task owned list,
    * required for prio recalculation during os_mtx_unlock() */
   list_append(&(task->mtx_list), &(mtx->listh));
   mtx->recur = 1; /* initial recursion lvl (>1 means locked, 0 means unlocked) */
}

/**
 * Clear ownership of mutex.
 * This is equivalent to unlocking the mutex. Note that recursive lock is not
 * modified here, this need to be done earlier and only if this value is 0
 * os_mtx_clear_owner() may be called */
static inline void os_mtx_clear_owner(os_mtx_t *mtx)
{
   mtx->owner = NULL;
   list_unlink(&(mtx->listh));
}

#ifdef OS_CONFIG_MUTEX_PRIO_INHERITANCE
/**
 * Task priority inheritance function for mutex locking.
 * This function evaluates the recursive lock dependency between tasks.
 */
static void os_mtx_lock_prio_boost(os_mtx_t *mtx)
{
  os_task_t *task = mtx->owner;
  const uint_fast8_t task_current_prio = task_current->prio_current;

   /* check for priority inversion precondition */
   if (task->prio_current < task_current_prio)
   {
      while (1) /* why we use loop here ? see comment 2 */
      {
         uint_fast8_t prio_new;

         /* boost the prio of task which hold mtx */
         prio_new = os_max(task_current->prio_current, task_current_prio);
         os_task_reprio(task, prio_new);

         /* in case such task is also blocked on mtx, go down into the
          * blocking chain boost the prio of their blockers */
         if ((TASKSTATE_WAIT != task->state) ||
             (OS_TASKBLOCK_MTX != task->block_type))
         {
            break;
         }
         /* because (OS_TASKBLOCK_MTX == task->block_type), task->task_queue
          * points into os_mtx_t->task_queue */
         task = os_container_of(
            task->task_queue, os_mtx_t, task_queue)->owner;
      }
   }
}

/**
 * Task priority inheritance function for mutex unlocking.
 * This function reset the priority of task_current to proper level according to
 * priority inheritance rules. This prevents priority inversion to happen
 * even when indirect recursive lock dependency is still present
 */
static void os_mtx_unlock_prio_reset(void)
{
   if (task_current->prio_current != task_current->prio_base)
   {
      /* calculation of new priority is quite complicated since we may have been
       * boosted because:
       * 1) some nested dependency
       * 2) another dependency by different mtx that this task owns
       * We need to iterate over all mtx held by this task and new prio will
       * be the max(p(wating_task)). Keep in mind that we use prio_current (not
       * prio_base) since we lock/unlock one mtx at the time and if the waiting
       * task has already boosted priority by another dependency chain than we
       * need to reprio basing on that (not prio_base of the task) */
      os_task_t *task;
      list_t *itr;
      os_mtx_t *itr_mtx;
      uint_fast8_t prio_new;

      /* new prio will be not less than prio_base of the task */
      prio_new = task_current->prio_base;

      /* iterate owner mtx list which this task owns */
      itr = list_itr_begin(&(task_current->mtx_list));
      while (false == list_itr_end(&(task_current->mtx_list), itr))
      {
         itr_mtx = os_container_of(itr, os_mtx_t, listh);
         /* peek (not dequeue) top prio task that is suspended on this mtx */
         task = os_task_peekqueue(&(itr_mtx->task_queue));
         if (task)
         {
            /* os_max() means take bigger from two. It is important that we use
             * task->prio_current not task->prio_base since we would like to
             * include nested lock dependency. In other words if some task has
             * inherited priority due its lock dependency and is still suspended
             * on mtx which we own, we would like to also inherit this boosted
             * priority (not only inherit the base priority of this task) */
            prio_new = os_max(prio_new, task->prio_current);
         }
         itr = itr->next; /* advance to next mtx on list */
      }

      /* apply newly calculated prio
       * since task_current is RUNNING we can just modify prio_current */
      task_current->prio_current = prio_new;
   }
}
#endif

/* --- public functions --- */
/* all public functions are documented in os_mtx.h file */

void os_mtx_create(os_mtx_t* mtx)
{
   OS_ASSERT(0 == isr_nesting); /* cannot operate on mtx from ISR */
   OS_ASSERT(NULL == waitqueue_current); /* cannot call after os_waitqueue_prepare() */

   memset(mtx, 0, sizeof(os_mtx_t));
   os_taskqueue_init(&(mtx->task_queue));
}

void os_mtx_destroy(os_mtx_t* mtx)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   OS_ASSERT(0 == isr_nesting); /* cannot operate on mtx from ISR */
   OS_ASSERT(NULL == waitqueue_current); /* cannot call after os_waitqueue_prepare() */

   arch_critical_enter(cristate);

   /* check if mtx is locked */
   if (NULL != mtx->owner)
   {
      /* in case mutex was locked, than only owner can destroy it */
      OS_ASSERT(mtx->owner == task_current);

      /* set the mtx state as unlocked (remove ownership) */
      os_mtx_clear_owner(mtx);

#ifdef OS_CONFIG_MUTEX_PRIO_INHERITANCE
      /* recalculate the prio of owner */
      os_mtx_unlock_prio_reset();
#endif

      /* wake up all tasks from mtx->task_queue */
      while (NULL != (task = os_task_dequeue(&(mtx->task_queue))))
      {
         task->block_code = OS_DESTROYED;
         os_task_makeready(task);
      }
   }

   memset(mtx, 0, sizeof(os_mtx_t)); /* finally deface mtx data */
   os_schedule(1); /* schedule to make possible context switch after we woke up tasks */

   arch_critical_exit(cristate);
}

os_retcode_t OS_WARN_UNUSEDRET os_mtx_lock(os_mtx_t* mtx)
{
   os_retcode_t ret;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* cannot operate on mtx from ISR */
   OS_ASSERT(task_current != &task_idle); /* IDLE task cannot call blocking functions (will crash OS) */
   OS_ASSERT(NULL == waitqueue_current); /* cannot call after os_waitqueue_prepare() */

   arch_critical_enter(cristate);
   do
   {
      /** \TODO there is a race condition of using mute after destroy
        * maybe we should check fo mutex initialization status here and return
        * proper code in this case */

      if (NULL == mtx->owner)
      {
         /* mutex unlocked, lock and take ownership */
         os_mtx_set_owner(mtx, task_current);
         ret = OS_OK;
         break;
      }

      /* mutex is locked, check if locked by current task */
      if (mtx->owner == task_current)
      {
    /* current task is the owner, just increase the recursion level */
         ++(mtx->recur);
         ret = OS_OK;
         break;
      }

#ifdef OS_CONFIG_MUTEX_PRIO_INHERITANCE
      /* mtx locked/owned by other task, boost the prio of owner if it has lower
       * prio than current task */
      os_mtx_lock_prio_boost(mtx);
#endif

      /* block the current task and switch context to READY task */
      os_block_andswitch(&(mtx->task_queue), OS_TASKBLOCK_MTX);

      /* we will return from previous call when os_mtx_unlock() would be
       * performed by other task. Now we are the owner of this mtx. The
       * ownership and the block_code have been set in os_mtx_destroy() or in
       * os_mtx_unlock() by other task */
      ret = task_current->block_code;

   } while (0);
   arch_critical_exit(cristate);

   return ret;
}

void os_mtx_unlock(os_mtx_t* mtx)
{
   os_task_t *task;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* cannot operate on mtx from ISR */
   OS_ASSERT(mtx->owner == task_current);/* only owner can unlock the mutex */
   OS_ASSERT(NULL == waitqueue_current); /* cannot call after os_waitqueue_prepare() */

   arch_critical_enter(cristate);
   do
   {
      /* for recursive lock decrease the recursion level
       * break if still in recursion */
      if ((--(mtx->recur)) > 0)
         break;

      /* mtx not locked anymore, set the mtx state as unlocked (remove ownership) */
      os_mtx_clear_owner(mtx);

#ifdef OS_CONFIG_MUTEX_PRIO_INHERITANCE
      /* before os_schedule we need to check if task_current does not have the
       * priority boosted and revert it to original priority if needed */
      os_mtx_unlock_prio_reset();
#endif

      /* since we unlocking the mtx we need to transfer the ownership to top
       * prio task which sleeps on this mtx. See comment 1 */
      task = os_task_dequeue(&(mtx->task_queue));
      if (NULL != task)
      {
         os_mtx_set_owner(mtx, task); /* lock and set ownership */
         task->block_code = OS_OK; /* set the block code to NORMAL WAKEUP */
         os_task_makeready(task);
         os_schedule(1); /* switch to ready task only when it has higher prio (1
                            as param of os_schedule(0 means just that) */
      }
   } while (0);
   arch_critical_exit(cristate);
}

/* Comment 1
 * There are two possible solution of task wake up:
 * 1) Only single (top prio) task is woken up when mtx is unlocked.
 * 2) Woke up all of the tasks which sleep on mtx and the top prio will run as
 *    first. Remain will be allowed to run after the all higher prio will block
 *    or give back the CPU.
 * In first solution we have only one (possible) context switch and FIFO order
 * of sleeping task is preserved in wait queue of mtx. Second approach requires
 * the use of spin loop and creates multiple context switches from which only
 * one will succeed in locking the mutex (other task will simply spin within
 * WAIT->READY->WAIT cycle). While second approach also can change the
 * "wait/woke-up" order of tasks (new tasks out of initial wait-set will be
 * spread across wait queue while woken up tasks will spin) it has better
 * anti-starvation characteristics. In first solution we always woke up the top
 * prio task which may lead to starvation of low prio task.  Since this is RTOS
 * we assumed that user will be aware of starvation possibility and since
 * solution 1 has more conservative and predictable approach it was decided to
 * use it over solution 2. */

 /* Comment 2
  * To justify the used approach lets first what is the priority inversion:
  *
  * In following example there is no other mechanism than static priority
  * scheduling and there are three task named L, M and H.
  * H - high priority task
  * M - medium priority task
  * L - low priority task
  *
  * Time line for priority inversion
  *          t2
  * H        +----+    t4
  * M    t1  |    |    +---->
  * L  ------+    +----+
  *               t3     time ->
  *
  * If H attempts to acquire mtx (at t2) after L has acquired it (at t1), then H
  * becomes blocked (at t3) until L relinquishes the mtx. Typically L will
  * promptly relinquish the mtx so that H will not be blocked for extensive
  * period of time. But it may also happen that task M will will become runnable
  * (at t4) and it can run very long which will prevent L from from relinquish
  * mtx promptly (since priority of M > priority of L) and allow H to lock it
  * and do its (high priority) job.
  *
  * Priority inheritance can be used to overcome priority inversion. In
  * following description p(t) means priority of task t.  For previous scenario
  * it will be enough that p(L) will be temporally (at t3) boosted to p(H) since
  * we must prevent M from preempting L (at t4). So priority inheritance can be
  * written as p(owner) = max(p(waiters)) which imply p(L) = p(H) in our case.
  *
  * Time line for priority inheritance
  *          t2   boost p(L)
  * H        +----+         +----+
  * M    t1  |    |         |    +---->
  * L  ------+    +====+====+
  *               t3   t4 (no preemption)
  *
  * But implementation of priority inheritance must also take following case
  * into account. Let add one more task:
  * LM - less than medium priority task (p(M) > p(LM) > p(L))
  * and two mutexes instead of one, mtx1 and mtx2 (this is important
  * difference). To clarify the deadlock possibility lets assume that if any of
  * those task would need to lock both mutexts than they do that in the same
  * order, so mtx1 is locked as first than mtx2 as second.
  * Task L locks only mtx2
  * Task LN locks both mtx1 and mtx2
  * Task H locks only mtx1
  * Task M does not lock any mtx
  *
  * Timeline for priority inversion with simple prio boost
  *                        t3 (boost of p(LM) = p(H))
  * H                 +----+
  * M  t1.1 t1.2 t2   |    |  +----->
  * LM      +----+    |    |  |
  * L  -----+    +====+    +==+
  *             boost p(L)    t4
  *               time ->
  * Situation is following:
  * L acquires mtx2 (at t1.1). LM acquires mtx1 (at t1.2). When LM will try to
  * acquire mtx2 (at t2) than as in previous scenario prio inheritance will
  * boost priority of L so p(L) = p(LM) and L will preempt and run.  Now, if H
  * wakes up and tries to lock mtx1 (at t3) it will cause priority inheritance
  * to owner of mtx1 which is LM, so p(LM) = p(H). But since LM is blocked on
  * mtx2 held by L we will see preemption to L (not LM). Up to now everything
  * seems to be OK, L will continue to run at p(LM) and LM will wait for mtx2.
  * But in case M become runnable (at t4) it will prevent L from promptly
  * relinquish of mtx1. Even if p(LM) = p(H) LM is not allowed to run since it
  * is still blocked on mtx2 while M prevents L from relinquish mtx1 (not mtx2).
  * So M will be allowed to run as long as it would like and it blocks execution
  * of all tasks L, LM and even H.
  *
  * To overcome this problem our simple priority inheritance rule that p(owner)
  * = max(p(waiters)) must be evaluated recursively for each owner of mutex
  * within blocking chain.
  * Properly implemented priority inheritance must do following pseudo code.
  *
  * if curr_task tries to lock mtx then
  * do {
  *    owner = mtx->owner;
  *    if (owner->current_prio > curr_task->current_prio) {
  *       owner->current_prio = curr_task->current_prio;
  *       if (owner->state == WAIT_FOR_MTX) {
  *          mtx = owner->mtx_to_wait_for;
  *          continue;
  *       }
  *       break;
  *   }
  * }
  *
  * Timeline for priority inversion with recursive prio boosting
  *                        t3 (boost of p(LM) = p(L) = p(H))
  * H                 +----+       +----+
  * M  t1.1 t1.2 t2   |    |       |    +---->
  * LM      +----+    |    |   +===+
  * L  -----+    +====+    +===+
  *             boost p(L)    t4
  *               time ->
  *
  * As a side discussion this also shows why "you cannot sleep in two or more
  * beds". In such case we would have not only make vertical walk through, but
  * also horizontal which would be a pain in the ... BTW is that the reason why
  * Windows is using kind of random priority boosting ?
  * https://msdn.microsoft.com/en-us/library/windows/desktop/ms684831(v=vs.85).aspx
  * Why its kernel supports ideas like WaitForMultipleObjects() call ?
  * */

