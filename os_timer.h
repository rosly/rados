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

#ifndef __OS_TIMER_
#define __OS_TIMER_

#define OS_TIMEOUT_INFINITE (ARCH_TICKS_MAX)
#define OS_TIMEOUT_TRY ((os_ticks_t)0)

/** definition of system tick, it is defined by arch but never can be smaller than
 * uint16_t */
typedef arch_ticks_t os_ticks_t;
#if sizeof(os_ticks_t) < sizeof(uint16_t)
#error os_ticks_t cannot be smaller than uint16_t
#endif

typedef void (*timer_proc_t)(void* param);

typedef struct {
   list_t list;
   os_ticks_t ticks_rem;
   os_ticks_t ticks_reload;
   timer_proc_t clbck;
   void* param;
} os_timer_t;

typedef struct
{
  os_ticks_t ticks_start;
  os_ticks_t ticks_rem;
} os_timeout_t;

/** current ticks counter, increamented each tick ISR call
 *  used for time keeping */
extern os_ticks_t os_ticks_cnt;

void os_timer_create(os_timer_t* timer, timer_proc_t clbck, void* param, uint_fast16_t timeout_ticks, uint_fast16_t reload_ticks);
void os_timer_destroy(os_timer_t* timer);

static inline void os_ticks_now(os_ticks_t *ticks_now)
{
  arch_ticks_atomiccpy(ticks_now, &os_ticks_cnt);
}

os_ticks_t os_ticks_diff(os_ticks_t* restrict tick_start);
void os_timeout_start(os_timeout_t* restrict timeout, unsigned timeout_ms);
int os_timeout_check(os_timeout_t* restrict timeout);

#endif

