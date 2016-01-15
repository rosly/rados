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

#include "os_private.h"

/* will be truncanced to register size */
#define OS_STACK_FILLPATERN ((uint8_t)0xAB)

/* --- variables definitions used by scheduler --- */

/** Pointer to currently running task structure
 * /note intentionally not volatile, not need to be volatile since we use it only
 * under critical section of scheduler and it is modified only by os_schedule()
 * call. Compiler asumes that after any function call all data under the pointer
 * may be changed (synchronization point). Also ISR will not change it out of
 * scheduler control since interrupts are blocked by critical sections */
os_task_t* task_current = NULL;

/** Task queue for READY tasks
 * The main structure used by scheduler during os_schedule() call. From this
 * task queue we pick task which will be running as current one */
os_taskqueue_t ready_queue;

/** Task structure for idle task
 * Since user programs does not explicitly create idle task, OS need to keep
 * internal task structure for IDLE task */
os_task_t task_idle;

/** Counter to mark ISR nesting
 * Allows to track the nesting level of ISR and control the task state
 * dump/restore on ISR enter/exit. Also used for locking the preemption Can be
 * also used to check if OS function was called by user or ISR since
 * isr_nesting > 0 means that we were called from ISR */
volatile os_atomic_t isr_nesting = 0;

/** Preemption lock bitflag
 * Used to explicitly lock the scheduler for any reason */
volatile os_atomic_t sched_lock = 0;

#ifdef OS_CONFIG_WAITQUEUE
/** Pointer to wait_queue on which task_current prepared to suspend.
 * Set in os_waitqueue_prepare(). After this call preemption is also disabled.
 * If this pointer is != NULL we say that task_current is in 'prepared' state
 * (prepared for suspend).
 */
os_waitqueue_t *waitqueue_current = NULL;
#endif

/* --- forward declaration of private functions --- */

#ifdef OS_CONFIG_CHECKSTACK
static void os_task_check_init(os_task_t *task, void* stack, size_t stack_size);
#endif
static void os_task_init(os_task_t* task, uint_fast8_t prio);

/* --- public function implementation --- */
/* all public functions are documented in os_sched.h file */

void os_scheduler_lock(void)
{
   OS_ASSERT(0 == isr_nesting); /* cannot call from ISR */
   OS_ASSERT(!waitqueue_current); /* cannot call after os_waitqueue_prepare() */

   os_scheduler_intlock();
}

void os_scheduler_unlock(bool sync)
{
   OS_ASSERT(0 == isr_nesting); /* cannot call from ISR */
   OS_ASSERT(!waitqueue_current); /* cannot call after os_waitqueue_prepare() */

   os_scheduler_intunlock(sync);
}

void os_start(
   os_initproc_t app_init,
   os_initproc_t app_idle)
{
   /* disable the interrupts, needed to make critical section */
   arch_dint();

   /* initialize OS subsystem and variables */
   os_taskqueue_init(&ready_queue);
   os_timers_init();

   /* create and switch to idle task, perform the remain app_init on idle_task */
   os_task_init(&task_idle, 0);
   task_idle.state = TASKSTATE_RUNNING;
   task_current = &task_idle;
   /* from this point we can perform context switching because we have at least
    * task_idle in ready_queue, but for fast app_init we disable the scheduler
    * until we will be fully ready */

   /* architecture dependent initialization */
   arch_os_start();

   /* we need to lock the scheduler since must prevent context switch to tasks
    * which will be created while app_init() */
   os_scheduler_intlock();
   /* user supplied function, it should initialize interrupts (tick and others) */
   app_init();
   os_scheduler_intunlock(true); /* sync = true, do not schedule() yet */
   arch_eint(); /* we are ready for scheduling actions, enable the all interrupts */

   /* after all initialization actions, force first schedule (context switch)
    * \TODO instead of following nasty code we can just use os_schedule(0) */
   do
   {
      arch_criticalstate_t cristate;

      /* enter critical section since we will schedule */
      arch_critical_enter(cristate);
      os_task_makeready(task_current); /* task_current == task_idle here */
      /* finally we switch the context to user task, (any user task has higher
       * prio than idle_task) */
      arch_context_switch(os_taskqueue_dequeue(&ready_queue));
      task_current->state = TASKSTATE_RUNNING; /* we are back again in idle task */
      arch_critical_exit(cristate);
   } while (0);

   /* idle task will spin in following idle loop */
   while (1)
   {
      app_idle(); /* user supplied idle function */
      arch_idle(); /* arch dependent relax function */
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

   OS_ASSERT(0 == isr_nesting); /* cannot create task from ISR */
   OS_ASSERT(prio < OS_CONFIG_PRIOCNT); /* prio must be less than prio config limit */
   OS_ASSERT(prio > 0); /* only idle task may have the prio 0 */
   OS_ASSERT(stack); /* stack must be given */
   OS_ASSERT(stack_size >= OS_STACK_MINSIZE); /* minimal size for stack */
   OS_ASSERT(!waitqueue_current); /* cannot call after os_waitqueue_prepare() */

   os_task_init(task, prio);

#ifdef OS_CONFIG_CHECKSTACK
   os_task_check_init(task, stack, stack_size);
#endif
   arch_task_init(task, stack, stack_size, proc, param);

   arch_critical_enter(cristate);
   os_taskqueue_enqueue(&ready_queue, task);
   /* 1 as a param allows context switch only if created task has higher
    * priority than task_curernt */
   os_schedule(1);
   arch_critical_exit(cristate);
}

int os_task_join(os_task_t *task)
{
   arch_criticalstate_t cristate;
   int ret;

   OS_ASSERT(0 == isr_nesting); /* cannot join tasks from ISR */
   /* idle task cannot call blocking functions (will crash OS) */
   OS_ASSERT(task_current != &task_idle);
   OS_ASSERT(!waitqueue_current); /* cannot call after os_waitqueue_prepare() */

   arch_critical_enter(cristate);
   OS_ASSERT(!task->join_sem); /* only one task is allowed to wait for particular task */
   OS_ASSERT(TASKSTATE_INVALID != task->state); /* task can be joined only once */
   /* check if task have finished (does it called os_task_exit()) ? */
   if (task->state < TASKSTATE_DESTROYED)
   {
      os_sem_t join_sem;

      /* it seems not, so we have to wait until it will finish
       * we will wait for it by blocking on semaphore */
      os_sem_create(&join_sem, 0);
      task->join_sem = &join_sem;
      ret = os_sem_down(&join_sem, OS_TIMEOUT_INFINITE);
      OS_ASSERT(OS_OK == ret);
      os_sem_destroy(&join_sem);
   }

   OS_SELFCHECK_ASSERT(TASKSTATE_DESTROYED == task->state); /* double check */
   /* here we know for sure that task ended up in os_task_exit() */
   task->state = TASKSTATE_INVALID; /* mark that task was joined */
   task->join_sem = NULL;
   ret = task->ret_value;
   arch_critical_exit(cristate);

   return ret;
}

void os_yield(void)
{
   OS_ASSERT(0 == isr_nesting); /* cannot join tasks from ISR */
   OS_ASSERT(task_current != &task_idle); /* idle task cannot call os_yield() */
   OS_ASSERT(!waitqueue_current); /* cannot call after os_waitqueue_prepare() */

   arch_criticalstate_t cristate;

   arch_critical_enter(cristate);
   os_schedule(0);
   arch_critical_exit(cristate);
}

#ifdef OS_CONFIG_CHECKSTACK
void os_task_check(os_task_t *task)
{
   if (OS_UNLIKELY(OS_STACK_FILLPATERN != *((uint8_t*)task->stack_end)))
   {
      os_halt();
   }
}
#else
void os_task_check(os_task_t* OS_UNUSED(task))
{
}
#endif

void OS_HOT os_tick(void)
{
   OS_ASSERT(isr_nesting > 0); /* this function may be called only form ISR */

   os_timer_tick(); /* call the timer module mechanism */
   /* switch to other READY task which has the same or greater priority (0 as
    * param of os_schedule() means just that) */
   os_schedule(0);
}

void OS_COLD os_halt(void)
{
   os_scheduler_intlock();
   arch_dint();
   arch_halt();
}

/* --- private functions --- */

/**
 * Function adds task to task_queue
 * Should be called each time we add task to task_queue since it updates
 * task_queue internals. It also links task with task_queue but does not update
 * the task state! This must be done before call of this function */
void OS_HOT os_taskqueue_enqueue(
   os_taskqueue_t* task_queue,
   os_task_t* task)
{
   /* enqueue the task to task_queue bucket */
   list_append(&(task_queue->tasks[task->prio_current]), &(task->list));
   task->task_queue = task_queue;

   /* update the mask for task_queue buckets */
   arch_bitmask_set(task_queue->mask, task->prio_current);
}

/**
 * Function unlink the task from task_queue
 * Need to be called each time OS wants to dequeue specific task from task_queue
 */
void OS_HOT os_taskqueue_unlink(os_task_t* task)
{
   os_taskqueue_t *task_queue;
   uint_fast8_t prio;

   /* unlink the task form task_queue bucket */
   list_unlink(&(task->list));

   /* we need to recalculate the mask for task queue buckets */
   task_queue = task->task_queue;
   prio = task->prio_current;
   if (list_is_empty(&task_queue->tasks[prio]))
   {
      /* mark that this prio list is empty */
      arch_bitmask_clear(task_queue->mask, prio);
   }

   task->task_queue = NULL;
}

/**
 * Function changes the effective (prio_current) priority of task
 * The pointed task may be either in WAIT or READY state
 */
void OS_HOT os_taskqueue_reprio(
   os_task_t* task,
   uint_fast8_t new_prio)
{
   os_taskqueue_t *task_queue;

   /* check if we really change the prio so we would not unnecessarily change the
    * order of waiting tasks */
   if (task->prio_current != new_prio)
   {
      task_queue = task->task_queue;

      /* if task was enqueued on task_queue we need to change the prio bucket */
      if (task_queue)
      {
         os_taskqueue_unlink(task);
      }
      task->prio_current = new_prio;
      if (task_queue)
      {
         os_taskqueue_enqueue(task_queue, task);
      }
   }
}

static os_task_t* os_taskqueue_intdequeue(
   os_taskqueue_t *task_queue,
   uint_fast8_t maxprio)
{
   list_t *task_list;
   os_task_t *task;

   /* get the max prio task */
   task_list = &task_queue->tasks[maxprio];
   task = os_container_of(list_detachfirst(task_list), os_task_t, list);
   if (list_is_empty(task_list))
   {
      /* mark that this prio list is empty */
      arch_bitmask_clear(task_queue->mask, maxprio);
   }

   task->task_queue = NULL;
   return task;
}

/**
 * Function dequeues the most urgent task from the task_queue.
 * Function need to be called when OS need to obtain most prioritized task in
 * task_queue
 */
os_task_t* OS_HOT os_taskqueue_dequeue(os_taskqueue_t* task_queue)
{
   uint_fast8_t maxprio;

   /* get max prio to fetch from proper list */
   maxprio = arch_bitmask_fls(task_queue->mask);
   if (0 == maxprio)
   {
      return NULL;
   }
   --maxprio; /* convert to index counted from 0 */

   return os_taskqueue_intdequeue(task_queue, maxprio);
}

/**
 * Similar to os_taskqueue_dequeue() but task is dequeued only if most top prio task
 * in task queue has prio higher than this passed by @param prio
 */
os_task_t* OS_HOT os_taskqueue_dequeue_prio(
   os_taskqueue_t* task_queue,
   uint_fast8_t prio)
{
   uint_fast8_t maxprio;

   maxprio = arch_bitmask_fls(task_queue->mask);
   if (0 == maxprio)
   {
      return NULL;
   }
   --maxprio; /* convert to index counted from 0 */

   if (maxprio < prio)
   {
      return NULL;
   }

   return os_taskqueue_intdequeue(task_queue, maxprio);
}

/**
 *  Function returns the pointer to top prio task on the task_queue
 *  This function does not dequeue the task from task_queue, it just returns
 *  pointer to task which is still enqueued in task_queue.
 *  Use this function only when you are interested about some property of most
 *  prioritized task on the queue but you don't want to dequeue this task.
 */
os_task_t* OS_HOT os_taskqueue_peek(os_taskqueue_t* task_queue)
{
   uint_fast8_t maxprio;
   list_t *task_list;

   maxprio = arch_bitmask_fls(task_queue->mask);
   if (0 == maxprio)
   {
      return NULL;
   }
   --maxprio; /* convert to index counted from 0 */

   /* peek the max prio task */
   task_list = &task_queue->tasks[maxprio];
   os_task_t *task = os_container_of(
      list_peekfirst(task_list), os_task_t, list);

   return task;
}

/**
 * Function initializes task_queue
 */
void os_taskqueue_init(os_taskqueue_t *task_queue)
{
   uint_fast8_t i;

   for (i = 0; i < os_element_cnt(task_queue->tasks); i++)
   {
      list_init(&(task_queue->tasks[i]));
   }
   task_queue->mask = 0;
}

/**
 * This function switches the current task to another READY task.
 * Context switch is done to the most prioritized task from ready queue.
 * By @param higher_prio we may force that context switch will be done only to
 * higher priority tasks than current_task->prio_current.
 *
 *  Function works in following schema
 *   - chose task from ready_queue, pick only task with priority equal to
 *     task_current or higher (depending on higher_prio param)
 *   - in case function is called from user code it will switch the context to
 *     new task
 *   - in case function is called from interrupt at nesting level == 1 it will
 *     not switch the context immediately but only change the task_current
 *     pointer (the context switch will be postponed until end of ISR)
 *   - in case function is called from interrupt at nesting level > 1 do nothing
 *     at all (not even search for READY task), this is because only most extern
 *     ISR at the nesting scenario can switch the tasks (otherwise we will get
 *     into trouble)
 *
 *  This function will perform the task switching only to task with prio equal
 *  or higher than task_current->prio_current. If the intention is to
 *  unconditionally switch of the task use the os_switch() instead. This can be
 *  required for instance when execution of current task is no longer possible,
 *  like in case of blocking code (look at os_sem.c code)
 *
 *  /note this function can be called only from critical section
 */
void OS_HOT os_schedule(uint_fast8_t higher_prio)
{
   os_task_t *new_task;

   /* Check the nesting level or if scheduler is locked.
    * Do not switch tasks in case of nested ISR or in case we explicitly locked
    * the scheduler for whatever reason */
   if (OS_LIKELY((isr_nesting <= 1) && (0 == sched_lock)))
   {
      /* dequeue another READY task which has priority equal or greater than
       * task_current (see condition inside os_taskqueue_dequeue_prio) */
      new_task = os_taskqueue_dequeue_prio(
         &ready_queue, task_current->prio_current + higher_prio);

      /* we will get NULL in case all READY tasks have lower priority */
      if (new_task)
      {
         /* since we have new task, task_current need to be pushed to
          * ready-queue */
         os_task_makeready(task_current);
         /* check if we were called from ISR */
         if (0 == isr_nesting)
         {
            arch_context_switch(new_task); /* not in ISR, switch context */
         } else {
            /* in case of ISR, only switch the task_current pointer, context
             * switching itself must be performed at the end of ISR (see
             * arch_contextrestore_i) */
            task_current = new_task;
         }
         /* in both cases task is running now */
         task_current->state = TASKSTATE_RUNNING;
      }
   }
}

/**
 * Function is similar to os_schedule in scope that it switches the context,
 * but it does it always (os_schedule() may not change the task if there is no
 * READY task with higher prio).
 * This function is intended to call while task_current cannot progress any
 * further and must be blocked.
 *
 * @param task_queue task_queue on which task_current will be blocked
 * @param block_type blocking root cause code
 *
 * @warning This function cannot be called from ISR!!
 */
void OS_HOT os_task_block_switch(
   os_taskqueue_t* task_queue,
   os_taskblock_t block_type)
{
  /* block current task on pointed task queue */
  os_task_makewait(task_queue, block_type);

  /* chose any READY task and switch to it - at least idle task is READY
   * so we will never get the NULL from os_taskqueue_dequeue() */
  arch_context_switch(os_taskqueue_dequeue(&ready_queue));

  /* we will return to this point after future context switch.
   * After return task state should be again set to TASKSTATE_RUNING, also
   * interrupts are again disabled here (since we were in OS critical section
   * before arch_context_switch() even it they where enabled for execution of
   * previous task) */
  task_current->state = TASKSTATE_RUNNING;
}

/**
 * Function finalizes task execution
 * This is the last function which should be called by OS to kill the task
 */
void OS_NORETURN OS_COLD os_task_exit(int retv)
{
   arch_criticalstate_t cristate;

   /* we remove current task from system and going for endless sleep. We need
    * to block the preemption, we never leave this state in this task, when we
    * switch to other task interrupts will be enabled again */
   arch_critical_enter(cristate);

   task_current->ret_value = retv; /* store the return value of task for future join */
   task_current->state = TASKSTATE_DESTROYED;

   /* if some task is waiting for this task, we signalize the join semaphore.
    * This will wake up the waiting task in os_task_join() */
   if (task_current->join_sem)
   {

      /* since after join, master task will probably remove the current task
       * stack and task structure, we cannot use os_sem_up().
       * We simply avoiding this by locking the scheduler. (keep in mind that
       * critical section is different thing than scheduler lock)
       *
       * \TODO after adding os_sem_up_sync() implementation blocking of
       * scheduler seems to be unnecessary, since we do not call schedule().
       * review this again and if really unnecessary remove scheduler locking
       * in below code */
      os_scheduler_intlock();
      os_sem_up_sync(task_current->join_sem, true);
      os_scheduler_intunlock(true); /* true = sync, re-enable scheduler but not
                                     schedule() yet */
   }

  /* Chose any READY task and switch to it - at least idle task is in READY
   * state (ready_queue) so we will never get the NULL there
   * Since we not pushing current_task anywhere it will disappear from scheduling
   */
   arch_context_switch(os_taskqueue_dequeue(&ready_queue));

  /* we should never reach this point, there is no chance that scheduler picked
   * up this code again since we dropped the task */
  OS_ASSERT(0);
  arch_critical_exit(cristate); /* just to silence warning about unused variable */
  /* \TODO since by critical section we only want to disable interrupts we
   * might change arch_critical_enter call in this function into arch_dint().
   * Review that. */
  while (1); /* just to silence warning about return from function */
}

/* --- private function implementation --- */

#ifdef OS_CONFIG_CHECKSTACK
/**
 * Initialize task checking mechanism
 */
static void os_task_check_init(os_task_t *task, void* stack, size_t stack_size)
{
   memset(stack, OS_STACK_FILLPATERN, stack_size);
   task->stack_size = stack_size;
#ifdef OS_STACK_DESCENDING
   task->stack_end = stack;
#else
   task->stack_end = (void*)(((uint8_t*)stack) + stack_size - 1);
#endif
}
#endif

/**
 * Initialize task structure
 */
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

