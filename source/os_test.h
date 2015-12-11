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

#ifndef __OS_TEST_
#define __OS_TEST_

/**
 * Macro __FILENAME__ should contain the name of the file without directory
 * path. This macro could be set at makefile target level as part of compiler
 * argument command line (like following -D__FILENAME__=$(notdir $<). This is a
 * trick to provide the short file name to the C source because GCC only
 * provides the full file name (with path) by __FILE__ macro.
 * This is usefull for embedded platforms where we would like to keep debug
 * strings short die to flash memory constrains as also due (usualy) slow debug
 * interface */
#ifdef __FILENAME__
#define test_debug(format, ...) \
  test_debug_printf(OS_PROGMEM_STR(OS_STR(__FILENAME__) ":" OS_STR(__LINE__) " " format "\r\n"), ##__VA_ARGS__)
#else
#define test_debug(format, ...) \
  test_debug_printf(OS_PROGMEM_STR(__FILE__ ":" OS_STR(__LINE__) " " format "\r\n"), ##__VA_ARGS__)
#endif

typedef void (*test_tick_clbck_t)(void);

/**
 * Debug function for test verbose output
 *
 * This function is setup and architecture dependent and should pass the
 * verbose debug output to console like interface (for instance serial port)
 */
void test_debug_printf(const OS_PROGMEM char* format, ...);

/**
 * Function notifies Human User Interface about test result
 *
 * Since not all architectures use console output, there can be cases where
 * only the test result is important (eor e.g automatic test benches).
 *
 * @param result result of the test, 0 success, != 0 failure
 */
void test_result(int result);

/**
 * Function setups all things needed for particular architecture to run
 *
 * Details:
 * Function setups the signal disposition tick signal, but do not
 * create the tick timer. This allows tests to handle the manual tick requests
 * (if needed) but also can be used for periodic tick setup (which is more
 * usual case). For periodic tick setup test should call test_setuptick */
void test_setupmain(const OS_PROGMEM char* test_name);

/**
 * Function starts the periodic timer
 *
 * @precondition test_setupmain was called beforehand
 */
void test_setuptick(test_tick_clbck_t clbck, unsigned long nsec);

/**
 * Function forces manual tick
 *
 * This function is used for fine checking the OS routines which base on tick
 *
 * @precondition test_setupmain was called beforehand
 */
void test_reqtick(void);

#include "arch_test.h"

#endif /* __OS_TEST_ */

