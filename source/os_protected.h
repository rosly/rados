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

/* This file contains all definitions used by various kernel modules which has
 * to be exposed for user view (from some reason). Even if they are axposed for
 * user those definitions are intended to be used only in internal OS code.
 * They are exposed mainly because of OS API use them. */

#ifndef __OS_PROTECTED_
#define __OS_PROTECTED_

#include "os.h" /* to include all public definitions */

/* --- OS macro definitions --- */

#define OS_XSTR(s) #s
#define OS_STR(s) OS_XSTR(s)
#define OS_XCONCAT(a, b) a##b
#define OS_CONCAT(a, b) OS_XCONCAT(a, b)

#define OS_STATIC_ASSERT(_e) \
  enum { OS_CONCAT(static_assert_, __LINE__) = 1/(!!(_e)) }
//char OS_CONCAT(static_assert_, __LINE__)[0 - 1*!(_e)];

/** Definition of system atomic value, it need to at least 8bits wide, unsigned */
typedef volatile arch_atomic_t os_atomic_t;
OS_STATIC_ASSERT(ARCH_ATOMIC_MAX >= (UINT8_MAX - 1));
#define OS_ATOMIC_MAX ARCH_ATOMIC_MAX

/** Definition of system tick, it is defined by arch but never can be smaller
 * than uint16_t */
typedef arch_ticks_t os_ticks_t;
OS_STATIC_ASSERT(sizeof(os_ticks_t) >= sizeof(uint16_t));
OS_STATIC_ASSERT(ARCH_TICKS_MAX >= (UINT16_MAX - 1));
#define OS_TICKS_MAX ARCH_TICKS_MAX

#endif

