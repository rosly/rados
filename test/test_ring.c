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

/**
 * /file Test os message box rutines
 * /ingroup tests
 *
 * /{
 */

#include "os.h"
#include "os_test.h"

#include <inttypes.h>

#define TEST_RING_SIZE ((arch_ridx_t)512)
#define TEST_SET_SIZE   ((arch_ridx_t)256)
#define TEST_ENQ_SIZE   ((arch_ridx_t)128)
#define TEST_STRESS_SIZE ((arch_ridx_t)32)

#define TEST_PRIO_LOW  (1)
#define TEST_PRIO_MED  (2)
#define TEST_PRIO_HIGH (3)
#define TEST_PRIO_CORD (OS_CONFIG_PRIOCNT - 1)
OS_STATIC_ASSERT(TEST_PRIO_CORD > TEST_PRIO_HIGH);

static os_task_t task_worker[4];
static OS_TASKSTACK task_stack[4][OS_STACK_MINSIZE];
static os_task_t task_coordinator;
static OS_TASKSTACK coordinator_stack[OS_STACK_MINSIZE];

static ring_t test_ring;
static void* test_ring_buff[TEST_RING_SIZE];

void test_idle(void)
{
   /* nothing to do */
}

void test_empty_tick(void)
{
}

void test_fill_ring(ring_t *r, arch_ridx_t cnt)
{
   arch_ridx_t ret;
   arch_ridx_t i;
   void* obj[cnt];

   for (i = 0; i < cnt; i++)
      obj[i] = (void*)(uintptr_t)i;

   ret = ring_sp_enq(r, obj, cnt);
   test_assert(ret == cnt);
}

void test_overflow_ring(ring_t *r, arch_ridx_t cnt)
{
   arch_ridx_t ret;
   arch_ridx_t i;
   void* obj[cnt];

   for (i = 0; i < cnt; i++)
      obj[i] = (void*)(uintptr_t)i;

   ret = ring_sp_enq(r, obj, cnt);
   test_assert(ret == (cnt - 1));
}

void test_verify_ring(ring_t *r, arch_ridx_t cnt)
{
   arch_ridx_t ret;
   arch_ridx_t i;
   uint8_t checks[cnt];
   void* obj[cnt];

   /* initialize the reference counter table */
   memset(checks, 0, sizeof(uint8_t) * cnt);

   /* get out everything from the ring */
   ret = ring_sc_deq(r, obj, cnt);
   test_assert(ret == cnt);
   test_assert(ring_cnt(r) == 0);

   /* check if only one (and exactly one) reference of each object exist */
   for (i = 0; i < cnt; i++) {
      test_verbose_debug("msq %"PRIu16" -> %"PRIu16,
			 (uint16_t)i, (uint16_t)(uintptr_t)obj[i]);
   }
   test_verbose_debug("");
   for (i = 0; i < cnt; i++) {
      test_assert(checks[(uint_fast16_t)(uintptr_t)(obj[i])] == 0);
      checks[(uint_fast16_t)(uintptr_t)(obj[i])]++;
   }
   for (i = 0; i < cnt; i++) {
      test_assert(checks[(uint_fast16_t)(uintptr_t)(obj[i])] == 1);
   }

   /* put everything back to the ring */
   ret = ring_sp_enq(r, obj, cnt);
   test_assert(ret == cnt);
}

void test_simple(void)
{
   uint64_t sum = 0;
   arch_ridx_t cnt;
   arch_ridx_t ret;
   arch_ridx_t prog = 1;
   void* obj[TEST_ENQ_SIZE];

   /* simple enq and deq */
   ring_init(&test_ring, test_ring_buff, TEST_RING_SIZE);
   test_fill_ring(&test_ring, TEST_ENQ_SIZE);
   test_verify_ring(&test_ring, TEST_ENQ_SIZE);

   /* enqueue maximal number of elements */
   ring_init(&test_ring, test_ring_buff, TEST_RING_SIZE);
   test_fill_ring(&test_ring, TEST_RING_SIZE - 1);
   test_verify_ring(&test_ring, TEST_RING_SIZE - 1);
   /* test overflow */
   ring_init(&test_ring, test_ring_buff, TEST_RING_SIZE);
   test_overflow_ring(&test_ring, TEST_RING_SIZE);
   test_verify_ring(&test_ring, TEST_RING_SIZE - 1);

   /* several deq-enq */
   ring_init(&test_ring, test_ring_buff, TEST_RING_SIZE);
   test_fill_ring(&test_ring, TEST_SET_SIZE);

   do {
      cnt = ring_mc_deq(&test_ring, obj, prog);
      test_assert(cnt > 0);

      ret = ring_mp_enq(&test_ring, obj, cnt);
      test_assert(cnt == ret);

      test_verify_ring(&test_ring, TEST_SET_SIZE);

      sum += cnt;
      /* increase the pooling set size to check also the bulk copy and speed up the test */
      if (sum > (TEST_SET_SIZE * 2))
         prog = TEST_ENQ_SIZE;

   /* repeat until we loop the buffer and loop the arch_ridx_t */
   } while (sum < ((uint64_t)ARCH_RIDX_MAX * 2));
}

int test_stress_task(void *param)
{
   uint64_t sum = 0;
   uintptr_t thri = (uintptr_t)param;
   arch_ridx_t cnt;
   arch_ridx_t ret;
   uint8_t loop;
   void* obj[TEST_STRESS_SIZE];

   do {
      /* dequeue by multiple calls to increase the probability of race
       * conditions during task switch */
      cnt = 0;
      do {
         ret = ring_mc_deq(&test_ring, &obj[cnt], 1);
         test_assert(ret == 1);

	 /* add some pseudo random busy-wait delay to increase the probability
	  * of race conditions */
	 for (loop = (uint8_t)(uintptr_t)(obj[cnt]); loop > 0; loop--);

	 cnt++;

      } while (cnt < TEST_STRESS_SIZE);

      sum += cnt;

      /* enqueue by multiple calls to increase the probability of race
       * conditions during task switch */
      do {
         ret = ring_mp_enq(&test_ring, &obj[cnt - 1], 1);
         test_assert(ret == 1);

	 /* add some pseudo random busy-wait delay to increase the probability
	  * of race conditions */
	 for (loop = (uint8_t)(uintptr_t)(obj[cnt - 1]); loop > 0; loop--);

	 cnt--;
      } while (cnt > 0);

   } while (sum < ((uint64_t)ARCH_RIDX_MAX * 2));

   return (int)thri;
}

void test_stress(void)
{
   uint_fast8_t i;

   ring_init(&test_ring, test_ring_buff, TEST_RING_SIZE);
   /* initialize ring with some objects
    * task will shuffle those, then we will verify if none of them was lost
    * this will verify that algorithm has no race conditions */
   test_fill_ring(&test_ring, TEST_SET_SIZE);

   /* craete frequent tick to maximize the possibility of race conditions */
   test_setuptick(test_empty_tick, 1);

   for (i = 0; i < 4; i++) {
      os_task_create(
         &task_worker[i], TEST_PRIO_LOW,
         task_stack[i], sizeof(task_stack[i]),
         test_stress_task, (void*)(uintptr_t)i);
   }

   /* scheduler will kick in and allow first task to run after following call */
   for (i = 0; i < 4; i++)
      os_task_join(&task_worker[i]);

   /* now verify that all messages are still in ring (we didn't loose anything
    * due to possible race conditions and also did not duplicated anything */
   test_verify_ring(&test_ring, TEST_SET_SIZE);
}

/**
 * Test coordinator, runs all test in unit
 */
int test_coordinator(void *OS_UNUSED(param))
{
   test_simple();
   test_stress();

   test_result(0);
   return 0;
}

void test_init()
{
   /* coordination task has the most high prio */
   os_task_create(
      &task_coordinator, TEST_PRIO_CORD,
      coordinator_stack, sizeof(coordinator_stack),
      test_coordinator, NULL);
}

int main(void)
{
   os_init();
   test_setupmain("Test_Ring");
   test_init();
   os_start(test_idle);

   return 0;
}

/** /} */

