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

#ifndef __OS_SCHED_
#define __OS_SCHED_

/**
 * Definition of task states. Typical to other RTOS'es
 * @warning DO NOT CHANGE THE ORDER OF THE STATES SINCE CODE COMPARES THEM
 */
typedef enum {
   TASKSTATE_RUNNING = 0, /**< Task is currently running, means task_current
                               point to TCB with that state */
   TASKSTATE_READY,       /**< Task is ready to be run and waits for CPU time,
                               TCB with that state are placed in ready_queue */
   TASKSTATE_WAIT,        /**< Task waits for synchronization object (mtx, sem
                               or wait_queue), TCB with that state are placed in
                               task queues of synchronization object */
   TASKSTATE_DESTROYED,   /**< Task was finished and waits for join operation,
                               TCB removed from scheduling, TCB with that
                               state are not stored anywhere */
   TASKSTATE_INVALID      /**< Task was joined, TCB state used to prevent from
                               double join */
} os_taskstate_t;

/** Definition of task blocking root cause. Used only if TCB state is
 * TASKSTATE_WAIT */
typedef enum {
   OS_TASKBLOCK_INVALID = 0,  /**< Invalid placeholder */
   OS_TASKBLOCK_SEM,          /**< Task blocked on semaphore */
   OS_TASKBLOCK_MTX,          /**< Task blocked on mutex */
   OS_TASKBLOCK_WAITQUEUE     /**< Task blocked on wait_queue */
} os_taskblock_t;

/** Return codes for OS API functions */
typedef enum {
   OS_OK = 0,     /**< Operation successful */
   OS_WOULDBLOCK, /**< Operation would block */
   OS_TIMEOUT,    /**< operation timeouted */
   OS_DESTROYED,  /**< Operation failed due resource was destroyed in flight */
   OS_INVALID     /**< Invalid operation */
} os_retcode_t;

/* --- forward declarations --- */
struct os_taskqueue_tag;
struct os_sem_tag;
struct os_waitqueue_tag;

/** Definition of Task Structure - Task Control Block - TCB */
typedef struct {
   /** need to be the first field for easy access to it from ASM ctx switch code
    * for more info about the context store see arch_context_t */
   arch_context_t ctx;

   /** list link, allows to place the task on various lists */
   list_t list;

   /** the base priority, constant for whole life cycle of the task, decided to
    * use fast8 type since we need at least uint8 while we don't want to have
    * access time penalties from memory aliment issues of specific ARCH/CPU */
   uint_fast8_t prio_base;

   /** current priority of the task, namely the effective priority, it may be
    * changed by priority inheritance code */
   uint_fast8_t prio_current;

   /** state of task - common meaning as in other RTOS'es */
   os_taskstate_t state;

   /** following struct is used only when task is in TASKSTATE_WAIT or
    * TASKSTATE_READY */
   struct {
      /** task_queue to which the task belongs, this can be ready_queue or any
       * task_queue of blocking object like sem or mtx. Pointer used during task
       * enqueue/dequeue since we have to modify the also task_queue itself */
      struct os_taskqueue_tag *task_queue;

      /** defines on which object type the task is blocked, valid only when task
       * state == TASKSTATE_WAIT */
      os_taskblock_t block_type;

      /** associated timer while waiting on resource with timeout guard, valid
       * only if task state = TASKSTATE_WAIT */
      os_timer_t *timer;
   };

   /** list of mutexes owned by task, this list is required to calculate new
    * prio_current during mutex unlock, extensive explanation of this can be
    * found in os_mtx_unlock. This list may be either empty or occupied either
    * during task_state READY and WAIT, it just means that task locked some
    * mutexes */
   list_t mtx_list;

   /** pointer to semaphore from os_task_join(), it is provided by task which
    * would like to join this task and waits until this task will finish. Tis is
    * quite cheap solution to join() operation since here we kept only the
    * pointer while semaphore itself is stored on stack of task which will wait
    * for join() */
   struct os_sem_tag *join_sem;

   /** union is used to save memory for variables used during self excluding
    * scenarios */
   union {
      /** value which can be returned by task before it will be destroyed */
      int ret_value;

      /** return code returned from blocking function, used for communication
       * between owner and task which wake it up */
      os_retcode_t block_code;
   };

#ifdef OS_CONFIG_CHECKSTACK
   /** Pointer to stack end used for verification if it was not overflowed */
   void *stack_end;
   size_t stack_size; /* \TODO necessarily needed ? */
#endif
} os_task_t;

/** Definition of task_queue, it is used to store TCB's for task which contends
 * for execution or resource. It is used as ready_queue and also as part of mtx
 * and sem blocking mechanism */
typedef struct os_taskqueue_tag {

   /** buckets of tasks, there are a separate list for each priority level */
   list_t tasks[OS_CONFIG_PRIOCNT];

   /** mask for used priority levels */
   arch_bitmask_t mask;

} os_taskqueue_t;

/* --- protected variables --- */
extern os_task_t task_idle;

/* --- forward declarations --- */
typedef void (*os_initproc_t)(void);
typedef int (*os_taskproc_t)(void *param);

/**
 * Function disables the scheduling.
 *
 * Interrupts are not affected, they will remain enabled. Since function
 * disables scheduling, also preemption will be disabled. Preemption will also
 * be disabled.
 * This function can be called multiple times. It tracks the nesting level of
 * scheduler disable, so to unlock scheduler so_scheduler_unlock() must be
 * called same number of times.
 *
 * @pre this function cannot be used from ISR, can be called from idle task with
 *      proper care
 * @pre this function cannot be called from waitqueue spin loop
 * @post after return from this function scheduler will remain disabled until
 *       os_scheduler_unlock() call same number of times as os_scheduler_lock()
 *       was called
 *
 * @note use this function with care since locking scheduler for long periods of
 *       time decreases responsiveness of system
 */
void os_scheduler_lock(void);

/**
 * Function enables the scheduling
 *
 * @param sync by passing 'true' in this parameter, user application will force
 *        semaphore signaling in synchronized mode. It means that there will be
 *        no context switches during os_sem_up_sync() call even if some higher
 *        priority task would be woken up. By passing 'false' in this parameter
 *        application specifies that context switches are allowed. This feature
 *        can be used in application code which will trigger the scheduler
 *        anyway after return from os_sem_up_sync() by some other OS API call.
 *        This helps to save CPU cycles by preventing from unnecessary context
 *        switches.
 *        IMPORTANT: This parameter must be set to 'false' in case function is
 *        called from ISR.
 *
 * @pre this function cannot be used from ISR, can be called from idle task with
 *      proper care
 * @pre this function cannot be called from waitqueue spin loop
 * @post after return from this function scheduler will be enabled again. If
 *       os_scheduler_lock() was called multiple times than to enable the
 *       scheduler os_scheduler_unlock() must be called the same number of
 *       times
 */
void os_scheduler_unlock(bool sync);

/**
 * Function initializes OS internals.
 *
 * Function should be called form user code main() function. It does not return.
 *
 * @param app_init Function callback which initializes architecture dependent HW
 *        and SW components from context of OS idle task (for example creation
 *        of global  mutexes or semaphores, creation of other OS tasks, starting
 *        of tick timer and all SW components which would need to call OS
 *        functions
 * @param app_idle Function callback which is called by OS in each turn of idle
 *        task. This callback can be used to perform user actions from context
 *        of idle task, but keep in mind that it cannot call any OS blocking
 *        functions.
 *
 * @pre prior this function call, all architecture depended HW and SW setup must
 *      be finished. This includes .bss and .data sections initialization, stack
 *      and frame pointer initialization, watchdog and interrupts disabling etc)
 * @pre It is important that prior this function call, interrupts must be
 *      disabled
 *
 * @note It is guarantied that app_init will be called before app_idle
 */
void os_start(
   os_initproc_t app_init,
   os_initproc_t app_idle);

/**
 * Function creates user tasks
 *
 * There is no limit for number of user tasks.
 *
 * @param task pointer to task structure TCB
 * @param prio priority of the task. Allowed priorities are 0 < prio <
 *        OS_CONFIG_PRIOCNT. Many task may share the same priority. In case more
 *        than one task would be in READY state, they are scheduled in round
 *        robin manner.  Such task are also threated in FIFO manner for all
 *        synchronization resources (mtx, sem etc)
 * @param stack pointer to stack memory. Each task has to have at least minimal
 *        stack size which will allow for execution of designed nested level of
 *        interrupt without overflow of the stack. SW can verify if task stack
 *        was not overflowed by calling os_task_check() (see
 *        OS_CONFIG_CHECKSTACK)
 * @param stack_size size of the given stack
 * @param entry point function of the task
 * @param user defined parameter given to entry point function
 *
 * @pre OS must be initialized before any task may be created (call os_start)
 * @pre this function cannot be called from ISR
 *
 * @post the created task would be scheduled immediately if it would have
 *       priority bigger than the task that call os_task_create(). Therefore
 *       caller may return after created task would block on resource.
 */
void os_task_create(
   os_task_t *task,
   uint_fast8_t prio,
   void *stack,
   size_t stack_size,
   os_taskproc_t proc,
   void *param);

/**
 * By calling the function one task can wait until task given by parameter
 * will exit from its own entry point function. Return value from task entry
 * point function is returned to waiter.
 *
 * @param task pointer to task structure (TCB) on which caller would like
 *        join/wait for exit
 *
 * @pre task can not be already finished/joined
 * @pre function cannot be called from ISR
 * @pre since this function would block it cannot be called from idle task
 *
 * @post stack and task structure (TCB) of the joined task may be safely removed
 *       after return from os_task_join(). It can be also reused for another
 *       task creation.
 *
 * @return return code from task entry point of joined task
 */
int os_task_join(os_task_t *task);

/**
 * Function allows to give back the processor to other task READY task with the
 * same priority. Context switch will be made if only some other task with the
 * same priority is in READY state. Therefore idle task will not be scheduled()
 * by this function.
 *
 * @pre this function cannot be called from ISR
 * @pre this function cannot be called from idle task
 */
void os_yield(void);

/**
 * Function verify if task stack was not overflowed
 *
 * @param task pointer to task structure which stack we would like to verify
 *
 * @pre task must be valid, initialized not finished
 *
 * @post in case function detects stack corruption it internally calls os_halt()
 */
void os_task_check(os_task_t *task);

/**
 * System tick function (system timer interrupt)
 *
 * This function need to be called from ISR. It kicks the timer subsystem and
 * also triggers the preemption mechanism. The frequency of os_tick() call is
 * user defined and it defined a jiffy. All timeouts for time guarded OS
 * blocking functions are measured in jiffies.
 *
 * @pre can be called only from ISR
 */
void OS_HOT os_tick(void);

/**
 * Function halt the system
 *
 * The main purpose of this function is to lock the execution in case of
 * critical error. This function can be used as final stage of user assert
 * failure code. This function never returns and no other task will be
 * scheduled. The system will be deadlocked in busy loop of os_halt() code.
 */
void OS_COLD os_halt(void);

/**
 * Function finalizes task execution
 *
 * This is the last function which should be called by OS to kill the task.
 * Usually it is called by arch dependent code after return from task entry
 * point function. User also can call this function at will in case the task
 * does not hold any resources like mutex, which can lead to deadlocking of the
 * system. os_task_exit() accepts task return code as parameter. After calling
 * os_task_exit() another task may reclaim the task return code as well as their
 * stack and task structure (TCB) by calling os_task_join().
 *
 * @pre task may not hold resources which may lead to system deadlock
 * @post this function does not return
 */
void OS_NORETURN OS_COLD os_task_exit(int retv);

#endif

