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

#ifndef __OS_SCHED_
#define __OS_SCHED_

typedef enum {
   TASKSTATE_RUNNING = 0,
   TASKSTATE_READY,
   TASKSTATE_WAIT,
   TASKSTATE_DESTROYED,
   TASKSTATE_INVALID, /* will be set after join to prevent from double join */
} os_taskstate_t;

typedef enum {
   OS_TASKBLOCK_INVALID = 0,
   OS_TASKBLOCK_SEM,
   OS_TASKBLOCK_MTX
} os_taskblock_t;

typedef enum {
   OS_OK = 0,
   OS_WOULDBLOCK,
   OS_TIMEOUT,
   OS_DESTROYED,

   OS_INVALID
} os_retcode_t;

struct os_sem_tag; /* forward declaration */
struct os_taskqueue_tag; /* forward declaration */

typedef struct {
   arch_context_t ctx; /* need to be the first field, because then it is easier to save the context, for more info about the context store see arch_context_t */

   list_t list; /**< list link, allows to place the task on some various lists */

   uint_fast8_t prio_base; /* the base priority, it is constant while all task life cycle, decided to use fast8 type sine we ned at least uint8 while we dont whant to have some additional penelty becouse of alligment issues, form other side it is nt realy connection to fast operations on priorities */
   uint_fast8_t prio_current; /* current priority of the task, it may be modified priority inversion code */

   os_taskstate_t state;
   struct {
      struct os_taskqueue_tag *task_queue; /* taskqueue to which the task belongs, this pointer is required id durring task enqueue/dequeue we have to modify the taskqueue object (this is the case for current taskqueue algorithm) */
      //void *block_obj; /* pointer to object which blocks the task *, valid only when task state == TASKSTATE_WAIT */
      os_taskblock_t block_type; /* defines on which object the task is blocked, valid only when task state == TASKSTATE_WAI */
      os_timer_t *timer; /* pointer to timer assosiated with task, valid only when task state == TASKSTATE_WAIT, it will be != NULL if task is waiting on some primitive with timeout guard */
   }; /**< data set while task is in TASKSTATE_WAIT or TASKSTATE_READY, in other words thise data are important only when taks is blocked or ready to run but not currently executing */
   list_t mtx_list; /**< list of owned mutexes, this list is requred to calculate new prio_current durring mutex unlock (while we drop priority), extensive explanation in os_mtx_unlock, keep in mnd that this list has nothing in common with block state of the task, this list may be either empty of ocupied both during state READY and WAIT, it just means that task locked some mutexes */

   struct os_sem_tag *join_sem; /* pointer to semaphore provided by task whih would like to be suspended until this task will finish, solving the os_task_join by using pointer to semaphore is much cheaper interms of resoures than kind of custom task_finish algorithm which uses doubly linked lists, also keep in mind that here we keept only the pointer while semaphore itself is stored on sack of task which will wait on this task finalization (we dont vaste memory for keepin semaphore structure here since it is needed only during finalization) */
   union {
      int ret_value; /* value which can be returned by task before it will be destroyed */
      os_retcode_t block_code; /* return code returned from blocking function, used to comunicate betwen owner and thread that wake it up */
   };

#ifdef OS_CONFIG_CHECKSTACK
   void *stack_end;
   size_t stack_size; /* neccessarly needed ? */
#endif
} os_task_t;

typedef struct os_taskqueue_tag {
   list_t tasks[OS_CONFIG_PRIOCNT]; /**< buckets of tasks, there are a separate list for each priority level, at those list task with the same prrity are placed, note for priority bootsing: in casof prioinversion we assign the prio_curr to level of the blocked task (not + 1), so we never exceed the OS_CONFIG_PRIOCNT limit */
   uint_fast8_t priomax; /**< priority of most important task in the waitqueue, using of this should increase throughput since we dont have to search the most important queue which is not empty, decided to use fast8 type sine we ned at least uint8 while we dont whant to have some additional penelty becouse of alligment issues, form other side it is nt realy connection to fast operations on priorities */
} os_taskqueue_t;

typedef void (*os_initproc_t)(void);
typedef int (*os_taskproc_t)(void* param);

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
void OS_HOT os_tick(void);

#endif

