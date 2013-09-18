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

/* private function forward declarations */
static void os_sem_timerclbck(void* param);

void os_sem_create(os_sem_t* sem, os_atomic_t init_value)
{
   memset(sem, 0, sizeof(os_sem_t));
   os_taskqueue_init(&(sem->task_queue));
   sem->value = init_value;
}

void os_sem_destroy(os_sem_t* sem)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   arch_critical_enter(cristate);

   /* wake up all task which waits on sem->task_queue */
   while( NULL != (task = os_task_dequeue(&(sem->task_queue))) ) {

      /* \TODO FIXME missing timer destroy code ?!? */

      task->block_code = OS_DESTROYED;
      os_task_makeready(task);
   }
   /* finaly we destroy all semaphore data, this should arrise problems if this
    * semaphore was used also in interupts (feel warned) */
   memset(sem, 0, sizeof(os_sem_t));

   arch_critical_exit(cristate);
}

os_retcode_t OS_WARN_UNUSEDRET os_sem_down(os_sem_t* sem, uint_fast16_t timeout_ticks)
{
   os_retcode_t ret;
   os_timer_t timer;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* this function may be called only form user code */
   OS_ASSERT(task_current->prio_current > 0); /* idle task cannot call blocking
                                                 functions (will crash OS) */

   /* we need to disable the interrupts since semaphores may be signalized from ISR
       once we check the semahore value we need to add the task to the task queue in atomic maner
       also we use the timers here, we need to prevent the race conditions which may arrise
       if we will not protect from timer callbacks */
   arch_critical_enter(cristate);
   do
   {
      if( sem->value > 0 )
      {
         /* in case sem->value is not zero, we do not have to block, just to
          * consume one coin from sem */
         /* /TODO in future try to implement the "condition and decrement" as a
          * CAS operation and move it before critical section, this will
          * increase performance (something like os_atomic_cas() */
         --(sem->value);
         ret = OS_OK;
         break;
      }

      if( OS_TIMEOUT_TRY == timeout_ticks ) {
         ret = OS_WOULDBLOCK;
         break;
      }

       if( OS_TIMEOUT_INFINITE != timeout_ticks ) {
         os_timer_create(&timer, os_sem_timerclbck, task_current, timeout_ticks, 0);
         task_current->timer = &timer;
      }

      /* now block and change switch the context */
      os_block_andswitch(&(sem->task_queue), OS_TASKBLOCK_SEM);

      if( NULL != task_current->timer ) {
         /* seems that timer didn't exipre up to now, we need to clean it */
         task_current->timer = NULL;
         os_timer_destroy(&timer);
      }

      /* the block_code was set in os_sem_destroy, timer callback or in os_sem_up */
      ret = task_current->block_code;

   }while(0);
   arch_critical_exit(cristate);

   return ret;
}

/* this function can be called from ISR (one of the basic functionality of
 * semaphores) */
void os_sem_up_sync(os_sem_t* sem, bool sync)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   OS_ASSERT(sem->value < SIG_ATOMIC_MAX); /* check the semaphore value limit */

   /* \TODO implement nbr in function param so we can increase the semaphore
    * number more than once. To make it work in this way we have also to wake
    * multiple tasks. Best will be to craete loop around existing code and wake
    * up task untill cnt in this loop > 0, Example how to do it is in
    * waitqueue_wakeup */

   arch_critical_enter(cristate);
   task = os_task_dequeue(&(sem->task_queue));
   if( NULL == task )
   {
      /* there was no task which waits on sem, in this case increment the sem
       * value */
      ++(sem->value);
   }
   else
   {
      
      /* we need to destroy the timer here, because otherwise we will be
       * vulnerable for race conditions from timer callbacks (ISR) */
      os_timeout_destroy(task); 

      task->block_code = OS_OK; /* set the block code to NORMAL WAKEUP */
      os_task_makeready(task);

      /* do not call schedule() if we will do it in some other os function
       * call. This is marked by sync parameter flag */
      if(!sync) {
         /* switch to more prioritized READY task, if there is such (1 param in
          * os_schedule means switch to other READY task which has greater
          * priority) */
         os_schedule(1);
      }
   }
   arch_critical_exit(cristate);
}

/* --- private functions --- */

static void os_sem_timerclbck(void* param)
{
   os_task_t *task = (os_task_t*)param;

   /* timer was assigned only to one task, by timer param */
   os_task_unlink(task);
   task->block_code = OS_TIMEOUT;
   os_task_makeready(task);
   /* we do not call the os_sched here, because this will be done at the
    * os_tick() (which calls the os_timer_tick which call this function) */
   /* timer is not auto reload so we dont have to wory about it here (it will
    * not call this function again, also we can safely call os_timer_destroy
    * multiple times for such destroyed timer unles memory for timer structure
    * will not be invalidated*/
}

