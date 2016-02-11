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

/* --- public functions --- */
/* all public functions are documented in os_mbox.h file */

void os_mbox_create(os_mbox_t *mbox, void *init_msg)
{
   /* cannot call after os_waitqueue_prepare() */
   OS_ASSERT(NULL == waitqueue_current);

   os_waitqueue_create(&(mbox->wait_queue));
   mbox->msg = init_msg;
}

void os_mbox_destroy(os_mbox_t *mbox)
{
   OS_ASSERT(0 == isr_nesting); /* cannot call from ISR */
   /* cannot call after os_waitqueue_prepare() */
   OS_ASSERT(NULL == waitqueue_current);

   os_waitqueue_destroy(&(mbox->wait_queue));
   memset(mbox, 0xff, sizeof(os_mbox_t));
}

os_retcode_t OS_WARN_UNUSEDRET os_mbox_pop(
   os_mbox_t *mbox,
   void **msg,
   uint_fast16_t timeout_ticks)
{
   os_retcode_t retc;

   OS_ASSERT(0 == isr_nesting); /* cannot call from ISR */
   /* idle task cannot call blocking functions (will crash OS) */
   OS_ASSERT(task_current->prio_current > 0);
   /* cannot call after os_waitqueue_prepare() */
   OS_ASSERT(!waitqueue_current);

   do
   {
      os_waitqueue_prepare(&mbox->wait_queue);
      /* try to atomically fetch the message */
      if ((*msg = os_atomic_exch(&mbox->msg, NULL)))
      {
         /* got the message */
         os_waitqueue_break();
         retc = OS_OK;
         break;
      }
      /* no message in mbox, wait for message post */
      retc = os_waitqueue_wait(timeout_ticks);
   } while (retc == OS_OK); /* wakeup code ? */

   return retc;
}

void* OS_WARN_UNUSEDRET os_mbox_push(
   os_mbox_t *mbox,
   void *msg,
   bool sync)
{
   void* prev_msg;

   /* sync must be == false in case we are called from ISR */
   OS_ASSERT((isr_nesting == 0) || (sync == false));
   /* cannot call after os_waitqueue_prepare() in case of task context */
   OS_ASSERT((isr_nesting > 0) || !waitqueue_current);

   prev_msg = os_atomic_exch(&mbox->msg, msg);
   /* since there is only 1 message we woke up only 1 task */
   os_waitqueue_wakeup_sync(&mbox->wait_queue, 1, sync);

   return prev_msg;
}

os_retcode_t OS_WARN_UNUSEDRET os_mbox_post(
   os_mbox_t *mbox,
   void *msg,
   bool sync)
{
   void *msg_prev;

   /* sync must be == false in case we are called from ISR */
   OS_ASSERT((isr_nesting == 0) || (sync == false));
   /* cannot call after os_waitqueue_prepare() in case of task context */
   OS_ASSERT((isr_nesting > 0) || !waitqueue_current);

   /* try atomicaly set the messag ptr in case it is currently NULL */
   msg_prev = NULL;
   if (os_atomic_cmp_exch(&mbox->msg, &msg_prev, msg))
      return OS_BUSY; /* there is existing message */

   /* messasge posted, wake up receiver task */
   os_waitqueue_wakeup_sync(&mbox->wait_queue, OS_WAITQUEUE_ALL, sync);
   return OS_OK;
}

void os_mqueue_create(
   os_mqueue_t *mqueue,
   void *mem[],
   arch_ridx_t size,
   os_mqueue_type_t type)
{
   /* cannot call after os_waitqueue_prepare() */
   OS_ASSERT(NULL == waitqueue_current);

   os_waitqueue_create(&(mqueue->wait_queue));
   ring_init(&mqueue->ring, mem, size);

   if ((OS_MQUEUE_SPSC == type) || (OS_MQUEUE_SPMQ == type))
      mqueue->enq = ring_sp_enq;
   else
      mqueue->enq = ring_mp_enq;

   if ((OS_MQUEUE_SPSC == type) || (OS_MQUEUE_MPSC == type))
      mqueue->deq = ring_sc_deq;
   else
      mqueue->deq = ring_mc_deq;
}

void os_mqueue_destroy(os_mqueue_t *mqueue)
{
   OS_ASSERT(0 == isr_nesting); /* cannot call from ISR */
   /* cannot call after os_waitqueue_prepare() */
   OS_ASSERT(NULL == waitqueue_current);

   os_waitqueue_destroy(&(mqueue->wait_queue));
   memset(mqueue, 0xff, sizeof(os_mqueue_t));
}

os_retcode_t OS_WARN_UNUSEDRET os_mqueue_pop(
   os_mqueue_t *mqueue,
   void *msg[],
   arch_ridx_t *cnt,
   uint_fast16_t timeout_ticks)
{
   os_retcode_t retc;
   arch_ridx_t deq;

   OS_ASSERT(0 == isr_nesting); /* cannot call from ISR */
   /* idle task cannot call blocking functions (will crash OS) */
   OS_ASSERT(task_current->prio_current > 0);
   /* cannot call after os_waitqueue_prepare() */
   OS_ASSERT(!waitqueue_current);
   OS_ASSERT(*cnt > 0); /* no sense to call this function otherwise */

   do
   {
      os_waitqueue_prepare(&mqueue->wait_queue);

      deq = mqueue->deq(&mqueue->ring, msg, *cnt);
      if (deq)
      {
         /* got the messages */
         os_waitqueue_break();
         retc = OS_OK;
         *cnt = deq;
         break;
      }
      /* no message in mqueue, wait for message post */
      retc = os_waitqueue_wait(timeout_ticks);
   } while (retc == OS_OK); /* wakeup code ? */

   return retc;
}

arch_ridx_t OS_WARN_UNUSEDRET os_mqueue_post(
   os_mqueue_t *mqueue,
   void *msg[],
   arch_ridx_t cnt,
   bool sync)
{
   arch_ridx_t ret;

   /* sync must be == false in case we are called from ISR */
   OS_ASSERT((isr_nesting == 0) || (sync == false));
   /* cannot call after os_waitqueue_prepare() in case of task context */
   OS_ASSERT((isr_nesting > 0) || !waitqueue_current);
   OS_ASSERT(cnt > 0); /* no sense to call this function otherwise */

   ret = mqueue->enq(&mqueue->ring, msg, cnt);

   if (ret > 0) {
      /* message posted, wake up receiver task */
      os_waitqueue_wakeup_sync(&mqueue->wait_queue, OS_WAITQUEUE_ALL, sync);
   }

   return ret;
}

