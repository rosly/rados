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
 * architecure. At least we can read 8bit values without masking interrupts
 * But from other hand for 8bit increment we need do disable interupts,
 * to make this operation atomic (load-increment-store) */
typedef uint8_t arch_atomic_t;
#define ARCH_ATOMIC_MAX UINT8_MAX

/** exactly 16 bits, minimal reasonable type for ticks, requires special
 * handling code */
typedef uint16_t arch_ticks_t;
#define ARCH_TICKS_MAX ((arch_ticks_t)UINT16_MAX)

/** we use 8bit to allow fast HW multiply on AVR CPU */
typedef uint8_t arch_tickshz_t;

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
#define OS_RESTRICT __restrict__
#define OS_PROGMEM __flash
#define OS_TASKSTACK uint8_t

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
  }while(0)

#if 0
/* \TODO following two instructions can be exchanged, since on AVR alvays one
 * more instruction is executed after enabling interrupts
 * "out __SREG__, %0\n\t"
 * "st %[atomic], __tmp_reg__\n\t" */
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
  }while(0)

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

/* do not mark memory as clobered, since this will destroy all compiler
 * optimizations for memory access and not add any beneficial value to generated
 * code */
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


#ifdef __AVR_HAVE_RAMPZ__
# define arch_push_rampz \
        "in r16, __RAMPZ__"      "\n\t" \
        "push r16"               "\n\t"
#else
# define arch_push_rampz
#endif

#ifndef __AVR_3_BYTE_PC__
# define arch_contextstore_i(_isrName) \
    __asm__ __volatile__ ( \
        /* store r16 and use it as temporary register */ \
        "push    r16"           "\n\t" \
        /* on AVR interupts will be masked when entering ISR \
           but since we entered ISR it means that they where enabled. \
           Therefore we need to save a content of SREG as if global interupt \
           flag was set. using sbr r16, 0x80 for that */ \
        "in      r16, __SREG__" "\n\t" \
        "sbr     r16, 0x80"     "\n\t" \
        "push    r16"           "\n\t" \
        /* push RAMPZ if pressent */   \
        arch_push_rampz                \
        /* store remain registers */   \
        /* gcc uses Y as frame register, it will be easier if we store it \
         * first */ \
        "push    r28"           "\n\t" \
        "push    r29"           "\n\t" \
        /* gcc threats r2-r17 as call-saved registes, if we store them first * \
         * then arch_context_swich can be opimized */ \
        "push    r0"            "\n\t" \
        "push    r1"            "\n\t" \
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
        /* skip  r16 - already saved */ \
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
        "lds     r16, isr_nesting" "\n\t" \
        "inc     r16"              "\n\t" \
        "sts     isr_nesting, r16" "\n\t" \
        /* prepare frame pointer for future C code (usual ISR prolog skiped \
         * since OS_ISR was used */ \
        "in      r28, __SP_L__" "\n\t" /* load SPL into r28 (means Ya) */ \
        "in      r29, __SP_H__" "\n\t" /* load SPH into r29 (means Yb) */ \
        "eor     r1, r1"        "\n\t" /* clear r1 */ \
        /* skip SP update if isr_nesting != 1 */ \
        "cpi     r16, 1"         "\n\t" \
        "brne    isr_contextstore_nested_%=\n\t" \
        /* store SP into task_current->ctx */ \
        "lds     r30, task_current"  "\n\t"  /* load Z with curent_task pointer */ \
        "lds     r31, task_current+1" "\n\t"  /* load Z with curent_task pointer */ \
        "st      Z,   r28"      "\n\t"   /* store SPL into *(task_current) */ \
        "std     Z+1, r29"      "\n\t"   /* store SPH into *(task_current) */ \
     "isr_contextstore_nested_%=:\n\t" \
        :: )
#else
#define arch_contextstore_i(_isrName) \
#error CPU with extended memory registers are not supported yet
/*      "in r0,_SFR_IO_ADDR(RAMPZ)\n\t"
        "push r0"               "\n\t"
        "in r0,_SFR_IO_ADDR(EIND)\n\t"
        "push r0"               "\n\t" */
#endif

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

#ifdef __AVR_HAVE_RAMPZ__
# define arch_pop_rampz \
        "pop r16"               "\n\t" \
        "out __RAMPZ__, r16"    "\n\t"
#else
# define arch_pop_rampz
#endif

#ifndef __AVR_3_BYTE_PC__
#define arch_contextrestore_i(_isrName) \
    __asm__ __volatile__ ( \
        /* disable interrupts in case we add nesting interrupt support */ \
        "cli"                         "\n\t" \
        /* decrement isr_nesting */ \
        "lds     r16, isr_nesting"    "\n\t" \
        "dec     r16"                 "\n\t" \
        "sts     isr_nesting, r16"    "\n\t" \
        /* check isr_nesting and skip restoring iof SP if isr_nesting != 0 */ \
        "brne    isr_contextrestore_nested_%=\n\t" \
        /* restore SP from task_current->ctx */ \
        "lds     r30, task_current"   "\n\t" /* load Z with curent_task pointer */ \
        "lds     r31, task_current+1" "\n\t" /* load Z with curent_task pointer */ \
        "ld      r16, Z"              "\n\t" /* load SPL from *(task_current) */ \
        "ldd     r17, Z+1"            "\n\t" /* load SPH from *(task_current) */ \
        "out     __SP_L__, r16"       "\n\t" /* save SPL */ \
        "out     __SP_H__, r17"       "\n\t" /* save SPH */ \
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
        /* skip r16 - will pop later */ \
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
        "pop    r1"                  "\n\t" \
        "pop    r0"                  "\n\t" \
        "pop    r29"                 "\n\t" \
        "pop    r28"                 "\n\t" \
        /* pop RAMPZ if pressent */         \
        arch_pop_rampz                      \
        /* in poped SEG, I bit may be either set or cleared depending if popped \
         * task had interupts disabled (was switched out by internal OS call) \
         * or enabled (swithed out by os_tick() from interrupt */ \
        "pop    r16"                 "\n\t" \
        /* check if interupts should be enabled after return, if not then we \
         * must use ret instead of reti, cause reti always enables interrupts \
         * interrupts must stay disabled if picked task to which we are switching \
         * now was pushed by arch_context_switch from inside of critical section \
         * of OS */ \
        "sbrc   r16, 7"              "\n\t" \
        "rjmp   isr_contextrestore_enableint_%=\n\t" \
        "out    __SREG__, r16"       "\n\t" \
        "pop    r16"                 "\n\t" \
        /* we will not get interrupt here even if we modify SREG and 2 \
         * instruction passed, since we know that I bit in SREG is disabled */ \
        "ret"                        "\n\t" \
     "isr_contextrestore_enableint_%=:\n\t" \
        /* here we know that I bit in SREG is enabled, we must enable interupts * \
         * after return, but since betwen updating SREG and return we will have * \
         * more that 2 instructions we need to temporarly disable the I bit and * \
         * enable interrupts by reti */ \
        "cbr r16, 0x80"              "\n\t" \
        "out    __SREG__, r16"       "\n\t" \
        "pop    r16"                 "\n\t" \
        /* since we return by reti, always one more instruction is executed \
         * after reti and we can use ISR's to implement OS single stepping \
         * debugger */ \
        "reti"                       "\n\t" \
        :: )
#else
#define arch_contextrestore_i(_isrName) \
#error CPU with extended memory registers are not supported yet
/*      "pop r0"                     "\n\t" \
        "in r0,_SFR_IO_ADDR(EIND)"   "\n\t" \
        "pop r0"                     "\n\t" \
        "in r0,_SFR_IO_ADDR(RAMPZ)"  "\n\t" */
#endif

#endif /* __OS_PORT_ */

