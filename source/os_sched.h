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

#ifndef __OS_SCHED_
#define __OS_SCHED_

/** Definition of task states. Typical to other RTOS'es */
typedef enum {
   TASKSTATE_RUNNING = 0, /**< Task is currently running, means task_current
                               point to TCB with that state */
   TASKSTATE_READY,       /**< Task is ready to be run, TCB with that state are
                               placed in ready_queue */
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
   OS_TASKBLOCK_INVALID = 0, /**< Invalid placeholder */
   OS_TASKBLOCK_SEM,         /**< Task blocked on semaphore */
   OS_TASKBLOCK_MTX,         /**< Task blocked on mutex */
   OS_TASKBLOCK_WAITQUEUE    /**< Task blocked on wait_queue */
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

/** Definition of Task Struct - Task Control Block - TCB */
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

    /** \TODO not currently used, pointer to object which blocks the task, valid
     * only when task state == TASKSTATE_WAIT */
    //void *block_obj;

    /** defines on which object type the task is blocked, valid only when task
     * state == TASKSTATE_WAIT */
    os_taskblock_t block_type;

    /** associated timer while waiting on resource with timeout guard, valid
     * only if task state = TASKSTATE_WAIT */
    os_timer_t *timer;

   /* This is pointer to wait_queue if task is associated with it
    * (os_waitqueue_prepare())
    * In case of preemption, scheduler instead of putting such task into
    * ready_queue, it will put it into task_queue of associated wait_queue. It
    * means that such tasks either are in TASKSTATE_ACTIVE while running and
    * checking the condition associated with wait_queue, or are in
    * TASKSTATE_WAIT and are placed in task_queue of proper wait_queue. The
    * associated code is inside os_task_makeready() */
    struct os_waitqueue_tag *wait_queue;
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
     * between owner and thread which wake it up */
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

  /** priority of most important task in the wait_queue, used for fast access to
   * most prioritized task in task_queue
   * \TODO we should use bitfield and bitfield_to_prio map */
  uint_fast8_t priomax;
} os_taskqueue_t;

/* --- forward declarations */
typedef void (*os_initproc_t)(void);
typedef int (*os_taskproc_t)(void* param);

/** Function initializes all internal OS structure.
 *
 * Function should be called form main() and never returns.
 *
 * @param app_init Function callback which initializes architecture dependend HW
 *        and SW components from OS idle task context (for example creation of
 *        global  mutexts or semaphores, creation of other OS tasks, starting
 *        of tick timer and all SW components wchich can issue further OS
 *        function calls).
 * @param app_idle Function callback which is called by OS on every cycle of
 *        idle task. If user part of SW should perform any action while idle task
 *        is scheduled, this is the right place for this. But keep in mind that
 *        from this function, you cannot call any os OS blocking functions.
 *
 * @pre prior this function call, all architecture depended HW setup must be
 *      done to level which allows stable and uninterrupted C enviroment execution
 *      (this includes .bss and .data sections initialization, stack and frame
 *      pointer initialization, watchdog and interrupts disabling etc)
 * @pre It is important that prior this function call, interrupts must be disabled
 *
 * @note It is guarantieed that app_init will be called before app_idle
 */
void os_start(
   os_initproc_t app_init,
   os_initproc_t app_idle);

void os_task_create(
   os_task_t *task,
   uint_fast8_t prio,
   void *stack,
   size_t stack_size,
   os_taskproc_t proc,
   void* param);

int os_task_join(os_task_t *task);

void os_task_check(os_task_t *task);

/** System tick function
 *
 * This function need to be called from ISR. It kicks the timer subsystem and
 * also trigers the preemption mechanism
 *
 * @pre can be called only from ISR
 */
void OS_HOT os_tick(void);

/** Function halt the system
 *
 * The main purpose of this function is to lock the execution in case of
 * critical error. This function can be used as final stage of user assert
 * failure code.
 */

void OS_COLD os_halt(void);

#endif

