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

#ifndef __OS_MBOX_
#define __OS_MBOX_

typedef enum {
   OS_MQUEUE_SPSC = 0,
   OS_MQUEUE_SPMQ,
   OS_MQUEUE_MPSC,
   OS_MQUEUE_MPMQ
} os_mqueue_type_t;

/** Definition of mbox structure
 *
 * Message box can hold only one message. During the send operation, sender may
 * decide if he would like to try to post message and get the error code in case
 * mbox is already occupied, or if he would like to push (overwrite) new the
 * message into the mbox regardless of current mbox state
 */
typedef struct {
   os_waitqueue_t wait_queue;
   void * volatile msg;
} os_mbox_t;

typedef arch_ridx_t os_mqueue_enq_t(
   ring_t *r,
   void * const obj[],
   arch_ridx_t cnt);

typedef arch_ridx_t os_mqueue_deq_t(
   ring_t *r,
   void *obj[],
   arch_ridx_t cnt);

typedef struct {
   os_waitqueue_t wait_queue;
   ring_t ring;
   os_mqueue_enq_t *enq;
   os_mqueue_deq_t *deq;
} os_mqueue_t;

void os_mbox_create(os_mbox_t* mbox, void *init_msg);

void os_mbox_destroy(os_mbox_t* mbox);

os_retcode_t OS_WARN_UNUSEDRET os_mbox_pop(
   os_mbox_t *mbox,
   void **msg,
   uint_fast16_t timeout_ticks);

void* OS_WARN_UNUSEDRET os_mbox_push(
   os_mbox_t *mbox,
   void *msg,
   bool sync);

os_retcode_t OS_WARN_UNUSEDRET os_mbox_post(
   os_mbox_t* mbox,
   void* msg,
   bool sync);

void os_mqueue_create(
   os_mqueue_t *mqueue,
   void *mem[],
   arch_ridx_t size,
   os_mqueue_type_t type);

void os_mqueue_destroy(os_mqueue_t *mqueue);

os_retcode_t OS_WARN_UNUSEDRET os_mqueue_pop(
   os_mqueue_t *mqueue,
   void *msg[],
   arch_ridx_t *cnt,
   uint_fast16_t timeout_ticks);

arch_ridx_t OS_WARN_UNUSEDRET os_mqueue_post(
   os_mqueue_t *mqueue,
   void *msg[],
   arch_ridx_t cnt,
   bool sync);

#endif

