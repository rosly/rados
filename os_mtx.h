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

#ifndef __OS_MTX_
#define __OS_MTX_

/**
   Mutex are second synchronization primitive. Comparing to semaphores it has following differences:
   - mutex has only two states locked/unlocked while semaphore has multiple values
   - mutex has concept of ownership, so locked mutex can be unlocked only by the owner (task which lock it as a last one), unlocking from another task will reasult in assertion, this may be seen as error hecking feature of the mutex
   - mutex prevents from priority nversion problem while semaphores not. Prevention algotithm boost the priority of task that holds the mutex to value of most prioritized thread that trys to obtain the lock + 1
   - mutex suport the recusrive locks (it tracks the owner). To free the mutex it must be unlocked the same number of times as many lock operations was done
   - lock operation on mutex cannot fail (this is bcouse in case of failure often user cannot go ay further). From the same reason mutex lock operation does not have the timeout (detecting deadlocks by timeout is realy bad idea)
   - operations on mutex should be faster comparing to semaphore (to be verified)
   */

typedef struct {
   list_t listh; /**< list header that allows to place mtx'es on some list */
   os_task_t *owner; /**< Task which currently owns the mutex */
   os_taskqueue_t wait_queue; /**< Queue of threads suspended on this mutex */
   uint_fast8_t recur; /**< Recursive locks count for owner (we do not need to use sig_atomic_t here becouse mutexes cannot e accessed from ISR and also only the owner may modify it */
} os_mtx_t;

void os_mtx_create(os_mtx_t* mtx);
void os_mtx_destroy(os_mtx_t* mtx);
/**
   THis function returns an return value which should be checked. The reason for this is mutex removal from other thread */
os_retcode_t OS_WARN_UNUSEDRET os_mtx_lock(os_mtx_t* mtx);
void os_mtx_unlock(os_mtx_t* mtx);

#endif

