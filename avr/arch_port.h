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

#ifndef __OS_PORT_
#define __OS_PORT_

#include <stdint.h> /* for platform specific types definitions like uint16_t */
#include <limits.h> /* for UINT_MAX etc */
#include <stddef.h>
#include <string.h> /* for memcpy */
#include <stdbool.h>

#include <avr/builtins.h> /* for __builtin_avr_sei */
#include <avr/io.h> /* for SREG */

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

/* AVR does not support direct memory manipulation since AVR is LOAD-STORE
 * architecure. Therefore even for 8bit increment we need do disable interupts,
 * to make this operation atomic */
typedef uint8_t arch_atomic_t;
/** exactly 16 bits, minimal reasonable type for ticks, requires special
 * handling code */
typedef uint16_t arch_ticks_t;
#define ARCH_TICKS_MAX ((arch_ticks_t)UINT16_MAX)
typedef uint8_t arch_criticalstate_t; /* size of AVR status register */

/* for ISR we use:
 * -naked - since we provide own register save-restore macros
 * -signal - seems to be proper attr for ISR, there is also interrupt but from
 *  what I see it is used if we whant ISR to handle nesting fast as possible
 * -used
 * -externally_visible
 */
#define OS_ISR __attribute__((naked, signal, used, externally_visible))
//#define OS_ISR __attribute__((naked, used, externally_visible))
#define OS_NAKED __attribute__((naked))
#define OS_NORETURN __attribute__ ((noreturn))
#define OS_PURE __attribute__ ((pure))
#define OS_HOT __attribute__ ((hot))
#define OS_COLD __attribute__ ((cold))
#define OS_WARN_UNUSEDRET __attribute__ ((warn_unused_result))
#define OS_LIKELY(_cond) __builtin_expect(!!(_cond), 1)
#define OS_UNLIKELY(_cond) __builtin_expect(!!(_cond), 0)
#define OS_UNUSED(_x) unused_ ## _x __attribute__((unused))

#define OS_STACK_DESCENDING
#define OS_STACK_MINSIZE ((size_t)35 * 4) /* four times of context dump size */

/* AVR does not support direct memory operations (LOAD-STORE architecture)
 * we mmust disable interupts to ensure atomicity */
#define os_atomic_inc(_atomic) \
  do { \
    arch_criticalstate_t cristate; \
    arch_critical_enter(cristate); \
    (_atomic)++; \
    arch_critical_exit(cristate); \
  }while(1)

#if 0
  do { \
    uint8_t _tmp_reg2; \
    __asm__ __volatile__ ( \
         "in %0, __SREG__\n\t" \
         "cli\n\t" \
         "ld __tmp_reg__, %[atomic]\n\t" \
         "inc __tmp_reg__\n\t" \
         "st %[atomic], __tmp_reg__\n\t" \
         "out __SREG__, %0\n\t" \
             : "=&r" (_tmp_reg2) \
             : [atomic] "e" (_atomic) \
             : "memory" ); \
  }while(0)
#endif

/* AVR does not support direct memory operations (LOAD-STORE architecture)
 * we mmust disable interupts to ensure atomicity */
#define os_atomic_dec(_atomic) \
  do { \
    arch_criticalstate_t cristate; \
    arch_critical_enter(cristate); \
    --(_atomic); \
    arch_critical_exit(cristate); \
  }while(1)

#if 0
  do { \
    uint8_t _tmp_reg2; \
    __asm__ __volatile__ ( \
         "in %0, __SREG__\n\t" \
         "cli\n\t" \
         "ld __tmp_reg__, %[atomic]\n\t" \
         "dec __tmp_reg__\n\t" \
         "st %[atomic], __tmp_reg__\n\t" \
         "out __SREG__, %0\n\t" \
             : "=&r" (_tmp_reg2) \
             : [atomic] "e" (_atomic) \
             : "memory" ); \
  }while(0)
#endif

/* on AVR there is no instruction to load whole X, Y, or whole Z at once, we
 * need to disable interrupts if we whant to do it atomicaly */
#define os_atomicptr_read(_ptr) \
  ({ \
    typeof(_ptr) _tmp_widereg; \
    arch_criticalstate_t cristate; \
    arch_critical_enter(cristate); \
    _tmp_widereg = (_ptr); \
    arch_critical_exit(cristate); \
    _tmp_widereg; /* return loaded value */ \
  })

#if 0
  {( \
    uint16_t _tmp_widereg; \
    __asm__ __volatile__ ( \
         "in __tmp_reg__, __SREG__\n\t" \
         "cli\n\t" \
         "ld %a0, %[ptr]+\n\t" \
         "ld %b0, %[ptr]\n\t" \
         "out __SREG__, __tmp_reg__\n\t" \
             : "=&r" (_tmp_widereg) \
             : "[ptr] e" (_ptr) \
             : ); \
    _tmp_widereg; /* return loaded value */ \
  )}
#endif

/* on AVR there is no instruction to store whole X, Y, or whole Z at once, we
 * need to disable interrupts if we whant to do it atomicaly */
#define os_atomicptr_write(_ptr, _val) \
  do { \
    arch_criticalstate_t cristate; \
    arch_critical_enter(cristate); \
    (_ptr) = (_val); \
    arch_critical_exit(cristate); \
 }while(0)

#if 0
  do { \
    __asm__ __volatile__ ( \
         "in __tmp_reg__, __SREG__\n\t" \
         "cli\n\t" \
         "st %[ptr]+,%a1n\t" \
         "ld %[ptr],%b1\n\t" \
         "out __SREG__, __tmp_reg__\n\t" \
             : : "[ptr] e" (_ptr), [_val] "e" (_val) \
             : "0" (_ptr) ); /* clobering ptr */ \
  }while(0)
#endif

#define os_atomicptr_xchnge(_ptr, _val) (OS_ASSERT(!"not implemented"))
#define arch_ticks_atomiccpy(_dst, _src) \
  do { \
    arch_criticalstate_t cristate; \
    arch_critical_enter(cristate); \
    *(_dst) = *(_src); \
    arch_critical_exit(cristate); \
 }while(0)

#if 0
  do { \
    uint8_t _tmp_reg2; \
    __asm__ __volatile__ ( \
         "in __tmp_reg__, __SREG__\n\t" \
         "cli\n\t" \
         "ld %[_tmp_reg2], %[_src]+\n\t" \
         "st %[_dst]+, %[_tmp_reg2]\n\t" \
         "ld %[_tmp_reg2], %[_src]+\n\t" \
         "st %[_dst]+, %[_tmp_reg2]\n\t" \
         "out __SREG__, __tmp_reg__\n\t" \
             : [_tmp_reg2] "+r" (_tmp_reg2) \
             : [_src] "e" (_src), [_dst] "e" (_dst) \
             : "0" (_src), "1" (_dst), "memory" ); \
  }while(0)
#endif

#define arch_critical_enter(_critical_state) \
   do { \
      (_critical_state) = SREG; \
      arch_dint(); \
   }while(0)

#define arch_critical_exit(_critical_state) \
   do { \
      SREG = (_critical_state); \
   }while(0)

#define arch_dint() __asm__ __volatile__ ( "cli\n\t" :: )
#define arch_eint() __asm__ __volatile__ ( "sei\n\t" :: )

/* format of the context pushed on stack for AVR port
hi adress
    PC - pushed frst
    R0 - stored to gain one free register
    SREG
    R1 - R31 - pusched last
low address
*/

/* This function have to:
 - if neecessary, disable interrupts to block the neesting
 - store all registers (power control bits does not have to be necessarly stored)
 - increment the isr_nesting
 - if isr_nesting is = 1 then
    - store the context curr_tcb->ctx (may take benfit from allready stored registers by storing only the stack pointer)
 - end

 - in some near future down in the ISR enable interrupts to support the nesting interrupts

 because ISR was called it means that interrupts was enabled. On some arch like MPS430 they may be automaticly disabled durring the enter to ISR
 On those architectures interrupts may be enabled when ISR will mask pending interrupt.
 In general disabling interrupt is usualy needed because we touch the task_current (usualy need 2 asm instructions) and we cannot be preempted by another interrupt.
 From other hand enabling the interrupts again as soon as possible is needed for realtime constrains.
 If your code does not need to be realtime constrained, it is not needed to enable the interupts in ISR, also the nesting interrupt code can be disabled

 The reason why we skip the stack pointer storage in case of nesing is obvous. In case of nesting we was not in task but in other ISR. So the SP will not be the task SP.
 But we have to store all registers anyway. This is why we store all registers and then optionaly store the SP in context of tcb */
#define arch_contextstore_i(_isrName) \
    __asm__ __volatile__ ( \
        /* on AVR interupts are disabled when entering ISR */ \
        /* store r0 for temporary operations */ \
        "push    r0"            "\n\t" \
        /* store SREG and r1 */        \
        "in      r0, __SREG__"  "\n\t" \
        "push    r0"            "\n\t" \
        "push    r1"            "\n\t" \
        /* store remain registers */    \
        /* gcc uses Y as frame register, it will be easier if we store it \
         * first */ \
        "push    r28"           "\n\t" \
        "push    r29"           "\n\t" \
        /* gcc threats r2-r17 as call-saved registes, if we store them first * \
         * then arch_context_swich can be opimized */ \
        "push    r2"            "\n\t" \
        "push    r3"            "\n\t" \
        "push    r4"            "\n\t" \
        "push    r5"            "\n\t" \
        "push    r6"            "\n\t" \
        "push    r7"            "\n\t" \
        "push    r8"            "\n\t" \
        "push    r9"            "\n\t" \
        "push    r10"           "\n\t" \
        "push    r11"           "\n\t" \
        "push    r12"           "\n\t" \
        "push    r13"           "\n\t" \
        "push    r14"           "\n\t" \
        "push    r15"           "\n\t" \
        "push    r16"           "\n\t" \
        "push    r17"           "\n\t" \
        "push    r18"           "\n\t" \
        "push    r19"           "\n\t" \
        "push    r20"           "\n\t" \
        "push    r21"           "\n\t" \
        "push    r22"           "\n\t" \
        "push    r23"           "\n\t" \
        "push    r24"           "\n\t" \
        "push    r25"           "\n\t" \
        "push    r26"           "\n\t" \
        "push    r27"           "\n\t" \
        "push    r30"           "\n\t" \
        "push    r31"           "\n\t" \
        /* incement isr_nesting */ \
        "lds     r16, %[isr_nesting]" "\n\t" \
        "inc     r16"            "\n\t" \
        "sts     %[isr_nesting], r16" "\n\t" \
        /* prepare frame pointer for future C code (usual ISR prolog skiped \
         * since OS_ISR was used */ \
        "in      r28, __SP_L__" "\n\t" /* load SPL into r28 (means Ya) */ \
        "in      r29, __SP_H__" "\n\t" /* load SPH into r29 (means Yb) */ \
        "eor     r1, r1"        "\n\t" /* clear r1 */ \
        /* check isr_nesting and skip SP storing if != 1 */ \
        "cpi     r16, 1"         "\n\t" \
        "brne    isr_contextstore_nested_%=\n\t" \
        /* store SP into task_current->ctx */ \
        "lds     r30, %[ctx]"   "\n\t"  /* load Z by addr of curent_task */ \
        "lds     r31, %[ctx]+1" "\n\t"  /* load Z by addr of curent_task */ \
        "st      Z,   r28"      "\n\t"   /* store SPL into task_curent->ctx */ \
        "std     Z+1, r29"      "\n\t"   /* store SPH into task_curent->ctx */ \
     "isr_contextstore_nested_%=:\n\t" \
            ::  [ctx] "p" (&(task_current)), \
                [isr_nesting] "p" (&(isr_nesting)))

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
        /* disable interrupts in case some ISR will implement nesting interrupt * \
         * handling */ \
        "cli"                         "\n\t" \
        /* decrement isr_nesting */ \
        "lds     r16, %[isr_nesting]" "\n\t" \
        "dec     r16"                 "\n\t" \
        "sts     %[isr_nesting], r16" "\n\t" \
        /* check isr_nesting and skip restoring iof SP if isr_nesting != 0 */ \
        "brne    isr_contextrestore_nested_%=\n\t" \
        /* restore SP from task_current->ctx */ \
        "lds     r30, %[ctx]"         "\n\t" /* load Z by addr of curent_task */ \
        "lds     r31, %[ctx]+1"       "\n\t" /* load Z by addr of curent_task */ \
        "ld      r16, Z"              "\n\t" /* load SPL into task_curent->ctx */ \
        "ldd     r17, Z+1"            "\n\t" /* load SPH into task_curent->ctx */ \
        "out     __SP_L__, r16"       "\n\t" /* load SPL from r16 */ \
        "out     __SP_H__, r17"       "\n\t" /* load SPH from r17 */ \
     "isr_contextrestore_nested_%=:\n\t" \
        /* restore all register */ \
        "pop    r31"                 "\n\t" \
        "pop    r30"                 "\n\t" \
        "pop    r27"                 "\n\t" \
        "pop    r26"                 "\n\t" \
        "pop    r25"                 "\n\t" \
        "pop    r24"                 "\n\t" \
        "pop    r23"                 "\n\t" \
        "pop    r22"                 "\n\t" \
        "pop    r21"                 "\n\t" \
        "pop    r20"                 "\n\t" \
        "pop    r19"                 "\n\t" \
        "pop    r18"                 "\n\t" \
        "pop    r17"                 "\n\t" \
        "pop    r16"                 "\n\t" \
        "pop    r15"                 "\n\t" \
        "pop    r14"                 "\n\t" \
        "pop    r13"                 "\n\t" \
        "pop    r12"                 "\n\t" \
        "pop    r11"                 "\n\t" \
        "pop    r10"                 "\n\t" \
        "pop    r9"                  "\n\t" \
        "pop    r8"                  "\n\t" \
        "pop    r7"                  "\n\t" \
        "pop    r6"                  "\n\t" \
        "pop    r5"                  "\n\t" \
        "pop    r4"                  "\n\t" \
        "pop    r3"                  "\n\t" \
        "pop    r2"                  "\n\t" \
        "pop    r29"                 "\n\t" \
        "pop    r28"                 "\n\t" \
        "pop    r1"                  "\n\t" \
        /* restore SREG, I bit in popped SEG will not be set since we pushed it * \
         * at the begining of ISR where interrupst where disabled, so restoring * \
         * SREG will not enable interrupts until we execute reti */ \
        "pop    r0"                  "\n\t" \
        "out    __SREG__, r0"        "\n\t" \
        /* restore r0 and reti */           \
        "pop    r0"                  "\n\t" \
        "reti"                       "\n\t" \
            ::  [ctx] "p" (task_current),   \
                [isr_nesting] "p" (isr_nesting))

#endif /* __OS_PORT_ */

