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

#ifndef __OS_SEM_
#define __OS_SEM_

#define OS_SEMTIMEOUT_INFINITE UINT_MAX
#define OS_SEMTIMEOUT_TRY 0

typedef struct os_sem_tag {
   /* Queue of threads suspended on this semaphore */
   os_taskqueue_t task_queue;

   /* Semaphore value (initialiy it was uint_fast8_t but os_atomic_t is the only
    * sfe type here, becouse semaphores can be incremented from ISR) */
   os_atomic_t value;
} os_sem_t;

void os_sem_create(os_sem_t* sem, os_atomic_t init_cnt);
/** \NOTE calling this function for semaphores which are also used in ISR is
 *        highly forbiden since it will crash your kernel (ISR will access to
 *        data which will be destroyed) */
void os_sem_destroy(os_sem_t* sem);
os_retcode_t OS_WARN_UNUSEDRET os_sem_down(os_sem_t* sem, uint_fast16_t timeout_ticks);
void os_sem_up(os_sem_t* sem);

#endif

