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
 *
 * Derived from FreeBSD's bufring.c
 *
 **************************************************************************
 *
 * Copyright (c) 2007,2008 Kip Macy kmacy@freebsd.org
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. The name of Kip Macy nor the names of other
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ***********************license end**************************************/

#include "os_private.h"


static void ring_enqueue(
   void** OS_RESTRICT ring,
   void* const * OS_RESTRICT obj,
   arch_ridx_t cnt_whole,
   arch_ridx_t mask,
   arch_ridx_t prod_head)
{
   arch_ridx_t size = mask + 1;
   arch_ridx_t io = prod_head & mask;
   arch_ridx_t cnt1 = os_min(cnt_whole, (arch_ridx_t)(size - io));
   arch_ridx_t cnt2 = cnt_whole - cnt1;

      void** OS_RESTRICT oring = &ring[io];
      do {
          *(oring++) = *(obj++);
      } while (--cnt1);

      if (OS_UNLIKELY(cnt2 > 0)) {
	      oring = ring;
	      do {
		  *(oring++) = *(obj++);
	      } while (--cnt2);
      }
}

static void ring_dequeue(
   void** OS_RESTRICT obj,
   void* const * OS_RESTRICT ring,
   arch_ridx_t cnt_whole,
   arch_ridx_t mask,
   arch_ridx_t cons_head)
{
   arch_ridx_t size = mask + 1;
   arch_ridx_t ii = cons_head & mask;
   arch_ridx_t cnt1 = os_min(cnt_whole, (arch_ridx_t)(size - ii));
   arch_ridx_t cnt2 = cnt_whole - cnt1;

      void* const * OS_RESTRICT iring = &ring[ii];
      do {
          *(obj++) = *(iring++);
      } while (--cnt1);

      if (OS_UNLIKELY(cnt2 > 0)) {
	      iring = ring;
	      do {
		  *(obj++) = *(iring++);
	      } while (--cnt2);
      }
}

void ring_init(
   ring_t *ring,
   void *mem[],
   arch_ridx_t size)
{
   /* size must be a power of 2 */
   OS_ASSERT(os_power_of_2(size));

   ring->ring = mem;
   ring->mask = size - 1;
   ring->prod.head = 0;
   ring->cons.head = 0;
   ring->prod.tail = 0;
   ring->cons.tail = 0;
}

arch_ridx_t ring_mp_enq(
   ring_t *r,
   void * const obj[],
   arch_ridx_t cnt_max)
{
   arch_ridx_t cnt;
   arch_ridx_t prod_head, prod_next;
   arch_ridx_t cons_tail, free = 0;
   arch_ridx_t mask = r->mask;

   /* since we spin on prod_tail we cannot operate from ISR */
   OS_ASSERT(0 == isr_nesting);
   OS_ASSERT(cnt_max > 0); /* no sense to call function if cnt == 0 */

   /* move prod.head atomically */
   do {
      cons_tail = os_atomic_load(&r->cons.tail);
      prod_head = os_atomic_load(&r->prod.head);

      free = (cons_tail - prod_head - 1) & mask;

      /* calculate how many entries we can store */
      cnt = cnt_max;
      if (OS_UNLIKELY(cnt > free)) {
          if (OS_UNLIKELY(free == 0))
            return 0;
         cnt = free;
      }

      prod_next = prod_head + cnt;
   } while (OS_UNLIKELY(os_atomic_cmp_exch(&r->prod.head, &prod_head,
                                              prod_next)));

   /* write entries in to the ring */
   if (OS_LIKELY(cnt == 1)) {
      r->ring[prod_head & mask] = obj[0];
      /* fast path */
   } else {
      ring_enqueue(r->ring, obj, cnt, mask, prod_head);
   }

   /* If there are other enqueues in progress that preceded us, we need to wait
    * for them to complete (for other task). Since before we run we might only
    * preempt task with the same priority, it is enough that we call os_yied()
    * to schedule those tasks */
   while (OS_UNLIKELY(os_atomic_load(&r->prod.tail) != prod_head))
      os_yield();
   os_atomic_store(&r->prod.tail, prod_next);

   return cnt;
}

arch_ridx_t ring_sp_enq(
   ring_t *r,
   void * const obj[],
   arch_ridx_t cnt)
{
   arch_ridx_t prod_head, cons_tail;
   arch_ridx_t prod_next, free;
   arch_ridx_t mask = r->mask;

   OS_ASSERT(cnt > 0); /* no sense to call function if cnt == 0 */

   cons_tail = os_atomic_load(&r->cons.tail);
   prod_head = os_atomic_load(&r->prod.head);

   free = (cons_tail - prod_head - 1) & mask;

   /* calculate how many entries we can store */
   if (OS_UNLIKELY(cnt > free)) {
      if (OS_UNLIKELY(free == 0))
         return 0;
      cnt = free;
   }

   prod_next = prod_head + cnt;
   /* we need to update the prod.head even if technically it is not needed, to
    * keep indexes compatible, just in case user would like to switch in runtime
    * to ring_mp_enq() */
   os_atomic_store(&r->prod.head, prod_next);

   /* write entries in to the ring */
   if (OS_LIKELY(cnt == 1)) {
      /* fast path */
      r->ring[prod_head & mask] = obj[0];
   } else {
      ring_enqueue(r->ring, obj, cnt, mask, prod_head);
   }

   /* Release our entries and the memory they refer to */
   os_atomic_store(&r->prod.tail, prod_next);

   return cnt;
}

arch_ridx_t ring_mc_deq(
   ring_t *r,
   void *obj[],
   arch_ridx_t cnt_max)
{
   arch_ridx_t cnt;
   arch_ridx_t cons_head, prod_tail;
   arch_ridx_t cons_next, entries;
   arch_ridx_t mask = r->mask;

   /* since we spin on cons_tail we cannot operate from ISR */
   OS_ASSERT(0 == isr_nesting);
   OS_ASSERT(cnt_max > 0); /* no sense to call function if cnt == 0 */

   /* move cons.head atomically */
   do {
      cons_head = os_atomic_load(&r->cons.head);
      prod_tail = os_atomic_load(&r->prod.tail);

      entries = (prod_tail - cons_head) & mask;

      /* calculate how many entries we can dequeue */
      cnt = cnt_max;
      if (entries < cnt) {
         if (OS_UNLIKELY(entries == 0))
            return 0;
         cnt = entries;
      }

      cons_next = cons_head + cnt;
   } while (OS_UNLIKELY(os_atomic_cmp_exch(&r->cons.head, &cons_head,
                                              cons_next)));

   /* copy entries from the ring */
   if (OS_LIKELY(cnt == 1)) {
      /* fast path */
      obj[0] = r->ring[cons_head & mask];
   } else {
      ring_dequeue(obj, r->ring, cnt, mask, cons_head);
   }

   /* If there are other dequeues in progress that preceded us, we need to wait
    * for them to complete */
   while (OS_UNLIKELY(os_atomic_load(&r->cons.tail) != cons_head))
      os_yield();
   os_atomic_store(&r->cons.tail, cons_next);

   return cnt;
}

arch_ridx_t ring_sc_deq(
   ring_t *r,
   void *obj[],
   arch_ridx_t cnt)
{
   arch_ridx_t cons_head, prod_tail;
   arch_ridx_t cons_next, entries;
   arch_ridx_t mask = r->mask;

   OS_ASSERT(cnt > 0); /* no sense to call function if cnt == 0 */

   cons_head = os_atomic_load(&r->cons.head);
   prod_tail = os_atomic_load(&r->prod.tail);

   entries = (prod_tail - cons_head) & mask;

   /* calculate how many entries we can dequeue */
   if (entries < cnt) {
      if (OS_UNLIKELY(entries == 0))
         return 0;
      cnt = entries;
   }

   cons_next = cons_head + cnt;
   /* we need to update the cons.head even if technically it is not needed, to
    * keep indexes compatible, just in case user would like to switch in runtime
    * to ring_mc_deq() */
   os_atomic_store(&r->cons.head, cons_next);

   /* copy entries from the ring */
   if (OS_LIKELY(cnt == 1)) {
      /* fast path */
      obj[0] = r->ring[cons_head & mask];
   } else {
      ring_dequeue(obj, r->ring, cnt, mask, cons_head);
   }

   os_atomic_store(&r->cons.tail, cons_next);

   return cnt;
}

/**
 * Return the number of entries in a ring.
 * /note The result can be used only for statistic purpose due to atomic nature
 *       of head and tail
 */
arch_ridx_t ring_cnt(ring_t *r)
{
   arch_ridx_t prod_tail = os_atomic_load(&r->prod.tail);
   arch_ridx_t cons_tail = os_atomic_load(&r->cons.tail);
   return (prod_tail - cons_tail) & r->mask;
}

/**
 * Return the number of free entries in a ring.
 * /note The result can be used only for statistic purpose due to atomic nature
 *       of head and tail
 */
arch_ridx_t ring_free(ring_t *r)
{
   arch_ridx_t prod_tail = os_atomic_load(&r->prod.tail);
   arch_ridx_t cons_tail = os_atomic_load(&r->cons.tail);
   return (cons_tail - prod_tail - 1) & r->mask;
}

