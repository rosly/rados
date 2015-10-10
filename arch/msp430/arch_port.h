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

#ifndef __OS_PORT_
#define __OS_PORT_

//#define __CC430F6137__ similar deffine should be automaticly added by compiler when it will read the -mcu=cc430f6137, this define is required for legacymsp430.h

#include <legacymsp430.h>
#include <stdint.h> /* for patform specific types definitions like uint16_t */
#include <limits.h> /* for UINT_MAX etc */
#include <stddef.h>
#include <string.h> /* for memcpy */

/* missing stdbool.h for this platform */
typedef uint8_t bool;
#define false ((uint8_t)0)
#define true  ((uint8_t)1) /* considered also !false but finaly decided to be more strict */

/* Within an interrupt handler, set the given bits in the value of SR
 * that will be popped off the stack on return */
//void __bis_status_register_on_exit (uint16_t bits);

/* Following structure held CPU registers which need to be preserved between
 * context switches. In general (at any ARCH), there are two possible
 * approaches:
 * - store all CPU context sensitive registers in arch_context_t structure
 *   (which is placed at the beginning of os_task_t)
 * - store only stack pointer in arch_context_t structure, while storing CPU
 *  registers on the task stack
 * It is up to programmer (who prepare specific port) to chose the appropriate
 * approach (storing registers in arch_context_t uses more static task memory
 * while storing them on stack requires more stack memory).  Most of others RTOS
 * use the second method. This is mainly because storing registers stack is the
 * fastest way (require less CPU cycles). */
typedef struct {
    uint16_t sp;
} arch_context_t;

typedef uint16_t arch_atomic_t; /* exactly 16 bits. sig_atomic_t should be available from signal.h but on my enviroment signal.h was empty */
typedef uint16_t arch_ticks_t; /* exactly 16 bits */
#define ARCH_TICKS_MAX ((arch_ticks_t)UINT16_MAX)
typedef uint16_t arch_criticalstate_t; /* size of CPU status register */

#define OS_ISR __attribute__((naked)) ISR
#define OS_NAKED __attribute__((naked))
#define OS_NORETURN __attribute__ ((noreturn))
#define OS_PURE __attribute__ ((pure))
#define OS_HOT __attribute__ ((hot))
#define OS_COLD __attribute__ ((cold))
#define OS_WARN_UNUSEDRET __attribute__ ((warn_unused_result))
#define OS_LIKELY(_cond) __builtin_expect(!!(_cond), 1)
#define OS_UNLIKELY(_cond) __builtin_expect(!!(_cond), 0)
#define OS_UNUSED(_x) unused_ ## _x __attribute__((unused))
#define OS_RESTRICT __restrict__
#define OS_PROGMEM
#define OS_TASKSTACK uint8_t __attribute__ ((aligned (2)))

#define OS_STACK_DESCENDING
#define OS_STACK_MINSIZE ((size_t)28 * 4) /* four times of context dump size */

#define os_atomic_inc(_atomic) \
    __asm__ __volatile__ ( \
        "inc %[atomic]\n\t" \
            ::  [atomic] "m" (_atomic))

#define os_atomic_dec(_atomic) \
    __asm__ __volatile__ ( \
        "dec %[atomic]\n\t" \
            ::  [atomic] "m" (_atomic))

/* all pointers in MSP430 are size of CPU register, no need to read or write
 * with any concurent handling */
#define os_atomicptr_read(_ptr) (_ptr)
#define os_atomicptr_write(_ptr, _val) ((_ptr) = (_val))
#define os_atomicptr_xchnge(_ptr, _val) (OS_ASSERT(!"not implemented"))

#define arch_ticks_atomiccpy(_dst, _src) \
  do { \
      *(_dst) = *(_src); /* we can copy as rval since MSP430 is 16bit arch and arch_ticks_t is 16bit*/ \
  }while(0)

#define arch_critical_enter(_critical_state) \
   do { \
      (_critical_state) = __read_status_register(); \
      arch_dint(); \
   }while(0)

#define arch_critical_exit(_critical_state) \
   do { \
      /* __bis_status_register(GIE & (_critical_state)); modify only the IE flag, while remain rest untouched */ \
      __write_status_register(_critical_state); /* overwrite all flags, since previously we run proram then power bits were set (no risk) */ \
   }while(0)

#define arch_dint() dint()
#define arch_eint() eint()

/* format of the context pushed on stack for MSP430 port
low adress
    PC - pushed frst
    R3(SR) - pushed automaticly on ISR enter (manualy durring schedule form user)
    R4 - R15 - pusched last
hi address
*/

/* This function have to:
 - if neecessary, disable interrupts to block the neesting
 - store all registers (power control bits does not have to be necessarly stored)
 - increment the isr_nesting
 - if isr_nesting is = 1 then
    - store the context curr_tcb->ctx (may take benfit from already stored registers by storing only the stack pointer)
 - end

 - in some near future down in the ISR enable interrupts to support the nesting interrupts

 because ISR was called it means that interrupts was enabled. On some arch like MPS430 they may be automaticly disabled durring the enter to ISR
 On those architectures interrupts may be enabled when ISR will mask pending interrupt.
 In general disabling interrupt is usualy needed because we touch the task_current (usualy need 2 asm instructions) and we cannot be preempted by another interrupt.
 From other hand enabling the interrupts again as soon as possible is needed for realtime constrains.
 If your code does not need to be realtime constrained, it is not needed to enable the interrupts in ISR, also the nesting interrupt code can be disabled

 The reason why we skip the stack pointer storage in case of nesing is obvous. In case of nesting we was not in task but in other ISR. So the SP will not be the task SP.
 But we have to store all registers anyway. This is why we store all registers and then optionaly store the SP in context of tcb */
#define arch_contextstore_i(_isrName) \
    __asm__ __volatile__ ( \
        /* on MSP430 interrupts are disabled when entering ISR */ \
        "inc %[isr_nesting]\n\t" /* increment isr_nesting, here we destroy orginal SR but it already lay on stack*/ \
        "pushm 12,r15\n\t" /* pushing R4 -R15 */ \
        "cmp #1, %[isr_nesting]\n\t" /* check isr_nesting */ \
        "jne "#_isrName "_contextStoreIsr_Nested\n\t" \
        "mov %[ctx], r15\n\t" \
        "mov r1, @r15\n\t" \
#_isrName "_contextStoreIsr_Nested:\n\t" \
            ::  [ctx] "m" (task_current), \
                [isr_nesting] "m" (isr_nesting))

/* This function have to:
 - disable IE (in case we achritecture allows for nesting)
 - decrement the isr_nesting
 - if isr_nesting = 0 then
     - restore context from curr_tcb->ctx
 - restore all registers
 - perform actions that will lead to enable IE after reti
 - return by reti

 Please first read the note for arch_context_StoreI. The important point here is why we need to enable the interrupt after reti in both cases (in normal and nested).
 This is because we disabled them for task_current manipulation in first step. But we need to enable them because:
 - in case of not nested they was for sure enabled (need to be enabled because we enter ISR ;) )
 - in case of nested the was also for sure enabled (from the same reason, we enter nested ISR) */
#define arch_contextrestore_i(_isrName) \
    __asm__ __volatile__ ( \
        "dint\n\t" /* disable interrupts in case some ISR will implement nesting interrupt handling */ \
        "dec %[isr_nesting]\n\t" \
        "jnz "#_isrName "_contexRestoreIsr_Nested\n\t" \
        "mov %[ctx], r1\n\t" \
        "mov @r1, r1\n\t" \
        "popm 12,r15\n\t" /* poping R4 -R15 */ \
        "bic %[powerbits], @r1 \n\t" /* enable full power mode in SR that will be poped */ \
        "jmp "#_isrName "_contexRestoreIsr_Done\n\t" \
#_isrName "_contexRestoreIsr_Nested:\n\t" \
        "mov.b   #0x80,  &0x0a20\n\t" \
        "popm 12,r15\n\t" /* poping R4 -R15 */ \
#_isrName "_contexRestoreIsr_Done:\n\t" \
        "bis %[iebits], @r1 \n\t" /* enable interrupts in SR that will be poped */ \
        "reti\n\t" \
            ::  [ctx] "m" (task_current), \
                [isr_nesting] "m" (isr_nesting), \
                [iebits] "i" (GIE), \
                [powerbits] "i" (SCG1+SCG0+OSCOFF+CPUOFF))

#endif /* __OS_PORT_ */

