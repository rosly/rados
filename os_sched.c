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

/** not need to be volatile since we whant to optimize the access to it, while
 * it only may change after os_schedule call, compiler asumes that after
 * function call all data under the pointer may be changed (also ISR will not
 * change it since interrupts are blocked by critical sections) */
/* intentionaly not volatile */
os_task_t* task_current = NULL;
os_taskqueue_t ready_queue;
volatile os_atomic_t isr_nesting = 0;
volatile os_atomic_t sched_lock = 0;
static os_task_t task_idle;

/* private function forward declarations */
static void arch_task_debug(os_task_t *task, void* stack, size_t stack_size);
static void os_task_init(os_task_t* task, uint_fast8_t prio);

/* prior this function call, all interrupts must be disabled */
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
   task_idle.state = TASKSTATE_RUNNING; /* because state = TASKSTATE_READY after init */
   /* interrupt are not enabled, critical section not needed for following */
   task_current = &task_idle;
   /* from this point we can switch the context because we have at least
    * task_idle in ready_queue, but for fast app_init we disable the scheduler
    * until we will be fully ready */

   arch_os_start();

   /* disable the scheduler for time of app_init() call, this is needed since we
    * dont wont to switch tasks while we craete them in app_init() */
   os_scheduler_lock();
   /* here app should create the remaining threads and start the interrupts (not
    * only the tick), we call app_init() user supplied function */
   app_init();
   os_scheduler_unlock();
   arch_eint(); /* we are ready for scheduler actions, enable the interrupts */

   do
   {
      arch_criticalstate_t cristate;

      arch_critical_enter(cristate);
      os_task_makeready(task_current); /* task_current means task_idle here */
      /* finaly we switch the context to first user task, (it will have the
       * higher ptiority than idle) (need to be done under critical section) */
      arch_context_switch(os_task_dequeue(&ready_queue));
      task_current->state = TASKSTATE_RUNNING; /* we are back again in idle task, it is running now */
      arch_critical_exit(cristate);
   }while(0);

   /* after all initialization actions, idle task will spin in idle loop */
   while(1)
   {
      /* user supplied idle function */
      app_idle();
      /* if exit then arch defined relax function */
      arch_idle();
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

   os_schedule(1); /* 1 as a param will cause a task-switch only if created task
                      has higher priority than task_curernt */
}

int os_task_join(os_task_t *task)
{
   arch_criticalstate_t cristate;
   os_sem_t join_sem;
   int ret;

   OS_ASSERT(0 == isr_nesting); /* this function may be called only form user code */
   /* idle task cannot call blocking functions (will crash OS) */
   OS_ASSERT(task_current->prio_current > 0);

   arch_critical_enter(cristate);
   OS_ASSERT(NULL == task->join_sem); /* only single task can wait for another task */
   OS_ASSERT(TASKSTATE_INVALID != task->state); /* task can be joined only once */
   /* check if task ended up in os_task_exit() */
   if( task->state < TASKSTATE_DESTROYED )
   {
      /* it seems not, so we have to wait until it will finish
       * we will wait for it by blocking on semaphore */
      os_sem_create(&join_sem, 0);
      task->join_sem = &join_sem;
      ret = os_sem_down(&join_sem, OS_TIMEOUT_INFINITE);
      OS_ASSERT(OS_OK == ret);
      os_sem_destroy(&join_sem);
   }

   OS_ASSERT(TASKSTATE_DESTROYED == task->state);
   /* here we know for sure that task ended up in os_task_exit() */
   task->state = TASKSTATE_INVALID; /* mark that task was joined */
   task->join_sem = NULL;
   arch_critical_exit(cristate);

   return task->ret_value;
}

void OS_HOT os_tick(void)
{
   OS_ASSERT(isr_nesting > 0); /* this function may be called only form ISR */

   os_timer_tick(); /* call the timer module mechanism */
   /* switch to other READY task which has the same or greater priority (0 as
    * param means just that) */
   os_schedule(0);
}

void OS_COLD os_halt(void)
{
  arch_halt();
}

/* --- private functions --- */

void OS_HOT os_task_enqueue(
   os_taskqueue_t* OS_RESTRICT task_queue,
   os_task_t* OS_RESTRICT task)
{
   list_append(&(task_queue->tasks[task->prio_current]), &(task->list));
   task->task_queue = task_queue;
   if( task_queue->priomax < task->prio_current ) {
     task_queue->priomax = task->prio_current;
   }
}

/**
 *  Function realculate the priomax inside task_queue after task unlink
 *  operation.Function does not unlink the task! this must be done before
 *  calling this function
 */
void OS_HOT os_task_queue_reprio(os_taskqueue_t* OS_RESTRICT task_queue)
{
   while( (0 != (task_queue->priomax)) &&
          list_is_empty(&(task_queue->tasks[task_queue->priomax]))) {
     --(task_queue->priomax);
   }
}

/**
 *  Function unlink the task from task_queue
 *  This is desierd function which should be called when we need to switch to
 *  explicitly pointed task from task_queues
 */
void OS_HOT os_task_unlink(os_task_t* OS_RESTRICT task)
{
   list_unlink(&(task->list));
   os_task_queue_reprio(task->task_queue);
   task->task_queue = NULL;
}

/**
 *  Function dequeues the most urgent task from the task_queue.  This is desierd
 *  function which should be called when we need to peek (switch to) most
 *  prioritized task
 */
os_task_t* OS_HOT os_task_dequeue(os_taskqueue_t* OS_RESTRICT task_queue)
{
   list_t *list = list_detachfirst(&(task_queue->tasks[task_queue->priomax]));
   if( NULL == list ) {
      return NULL;
   }
   os_task_t *task = os_container_of(list, os_task_t, list);
   os_task_queue_reprio(task_queue);
   task->task_queue = NULL;
   return task;
}

/**
 *  Similar to os_task_dequeue() but taks is dequeued only if its prio is higher
 *  than prio passed by param
 */
os_task_t* OS_HOT os_task_dequeue_prio(
   os_taskqueue_t* OS_RESTRICT task_queue,
   uint_fast8_t prio)
{
   if( task_queue->priomax >= prio ) {
      return os_task_dequeue(task_queue);
   }
   return NULL;
}

/**
 *  Function returns the pointer to most urgent task on the task_queue
 *  This function does not modify the task_queue, it just returns the reference
 *  Use this function only when you are intrested about some property of most
 *  prioritized task on the queue but you dont whant to dqueue this task
 */
os_task_t* OS_HOT os_task_peekqueue(os_taskqueue_t* OS_RESTRICT task_queue)
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
   unsigned i;

   for(i = 0; i < os_element_cnt(task_queue->tasks); i++) {
      list_init(&(task_queue->tasks[i]));
   }
   task_queue->priomax = 0;
}

/**
 * This function switches the current task to one of task from ready queue.
 * Typicaly the most prioritized task from ready queue is chosen, but it has to
 * have prio higher than task_current. Additionaly we can force to chose only
 * task with higher priority than task_curent by usin of \param higher_prio
 * param.
 *
 *  Function works in following schema
 *   - chose task from ready_queue, pick only task with priority equal to
 *     task_current or higher (depending on higher_prio param)
 *   - in case of user code it will switch the context to new task
 *   - in case of interrupt (nesting level == 1) it will not swich the context
 *     but only change the task_current pointer (so context switch can be done at
 *     the end of ISR)
 *   - in case of interrupt (nesting level > 1) do nothing at all (not even
 *     saerch for READY task), this is because only most bottom ISR at the nesting
 *     scenario can switch the tasks (otherwise we will get into trouble)
 *
 *  This function can not perform the task switching at all, if all of the READY
 *  tasks will be less prioritized then the task_current.  So if you want to
 *  unconditionally switch the task use the os_switch() instead. This can be
 *  required if you cannot continiue with execution of current task, for
 *  instance after kind of block (look at os_sem.c code)
 *
 *  /note this function can be called only from critical section
 */
void OS_HOT os_schedule(uint_fast8_t higher_prio)
{
   os_task_t *new_task;

   /* Check the nesting level or if scheduler is locked.
    * We dont whant to switch the task if we are in nested ISR and in case we
    * explicitly locked the scheduler from some reason  */
   if( OS_LIKELY((isr_nesting <= 1) && (0 == sched_lock)) )
   {
      /* pick some READY task for ready_queue which has priority equal or
       * greater than task_current (see condition inside os_task_dequue_prio) */
      new_task = os_task_dequeue_prio(
                   &ready_queue, task_current->prio_current + higher_prio);

      /* in case all READY tasks have lower priority, we will get NULL and no
       * task switch will be done */
      if( NULL != new_task ) {
         /* since we have new task, task_current need to be pushed to
          * ready-queue */
         os_task_makeready(task_current);
         /* check if we are in ISR */
         if( 0 == isr_nesting ) {
            /* switch the context only if not in ISR */
            arch_context_switch(new_task);
         } else {
            /* in case of ISR, only switch the task_current pointer, context
             * switching itself must be perfomed at the end of ISR (see
             * arch_contextrestore_i) */
            task_current = new_task;
         }
         /* in both cases task is running now */
         task_current->state = TASKSTATE_RUNNING;
      }
   }
}

/**
 * Function is simmilar to os_schedule in scope that it switches the context,
 * but it does it always (os_schedule() may not change the task if there is no
 * READY task with higher prio).
 * This function is intended to call while task_current cannot progress any
 * further and must be blocked.
 *
 * \param task_queue task_queue on wich task_current will be blocked
 * \param block_type a couse of block
 *
 * \warning This function cannot be called from ISR!!
 */
void OS_HOT os_block_andswitch(
   os_taskqueue_t* OS_RESTRICT task_queue,
   os_taskblock_t block_type)
{
  /* block current task on pointed queue */
  os_task_makewait(task_queue, block_type);

  /* Chose any READY task and switch to it - at least idle task is in READY
   * state (ready_queue) so we will never get the NULL there */
  arch_context_switch(os_task_dequeue(&ready_queue));

  /* return from arch_context_switch call will be in some of next os_schedule()
   * call. After return task state should be again set to TASKSTATE_RUNING, also
   * iterrupts are again disabled here (even it they where enabled for execution
   * of previous task) */
  task_current->state = TASKSTATE_RUNNING;
}

void os_task_exit(int retv)
{
   arch_criticalstate_t cristate;

   /* we remove current task from system and going for endless sleep. We need
    * to block the preemption, we never leave this state in this task, when we
    * switch to other task interrupts will be enabled again */
   arch_critical_enter(cristate);

   task_current->ret_value = retv; /* store the return value of task for future join */
   task_current->state = TASKSTATE_DESTROYED;

   /* if some task is waiting for this task, we signalize the join semaphore on
    * which some other thread is waiting in os_task_join */
   if( NULL != task_current->join_sem ) {

      /* \TODO after adding os_sem_up_sync() blocking of scheduler seems to be
       * unnecessary, we will not call schedule() anyway .. rething it again and
       * if really unnecessary remove sheduler locking from below */

      /* since after join, master thread will probably remove the current task
       * stack and task struct, we cannot switch to it while we call os_sem_up.
       * We simply avoiding this by locking the scheduler. (keep in mind that
       * critical section is different think, here we lock additionaly
       * scheduler) */
      os_scheduler_lock();
      os_sem_up_sync(task_current->join_sem, true);
      os_scheduler_unlock(); /* reenable task switch by scheduler */
   }

  /* Chose any READY task and switch to it - at least idle task is in READY
   * state (ready_queue) so we will never get the NULL there
   * Since we not saving current_task anywhere it will disapear from scheduling
   * loop */
   arch_context_switch(os_task_dequeue(&ready_queue));

  /* we should never reach this point, there is no chance that scheduler picked
   * up this code again since we dropped the task */
  OS_ASSERT(0);
  arch_critical_exit(cristate); /* just to prevent warning about unused variable */
  /* \TODO change arch_critical_enter call in this function into arch_dint() and
   * check if this will work */
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
   task->wait_queue = NULL;
   list_init(&(task->mtx_list));
}

