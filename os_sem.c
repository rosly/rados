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
   os_taskqueue_init(&(sem->wait_queue));
   sem->value = init_value;
}

/* calling this function for semaphores which are also used in ISR is hghly forbiden
   since it will crash your kernel (ISR will access to data which is destroyed) */
void os_sem_destroy(os_sem_t* sem)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   arch_critical_enter(cristate);
   while( NULL != (task = os_task_dequeue(&(sem->wait_queue))) ) {
      task->block_code = OS_DESTROYED;
      os_task_makeready(task); /* wake up all task which waits on sem->wait_queue */
   }
   memset(sem, 0, sizeof(os_sem_t)); /* finaly we destroy all semaphore data, this should arrise problems if this semaphore was used also in interupts */
   arch_critical_exit(cristate);
}

os_retcode_t OS_WARN_UNUSEDRET os_sem_down(os_sem_t* sem, uint_fast16_t timeout_ticks)
{
   os_retcode_t ret;
   os_timer_t timer;
   os_task_t *task;
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* this function may be called only form user code */
   OS_ASSERT(task_current->prio_current > 0); /* idle task cannot cal blocking functions (will crash OS) */

   /* we need to disable the interrupts since semaphores may be signalized from ISR
       once we check the semahore value we need to add the task to the wait queue in atomic maner
       also we use the timers here, we need to prevent the race conditions which may arrise
       if we will not protect from timer callbacks */
   arch_critical_enter(cristate);
   do
   {
      if( sem->value > 0 )
      {
         /* in case sem->value is not zero, we do not have to block, just to consume one coin from sem */
         --(sem->value); /* /todo in future try to implement the condition and decrement as a cas operation, this will significantly increase throughput */
         ret = OS_OK;
         break;
      }

      if( OS_SEMTIMEOUT_TRY == timeout_ticks ) {
         ret = OS_WOULDBLOCK;
         break;
      }

       if( OS_SEMTIMEOUT_INFINITE != timeout_ticks ) {
         os_timer_create(&timer, os_sem_timerclbck, task_current, timeout_ticks, 0);
         task_current->timer = &timer;
      }

      os_task_makewait(&(sem->wait_queue), OS_TASKBLOCK_SEM);

      task = os_task_dequeue(&ready_queue); /* chose any ready task to whoh we can switch */
      arch_context_switch(task); /* here we switch to any READY task (possibly enable the interrupts after context switch) */
      /* the task state is set to RUNING internaly before return from arch_context_switch, also iterrupts are again disabled here (even it they was enabled for execution of previous task) */

      if( NULL != task_current->timer ) {
         task_current->timer = NULL;
         os_timer_destroy(&timer); /* this will remove timer if it does not expire up to now (internaly this function checks the required condition) */
      }

      ret = task_current->block_code; /* the block_code was set in os_sem_destroy, timer callback or in os_sem_up */

   }while(0);
   arch_critical_exit(cristate);

   return ret;
}

   /* this function can be called from ISR (one of the basic functionality of semaphores) */
void os_sem_up(os_sem_t* sem)
{
   arch_criticalstate_t cristate;
   os_task_t *task;

   OS_ASSERT(sem->value < SIG_ATOMIC_MAX); /* check the semaphore value limit */

   arch_critical_enter(cristate);
   task = os_task_dequeue(&(sem->wait_queue));
   if( NULL == task )
   {
      ++(sem->value); /* there was no task which waits on sem, in this case increment the sem value */
   }
   else
   {
      if( task->timer ) /* check if task waits for semaphore with timeout */
      {
         /* we need to destroy the timer here, because otherwise we will be wournable for race conditions from timer callbacks (curently callback are blocked becose of critical section, but we will posibly jump out of it while we will switch the tasks durring os_shedule) */
         os_timer_destroy(task->timer);
         task->timer = NULL; /* protect from double timer destroy in os_sem_down (that code will be executed when task will be scheduled again) */
      }

      task->block_code = OS_OK; /* set the block code to NORMAL WAKEUP */
      os_task_makeready(task);
      os_schedule(1); /* switch to more prioritized READY task, if there is such (1 param in os_schedule means switch to other READY task which has greater priority) */
   }
   arch_critical_exit(cristate);
}

/* --- private functions --- */

static void os_sem_timerclbck(void* param)
{
   os_task_t *task = (os_task_t*)param;

   os_task_unlink(task); /* unlink the task from semaphore wait queue */

   task->block_code = OS_TIMEOUT;
   os_task_makeready(task);
   /* we do not call the os_sched here, because this will be done at the os_tick() (which calls the os_timer_tick which call this function) */

   /* timer is not auto reload so we dont have to wory about it here (it will not call this function again, also we can safely call os_timer_destroy in any time for timer assosiated with this task) */
}

