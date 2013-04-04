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

/* will be truncanced to register size */
#define OS_STACK_FILLPATERN ((uint8_t)0xAB)

os_task_t *task_current = NULL; /* not need to be volatile since we whant to optimize the access to it, while it only may change after os_schedule call, compiler asumes that after function call all data under the pointer may be changed (also ISR will not change it since interrupts are blocked by critical sections) */
os_taskqueue_t ready_queue;
volatile os_atomic_t isr_nesting = 0;
volatile os_atomic_t sched_lock = 0;
static os_task_t task_idle;

/* private function forward declarations */
static void arch_task_debug(os_task_t *task, void* stack, size_t stack_size);
static void os_task_init(os_task_t* task, uint_fast8_t prio);

/* priod and in time of this function call, all interrupts must be disabled */
void os_start(
   os_initproc_t app_init,
   os_initproc_t app_idle)
{
   /* disable the interrupts durrint the os initialization */
   arch_dint();

   /* initialize the remain os variables */
   os_taskqueue_init(&ready_queue);
   os_timers_init();

   /* create and switch to idle task, perform the remain app_init on idle_task */
   os_task_init(&task_idle, 0);
   task_idle.state = TASKSTATE_RUNNING;
   task_current = &task_idle; /* interrupt are not enabled, we can safely done this without ritical section */
   /* from this point we can switch the context because we have at least task_idle in ready_queue, but for fast app_init we disable the scheduler until we will be fully ready */

   arch_os_start();

   os_scheduler_lock(); /* disable the scheduler, so app_init will not switch for task which it will eventualy create */
   app_init(); /* here app should create the remaining threads and start the interrupts (not only the tick) */
   os_scheduler_unlock();
   arch_eint(); /* enable the interrupts */

   do
   {
      arch_criticalstate_t cristate;

      arch_critical_enter(cristate);
      os_task_makeready(task_current); /* task_current means task_idle here */
      arch_context_switch(os_task_dequeue(&ready_queue)); /* finaly we switch the context to first user task, (it will have the higher ptiority than idle) (need to be done under critical section) */
      arch_critical_exit(cristate);
   }while(0);

   while(1)
   {
      app_idle();
   }
}

void os_task_create(
   os_task_t *task,
   uint_fast8_t prio,
   void *stack,
   size_t stack_size,
   os_taskproc_t proc,
   void* param)
{
   arch_criticalstate_t cristate;

   OS_ASSERT(0 == isr_nesting); /* this function may be called only form user code */
   OS_ASSERT(prio < OS_CONFIG_PRIOCNT);
   OS_ASSERT(prio > 0); /* only idle task may have the prio 0 */
   OS_ASSERT(NULL != stack);
   OS_ASSERT(stack_size >= OS_STACK_MINSIZE);

   os_task_init(task, prio);

   arch_task_debug(task, stack, stack_size);
   arch_task_init(task, stack, stack_size, proc, param);

   arch_critical_enter(cristate);
   os_task_enqueue(&ready_queue, task);
   arch_critical_exit(cristate);

   os_schedule(1); /* 1 as a param will couse a task switch only if created task has higher priority than task_curernt */
}

int os_task_join(os_task_t *task)
{
   arch_criticalstate_t cristate;
   os_sem_t join_sem;
   int ret;

   OS_ASSERT(task_current->prio_current > 0); /* idle task cannot call blocking functions (will crash OS) */

   arch_critical_enter(cristate);
   OS_ASSERT(NULL == task->join_sem); /* only single task can wait for another task */
   OS_ASSERT(TASKSTATE_INVALID != task->state); /* task can be joined only once */
   if(  task->state < TASKSTATE_DESTROYED )
   {
      os_sem_create(&join_sem, 0); /* initialize the sem on which we will block */
      task->join_sem = &join_sem;

      /* here we should wait on task exit semaphore */
      ret = os_sem_down(&join_sem, OS_SEMTIMEOUT_INFINITE);
      OS_ASSERT(OS_OK == ret);
      os_sem_destroy(&join_sem);
   }
   OS_ASSERT(TASKSTATE_DESTROYED == task->state); /* now the task state has to be DESTROYED */
   task->state = TASKSTATE_INVALID; /* mark that task is already joined */
   task->join_sem = NULL;
   arch_critical_exit(cristate);

   return task->ret_value;
}

void OS_HOT os_tick(void)
{
   OS_ASSERT(isr_nesting > 0); /* this function may be called only form ISR */

   os_timer_tick(); /* call the timer module mechanism */
   os_schedule(0); /* switch to other READY task, if there is such (0 param in os_schedule means switch to other READY task which has the same or greater priority) */
}

/* --- private functions --- */

void OS_HOT os_task_enqueue(os_taskqueue_t* restrict task_queue, os_task_t* restrict task)
{
   list_append(&(task_queue->tasks[task->prio_current]), &(task->list));
   task->task_queue = task_queue;
   if( task_queue->priomax < task->prio_current ) {
     task_queue->priomax = task->prio_current;
   }
}

/**
 *  Function realculate the priomax inside task_queue after task unlink operation
 *  Function does not unlink the task! this must be done before calling this function
 */
void OS_HOT os_task_queue_reprio(os_taskqueue_t* restrict task_queue)
{
   while( (0 != (task_queue->priomax)) && list_is_empty(&(task_queue->tasks[task_queue->priomax])) ) {
     --(task_queue->priomax);
   }
}

/**
 *  Function unlink the task from task_queue
 *  This is desierd function which should be called when we need to switch to explicitly pointed task from wait_queues
 */
void OS_HOT os_task_unlink(os_task_t* restrict task)
{
   list_unlink(&(task->list));
   os_task_queue_reprio(task->task_queue);
   task->task_queue = NULL;
}

/**
 *  Function dequeues the most urgent task from the wait_queue
 *  This is desierd function which should be called when we need to switch to other but most urgent task
 */
os_task_t* OS_HOT os_task_dequeue(os_taskqueue_t* restrict task_queue)
{
   list_t *list = list_detachfirst(&(task_queue->tasks[task_queue->priomax]));
   if( NULL == list ) {
      return NULL; /* this may happend on mutex, semaphore wait list, but not for ready_queue */
   }
   os_task_t *task = os_container_of(list, os_task_t, list);
   os_task_queue_reprio(task_queue);
   task->task_queue = NULL;
   return task;
}

os_task_t* OS_HOT os_task_dequeue_prio(os_taskqueue_t* restrict task_queue, uint_fast8_t prio)
{
   if( task_queue->priomax >= prio ) { /* dequeue only if there si a task with prio equal or greater than the prio param */
      return os_task_dequeue(task_queue);
   }
   return NULL;
}

/**
 *  Functionreturns the pointer to most urgent task on the wait_queue
 *  This function does not modify the wait_queue, it just returns the reference
 *  Use this function olny when you are intrested about some property of most prioritized task on the queue
 */
os_task_t* OS_HOT os_task_peekqueue(os_taskqueue_t* restrict task_queue)
{
   list_t *list = list_peekfirst(&(task_queue->tasks[task_queue->priomax]));
   if( NULL == list ) {
      return NULL; /* this may happend on mutex, semaphore wait list, but not for ready_queue */
   }
   os_task_t *task = os_container_of(list, os_task_t, list);
   return task;
}

void os_taskqueue_init(os_taskqueue_t *task_queue)
{
   size_t i;

   for(i = 0; i < os_element_cnt(task_queue->tasks); i++) {
      list_init(&(task_queue->tasks[i]));
   }
   task_queue->priomax = 0;
}

/* This function chose the new task which will be run. THe task is chosen based on task_current priority and higher_prio param.
   higher_prio param is added to task_current priority, and if top prioritied READY task has priority greater or equal to this value it is chosen as a new task_current.
    - in case of user code it will switch the context to new task
    - in case of interrupt (nesting level == 1) it will not swich the context but only change the task_current pointer (so context switch can be done at the end of ISR)
    - in case of interrupt (nesting level > 1) do nothing at all (not even saerch for READY task), this is because only most bottom ISR at the nesting scenario can switch the tasks (otherwise we will get into trouble)

    This function can not perform the task switching at all, if all of the READY tasks will be less prioritized then the task_current.
    So if you want to unconditionally switch the task (for instance because task cannot progress any futrher (a kind of lock etc)) use the arch_context_switch instead.

    /note this function can be called only from critical section */
void OS_HOT os_schedule(uint_fast8_t higher_prio)
{
   os_task_t *new_task;

   /* check the nesting level or if scheduler is locked */
   if( OS_LIKELY((isr_nesting <= 1) && (0 == sched_lock)) ) /* either in case of os_tick and user called os function we expect that the thread will be switched, (verify this assumtion by getting the profile) */
   {
      new_task = os_task_dequeue_prio(&ready_queue, task_current->prio_current + higher_prio); /* if higher_prio > 0 we will get only task with higher priority than task_current, (see condition inside os_task_dequeue_prio) */
      if( NULL != new_task ) { /* in case all READY tasks have lower priority, we will get NULL and no task switch will be done */
         os_task_makeready(task_current); /* push task_current to ready_queue */
         if( 0 == isr_nesting ) {
            arch_context_switch(new_task); /* switch to new task if not in interrupt */
         } else {
            task_current = new_task; /* in case of interrupt only change the task_current pointer,  so the context switching will be properly perfomed by arch_contextrestore_i at the end of ISR */
            task_current->state = TASKSTATE_RUNNING; /* dont forget about changig the task state */
         }
      }
   }
}

void os_task_exit(int retv)
{
   os_task_t *task;
   arch_criticalstate_t cristate;

   /* we remove current task from system and going for endless sleep */
   arch_critical_enter(cristate); /* we need to block the preemption, we never leave this state in this task, when we switch to other task interrupts will be enabled again */
   task_current->ret_value = retv;
   task_current->state = OS_DESTROYED;

   /* if some task is waiting fr this tas we signalize the join)sem, on which some thread may wait in os_task_join */
   if( NULL != task_current->join_sem ) {
      os_scheduler_lock(); /* since after join this signalized thread may remove the current task stack and task struct, we cannot switch to it inside os_sem_up (we must lock the scheduler) (keep in mind that locking the scheduler is different thing than critical section) */
      os_sem_up(task_current->join_sem);
      os_scheduler_unlock(); /* we need to allo the task switch durring the following arch_context_switch */
   }

   /* next we pop any ready task - at least there will be a idle task so we will never get the NULL there
       and we switch to that task, this will drop the reference to last task_curent and eficiently remove the task from the system queues */
   task = os_task_dequeue(&ready_queue); /* chose any ready task to whoh we can switch */
   arch_context_switch(task);

   OS_ASSERT(0); /* we should never reach this point, becouse we droped the task */
}

/* --- private functions --- */

static void arch_task_debug(os_task_t *task, void* stack, size_t stack_size)
{
#ifdef OS_CONFIG_CHECKSTACK
   memset(stack, OS_STACK_FILLPATERN, stack_size);
   task->stack_size = stack_size;
#ifdef OS_STACK_DESCENDING
   task->stack_end = stack;
#else
   task->stack_end = (void*)(((uint8_t*)stack) + stack_size);
#endif
#endif
}

static void os_task_init(
   os_task_t* task,
   uint_fast8_t prio)
{
   memset(task, 0, sizeof(os_task_t));
   list_init(&(task->list));
   task->prio_base = prio;
   task->prio_current = prio;
   task->state = TASKSTATE_READY;
   task->block_type = OS_TASKBLOCK_INVALID;
   list_init(&(task->mtx_list));
}

