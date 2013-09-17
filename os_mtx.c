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

#include "os_private.h"

static inline void os_mtx_setowner(os_mtx_t *mtx, os_task_t *task)
{
   mtx->owner = task;
   /* this list link allows to place mtx on owner mtx list, this is required for
    * letter prio_current calculation durring the os_mtx_unlock */
   list_append(&(task->mtx_list), &(mtx->listh));
   /* initialize the recursion lvl (1 means locked mtx, 0 means unlock */
   mtx->recur = 1;
}

static inline void os_mtx_noowner(os_mtx_t *mtx)
{
   mtx->owner = NULL;
   list_unlink(&(mtx->listh));
}

void os_mtx_create(os_mtx_t* mtx)
{
   OS_ASSERT(0 == isr_nesting); /* mutex cannot be operated from ISR */

   memset(mtx, 0, sizeof(os_mtx_t));
   os_taskqueue_init(&(mtx->task_queue));
}

void os_mtx_destroy(os_mtx_t* mtx)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   OS_ASSERT(0 == isr_nesting); /* mutex cannot be operated from ISR */

   arch_critical_enter(cristate);
   while( NULL != (task = os_task_dequeue(&(mtx->task_queue))) ) {
      task->block_code = OS_DESTROYED;
      os_task_makeready(task); /* wake up all task which waits on mtx->task_queue */
   }
   memset(mtx, 0, sizeof(os_mtx_t)); /* finaly we obstruct mtx data */
   arch_critical_exit(cristate);
}

os_retcode_t OS_WARN_UNUSEDRET os_mtx_lock(os_mtx_t* mtx)
{
   os_retcode_t ret;
   os_task_t *task;
   arch_criticalstate_t cristate;

   /* mutex cannot be operated from ISR */
   OS_ASSERT(0 == isr_nesting);
   /* idle task cannot cal blocking functions (will crash OS) */
   OS_ASSERT(task_current->prio_current > 0);

   /* while mutex cannot be accessed from ISR (not resonable) we still need to
    * protect the execution from preemption
      preemption can switch tasks and enter/access the same mtx */
   arch_critical_enter(cristate);
   do
   {
      if( NULL == mtx->owner )
      {
         /* in case nobody owns the mutex we just take the ownership */
         os_mtx_setowner(mtx, task_current);
         ret = OS_OK;
         break;
      }

      /* here we know that mutex is already locked, hek if mutex is locked by
       * current task */
      if( mtx->owner == task_current )
      {
         /* in case the current task is the owner, we juste increase the
          * recursion level */
         ++(mtx->recur);
         ret = OS_OK;
         break;
      }

      /* here we know that mutex is locked by other task, we will have to wait
       * for mutex unlock */

      /* but first check for priority inversion, and in case the task that owns
       * the mutex has lower priotity, perform the priority boost */
      if( mtx->owner->prio_current < task_current->prio_current )
      {
         /* here we have to make a iteration through blocking chain to implement
          * the priority propagation. Task are prioritized from most important
          * to less important (for eg. A, B, C). Lets imagine that B blocks on
          * mtx allready owned by C, this is the simple case when prio will be
          * bosted to B-level and scheduler will continue to execute C.  But
          * then lets also assume, that some ISR will signalize the semaphore
          * which wake up the A, then A will block on another mtx allready owned
          * by B.  In this case we will have to inrease priority of both tasks B
          * and C to allow the execution of preempted C then B and finaly A */

         task = mtx->owner;
         while(1) {
            /* boost the prio of each task in chain, so when we call
             * os_task_makewait we will switch to task which blocks the current
             * one */
            task->prio_current = os_max(task_current->prio_current,
                                        task->prio_current);

            /* next in case this task is blocked on mtx we need also increase
             * the prio of task which blocks it (which owns the mtx on which the
             * task is blocked) */
            if( (TASKSTATE_WAIT == task->state) &&
                (OS_TASKBLOCK_MTX == task->block_type) )
            {
               /* here we fist get mtx pointer by using of os_container_of macro
                * with task_queue pointer (we know that this is the task_queue
                * of the mutex because (OS_TASKBLOCK_MTX == task->block_type)),
                * then we get the owner task of that mtx */
               task = os_container_of(task->task_queue, os_mtx_t, task_queue)->owner;
               continue;
            }
            break;
         }
      }

      /* now block and change switch the context */
      os_block_andswitch(&(mtx->task_queue), OS_TASKBLOCK_MTX);
      /* The return from previus call is trigered by os_mtx_unlock performed by
       * other task in tos_mtx_unlock the os_mtx_setowner is called, so here we
       * are new owner of the mutex please read also comment2 below in this file
       * */

      /* the block_code was set in os_mtx_destroy or in os_mtx_unlock */
      ret = task_current->block_code;

   }while(0);
   arch_critical_exit(cristate);

   return ret;
}

void os_mtx_unlock(os_mtx_t* mtx)
{
   os_task_t *task;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* mutex cannot be operated from ISR */
   OS_ASSERT(mtx->owner == task_current);/* only owner can unlock the mutex */

   arch_critical_enter(cristate);
   do
   {
      /* check if mtx was not recursively locked
         just decrease the recursion level and break if still in recursion */
      if( (--(mtx->recur)) > 0 )
         break;

      /* now we know that this is the real unlock, set the mtx state as unlocked */
      os_mtx_noowner(mtx);

      /* before os_schedule we need to check if current_task does not have the
       * priority boosted.  If yes the we need to recalculate the priority, this
       * is quite complicated since we may have been boosted because of some
       * nested dependency.  For more info look at prio boosting code in
       * os_mtx_lock */
      if( task_current->prio_current != task_current->prio_base )
      {
         list_t *itr;
         os_mtx_t *itr_mtx;
         uint_fast8_t prio_new;

         /* we need to calculate a new taks priority.  This will be the prio of
          * most important task from those that waits on ay mtx stil owned by
          * task_current. So we need to traverse through the owned mtxes and
          * calculate the os_maximal task prio.  Please see additional coment1
          * at the end of this file */

         prio_new = task_current->prio_base; /* new prio will be not less than prio_base of the task */
         itr = list_itr_begin(&(task_current->mtx_list)); /* start iteration from begin of task_current mutex list */
         while( false == list_itr_end(&(task_current->mtx_list), itr) ) /* until the end of this list */
         {
            itr_mtx = os_container_of(itr, os_mtx_t, listh); /* calculate the pointeto mtx from list link (should be the same (operation removed by compilation optimizer) if listh is on begin of mtx structure) */
            task = os_task_peekqueue(&(itr_mtx->task_queue)); /* peek the most prioritized task from task queue of the mutex which we currently consider durring iteration (just peed not dequeue) */
            prio_new = os_max(prio_new, task->prio_current); /* take the os_maximum from already calculated prio_new and most priotitized task from paticular mtx, use prio_current instead of prio_base because this blocked task may have some dependency (same story as in os_mutex_lock) */
            itr = itr->next; /* go forward in the iteration, switch to next mtx on list */
         }

         task_current->prio_current = prio_new; /* apply newly calculated prio, now we can make os_schedule which will perform as we expect */
      }

      /* here we can transfer the mutex ownership to most prioritized task that
       * wait on mutex task queue. As usualy waitqueue works in fifo order, this
       * guarantie fair scheduling sequence for task with the same priority.  We
       * need to assign the ownership here because after swithich task to READY
       * state it can (depending on prioirity) preempt the curent task (done by
       * os_scheule(1)).  In other words before we call os_schedule we have to
       * set a new owner ot mutex */
      task = os_task_dequeue(&(mtx->task_queue));
      if( NULL != task )
      {
         os_mtx_setowner(mtx, task);
         task->block_code = OS_OK; /* set the block code to NORMAL WAKEUP */
         os_task_makeready(task);
         /* switch to more prioritized READY task, if there is such (1 param in
          * os_schedule means switch to other READY task which has greater
          * priority) */
         os_schedule(1);
      }
   }while(0);
   arch_critical_exit(cristate);
}

/* comment1:
   We realy have to calculate the new prio by scanning a task queues of mtx'es
   which is owned by current task. Another option which may be considered it to
   drop the prio to base if the new task which will be dequed because of unlock
   (look for task = os_task_dequeue(&(mtx->task_queue));) has the same prio as
   the boosted current one. But this solution has following bug:

   - lest imagine task sorted from most important to less important A, B, C
   - task A and B is blocked on condition signalized from ISR while C is running
   - lets assume that task C takes two mtx (M1 and M2),
   - then ISR signalizes the condition on which B was blocked, task B tries to
     lock M2, this will involve prio boost of task C to level of taskB, task B is
     going to block
   - then ISR signalizes the condition on which A was blocked, task A tries to
     lock M1, this will involve prio boost of task C to level of taskA, task A is
     going to block
   - C finishes the operation under M2 and unlock it, in this case we dont lower
     the prio of task C to some orginal lvl because task which was waken (B) has
     lower prio than the current task C
   - this is good because we still blocking A (which is more important that just
     unblocked B) and we have to do everything that wil allow A to continue (we
     need to unlock M1 so we continue the execution of C)
   - when C finishes operation under M1 it will unlock it, unblock the A and in
     this case the prio_current is the same as unblocked task, so C will lower its
     prio to orginal C level

   but lets consider that we unlock the M1 first
   then C will drop its prio to orginal C level, (because waked A has the same prio as current_prio of C) and task A will be sheduled
   when A will block again on some condition, then C should be sheduled to continue operation under M2 (because it still block B while this is the only task which is READY (it was just preempted by A))
   but here the problem apears apears, if we imagine that yet additional task D is considered which has the same base prio as C
   in this case C will work to finish operation under M2 (which holds the more prioritized B while C already droped its boosted prio) and bang!! the preemption may kick in
   if preemption will switch to D we waste time, because we should finish operation under M2 for which more prioritized B waits, not swiching to some low priority task (such as D) */

/* comment2
   Mutex ownership is transfered betwen task durring os_mtx_unlock
   this sollution guaraties the fifo characteristic of mutex waitqueue because we directly pass the ownerhip to most prioritized task
   in contrast we could also wake up all tasks which waits in waitqueue while os_mtx_lock will use the lock loop
   this loop will held task in READY->WAIT->READY state switch chain until it will be the first task that check the mutex ownership condition
   this solution does not pserve the fifo characteristic and because of that we dont use it */

