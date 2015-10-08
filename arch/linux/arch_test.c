/*
 * This file is a part of RadOs project
 * Copyright (c) 2013, Radoslaw Biernacki <radoslaw.biernacki@gmail.com>
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

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "os_private.h"
#include "os_test.h"

static timer_t timer; /**< linux POSIX timer used as an emulation of tick */
static test_tick_clbck_t test_tick_clbck = NULL; /**< additional callback for emulated tick */
static const char* test_name = NULL;

/** Linux architecture dependend. Signal handler function for timer tick
 * emulation
 */
static void OS_ISR sig_alrm(int OS_UNUSED(signum), siginfo_t * OS_UNUSED(siginfo), void *ucontext)
{
   arch_contextstore_i(sig_alrm);
   os_tick();
   if( NULL != test_tick_clbck )
   {
      test_tick_clbck();
   }
   arch_contextrestore_i(sig_alrm);
}

/* for documentation check os_test.h */
void test_debug_printf(const char* format, ...)
{
   va_list ap;

   va_start(ap, format);
   //printf("%s: ", test_name);
   vprintf(format, ap);
   printf("\n");
   va_end(ap);
}

/* for documentation check os_test.h */
void test_result(int result)
{
   if(0 == result) {
      test_debug_printf("%s: Test PASSED\n", test_name);
   } else {
      test_debug_printf("%s: Test FAILURE\n", test_name);
   }

   arch_dint();
   exit(result);
}

/* for documentation check os_test.h */
void test_setupmain(const char* name)
{
   int ret;
   struct sigaction tick_sigaction = {
      .sa_sigaction = sig_alrm,
      .sa_mask = { { 0 } }, /* additional (beside the current signal) mask (they will be added to the mask instead of set) */
      .sa_flags = SA_SIGINFO , /* use sa_sigaction instead of old sa_handler */
      /* SA_NODEFER could be used if we would like to have the nesting enabled right durring the signal handler enter */
      /* SA_ONSTACK could be sed if we would like to use the signal stack instead of thread stack */
   };

   ret = sigaction(SIGALRM, &tick_sigaction, NULL);
   test_assert(0 == ret);
   test_name = name;
}

/* for documentation check os_test.h */
void test_setuptick(test_tick_clbck_t clbck, unsigned long nsec)
{
   int ret;
   struct sigevent sev = {
      .sigev_notify = SIGEV_SIGNAL,
      .sigev_signo = SIGALRM,
   };
   struct itimerspec its = {
      .it_interval = {
         .tv_sec = 0,
         .tv_nsec = nsec,
      },
      .it_value = {
         .tv_sec = 0,
         .tv_nsec = nsec,
      }
   };

   /* create and start the kernel periodic timer
      assign the SIGALRM signal to the timer */

   test_assert(nsec > 0); /* nsec must be greater than 0 for linux timer API */
   test_tick_clbck = clbck;

   ret = timer_create(CLOCK_PROCESS_CPUTIME_ID, &sev, &timer);
   test_assert(0 == ret);
   ret = timer_settime(timer, 0, &its, NULL);
   test_assert(0 == ret);
}

/* for documentation check os_test.h */
void test_reqtick(void)
{
   raise(SIGALRM);
}

