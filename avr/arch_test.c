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

#include <os_private.h>
#include <os_test.h>

test_tick_clbck_t test_tick_clbck = NULL;
int result_store;

/* for documentation check arch_test.h */
void test_debug_printf(const OS_PROGMEM char* OS_UNUSED(format), ...)
{
}

/* for documentation check arch_test.h */
void test_result(int result)
{
  result_store = result;
   if(0 == result) {
      test_debug_printf("Test PASSED\n");
   } else {
      test_debug_printf("Test FAILURE\n");
   }

   arch_halt();
}

/* for documentation check arch_test.h */
void test_setupmain(const OS_PROGMEM char* OS_UNUSED(test_name))
{
   //test_assert(0); /* missing implementation */
}

/* for documentation check arch_test.h */
void test_setuptick(test_tick_clbck_t OS_UNUSED(clbck), unsigned long OS_UNUSED(nsec))
{
   test_assert(0); /* missing implementation */
}

/* for documentation check arch_test.h */
void test_reqtick(void)
{
   test_assert(0); /* missing implementation */
}

