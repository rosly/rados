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

#ifndef __OS_PORT_
#define __OS_PORT_

/* before we include legacymsp30.h we need defines like __CC430F6137__. Those
 * should be automatically added by compiler, because makefiles supply the
 * -mcu=xxx option. */
#include <legacymsp430.h>
#include <stdint.h>  /* for platform specific types definitions like uint16_t */
#include <limits.h>  /* for UINT_MAX etc */
#include <stddef.h>
#include <string.h>  /* for memcpy */

/* missing stdbool.h for this platform, defining bool by hand */
typedef uint8_t bool;
#define false ((uint8_t)0)
/* I also considered also !false but finally decided to be more strict */
#define true  ((uint8_t)1)

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

/* exactly 16 bits. sig_atomic_t should be available from signal.h but on my
 * enviroment signal.h was empty */
typedef uint16_t arch_atomic_t;
#define ARCH_ATOMIC_MAX UINT16_MAX

typedef uint16_t arch_ticks_t;         /* exactly 16 bits */
#define ARCH_TICKS_MAX ((arch_ticks_t)UINT16_MAX)
typedef uint16_t arch_criticalstate_t; /* size of CPU status register */

/* msp430 does not support cpu op for ffs */
typedef uint8_t arch_bitmask_t;
#define ARCH_BITFIELD_MAX ((size_t)((sizeof(arch_bitmask_t) * 8)))

/* 16bit is the native register size of msp430. Additionally it offers direct
 * operations on memory. If we use 16bit for ring indexes we would not have to
 * disable the interrupts (look at run time code optimizations generated in
 * os_atomic_*() */
typedef uint16_t arch_ridx_t;
#define ARCH_RIDX_MAX UINT16_MAX

#define OS_ISR __attribute__((naked)) ISR
#define OS_NAKED __attribute__((naked))
#define OS_NORETURN __attribute__ ((noreturn))
#define OS_PURE __attribute__ ((pure))
#define OS_HOT __attribute__ ((hot))
#define OS_COLD __attribute__ ((cold))
#define OS_NOINLINE __attribute__ ((noinline))
#define OS_WARN_UNUSEDRET __attribute__ ((warn_unused_result))
#define OS_LIKELY(_cond) __builtin_expect(!!(_cond), 1)
#define OS_UNLIKELY(_cond) __builtin_expect(!!(_cond), 0)
#define OS_UNUSED(_x) unused_ ## _x __attribute__((unused))
#define OS_RESTRICT __restrict__
#define OS_PROGMEM
#define OS_PROGMEM_STR(_s) (_s)
#define OS_TASKSTACK uint8_t __attribute__ ((aligned(2)))

#define OS_STACK_DESCENDING
#define OS_STACK_MINSIZE ((size_t)28 * 4) /* four times of context dump size */

/* typical atomic buildins are not available for msp430-gcc, we have to
 * implement them by macros
 * Following macro might seem to be complicated, AND IN FACT IT IS BY REASON:
 * - to prevent from macro parameters side effects, and allow to accept any type
 *   of argument (8/16/32bit), we could use following construction typeof(_ptr)
 *   __ptr = (_ptr).
 * - unfortunately typeof() reuse top level modifiers such as const and volatile
 * - we need to use some local variables which will be removed/optimized by
 *   compiler. But since those variables has to have the same type as _ptr, in
 *   case _ptr is volatile this creates very inefficient code, since compiler
 *   will use stack for temporary variables of volatile type. In other words
 *   this would be opposite to what we want to achieve.
 * - if we want to use temporary variables without additional penalty, we need
 *   to strip of volatile modifier from pointer type.
 * - as a alternative, we could use in-line assembly but then, compiler is
 *   constrained with optimizations. Assembler macros will force compiler to use
 *   concrete register pairs (eg Y,Z) and addressing modes and/or prevent jump
 *   optimizations etc).
 * - OK, we know that we have to stick to C
 * - and we need strict control over the temporary variable types
 * - the only way to do this is to check the size of input variable and recreate
 *   the desired pointer type by copy of pointer address. This way we also
 *   prevent from macro parameters side effects
 * - then right before the access we need to add the volatile modifier to
 *   prevent constant propagation optimizations
 * - for type detection we use __builtin_choose_expr() which checks for constant
 *   condition and than emits one of two code branches
 */

/* there is no way to atomicaly increment and fetch the value from memory, so
 * interrupts must be dissabled. We could only atomicaly increment/decrement
 * directly on memory but futher load will not be atomic */
#define os_atomic_inc_load(_ptr) \
   ({ \
      (typeof(*(_ptr)))__builtin_choose_expr(sizeof(typeof(*(_ptr))) == 1, \
         ({ \
            uint8_t *__ptr = (uint8_t*)(_ptr); \
            uint8_t __val; \
            arch_criticalstate_t cristate; \
            arch_critical_enter(cristate); \
            __val = ++(*(volatile uint8_t*)__ptr); \
            arch_critical_exit(cristate); \
            __val; \
         }), \
         ({ \
            (typeof(*(_ptr)))__builtin_choose_expr(sizeof(typeof(*(_ptr))) == 2, \
               ({ \
                  uint16_t *__ptr = (uint16_t*)(_ptr); \
                  uint16_t __val; \
                  arch_criticalstate_t cristate; \
                  arch_critical_enter(cristate); \
                  __val = ++(*(volatile uint16_t*)__ptr); \
                  arch_critical_exit(cristate); \
                  __val; \
               }), \
            ({ \
               arch_halt(); /* not implemented type of atomic access */ \
               -1; /* this branch is evaluated than optimized, return something */ \
	     }) ); \
         }) ); \
    })

#define os_atomic_dec_load(_ptr) \
   ({ \
      (typeof(*(_ptr)))__builtin_choose_expr(sizeof(typeof(*(_ptr))) == 1, \
         ({ \
            uint8_t *__ptr = (uint8_t*)(_ptr); \
            uint8_t __val; \
            arch_criticalstate_t cristate; \
            arch_critical_enter(cristate); \
            __val = --(*(volatile uint8_t*)__ptr); \
            arch_critical_exit(cristate); \
            __val; \
         }), \
         ({ \
            (typeof(*(_ptr)))__builtin_choose_expr(sizeof(typeof(*(_ptr))) == 2, \
               ({ \
                  uint16_t *__ptr = (uint16_t*)(_ptr); \
                  uint16_t __val; \
                  arch_criticalstate_t cristate; \
                  arch_critical_enter(cristate); \
                  __val = --(*(volatile uint16_t*)__ptr); \
                  arch_critical_exit(cristate); \
                  __val; \
               }), \
            ({ \
               arch_halt(); /* not implemented type of atomic access */ \
               -1; /* this branch is evaluated than optimized, return something */ \
	     }) ); \
         }) ); \
    })

/* simple increment can be done atomicaly directly on memory thanks to MSP430
 * ortogonal instruction set */
#define os_atomic_inc(_ptr) \
   do { \
      if (sizeof(typeof(*(_ptr))) == 2) { \
         __asm__ __volatile__ ( \
            "inc %[atomic]\n\t" \
            ::  [atomic] "m" (*(_ptr))); \
      } \
   } while (0)

#define os_atomic_dec(_ptr) \
   do { \
      if (sizeof(typeof(*(_ptr))) == 2) { \
         __asm__ __volatile__ ( \
            "dec %[atomic]\n\t" \
            ::  [atomic] "m" (*(_ptr))); \
      } \
   } while (0)

#define os_atomic_load(_ptr) \
   ({ \
      (typeof(*(_ptr)))__builtin_choose_expr(sizeof(typeof(*(_ptr))) == 2, \
         ({ \
            uint16_t *__ptr = (uint16_t*)(_ptr); \
            /* concurrent handling is not needed for values <= 16bit, but \
             * double check that */ \
            /* OS_STATIC_ASSERT(__atomic_always_lock_free(sizeof(uint16_t), __ptr)); */ \
            *(volatile uint16_t*)__ptr; \
         }), \
         /* will give following error error: control reaches end of non-void function */ \
         ({ }) ); \
    })

#define os_atomic_store(_ptr, _val) \
   do { \
      OS_STATIC_ASSERT(__builtin_types_compatible_p(typeof(*(_ptr)), typeof(_val))); \
      if (sizeof(typeof(*(_ptr))) == 2) { \
         uint16_t *__ptr = (uint16_t*)(_ptr); \
         uint16_t __val = (uint16_t)(_val); \
         /* concurrent handling is not needed for values <= 16bit, but \
          * double check that */ \
         /* OS_STATIC_ASSERT(__atomic_always_lock_free(sizeof(uint16_t), __ptr)); */ \
         *(volatile uint16_t*)__ptr = __val; \
      } \
   } while (0)

#define os_atomic_exch(_ptr, _val) \
   ({ \
      OS_STATIC_ASSERT(__builtin_types_compatible_p(typeof(*(_ptr)), typeof(_val))); \
      (typeof(*(_ptr)))__builtin_choose_expr(sizeof(typeof(*(_ptr))) == 2, \
         ({ \
            uint16_t *__ptr = (uint16_t*)(_ptr); \
            uint16_t __val = (uint16_t)(_val); \
            uint16_t __tmp; \
            arch_criticalstate_t cristate; \
            arch_critical_enter(cristate); \
            __tmp = *(volatile uint16_t*)__ptr; \
            *(volatile uint16_t*)__ptr = __val; \
            arch_critical_exit(cristate); \
            __tmp; /* return value */ \
         }), \
         /* will give following error error: control reaches end of non-void function */ \
         ({ }) ); \
    })

/* \TODO got some errors while trying following
 * OS_STATIC_ASSERT( \
 *    __builtin_types_compatible_p(typeof(_ptr), typeof(_exp_val))); \
 * OS_STATIC_ASSERT( \
 *    __builtin_types_compatible_p(typeof(*(_ptr)), typeof(_val))); \
 */
#define os_atomic_cmp_exch(_ptr, _exp_val, _val) \
   ({ \
      bool fail; \
      if (sizeof(typeof(*(_ptr))) == 2) { \
         uint16_t *__ptr = (uint16_t*)(_ptr); \
         uint16_t *__exp_val = (uint16_t*)(_exp_val); \
         uint16_t __val = (uint16_t)(_val); \
         uint16_t __tmp; \
         arch_criticalstate_t cristate; \
         arch_critical_enter(cristate); \
         __tmp = *(volatile uint16_t*)__ptr; \
         if (__tmp == *__exp_val) { \
            *(volatile uint16_t*)__ptr = __val; \
            fail = false; \
            arch_critical_exit(cristate); \
         } else { \
            *__exp_val = __tmp; \
            fail = true; \
            arch_critical_exit(cristate); \
         } \
      } \
      fail; /* return value */ \
    })

#define arch_bitmask_set(_bitfield, _bit) \
   do { \
      (_bitfield) |= 1 << (_bit); \
   } while (0);

#define arch_bitmask_clear(_bitfield, _bit) \
   do { \
      (_bitfield) &= ~(1 << (_bit)); \
   } while (0);

uint_fast8_t arch_bitmask_fls(arch_bitmask_t bitfield);

#define arch_critical_enter(_critical_state) \
   do { \
      (_critical_state) = __read_status_register(); \
      arch_dint(); \
   } while (0)

#define arch_critical_exit(_critical_state) \
   do { \
      /* overwrite all flags, previously power bits must been enabled so \
       * we will not suspend CPU (no risk) */ \
      /* \TODO instead try ? __bis_status_register(GIE & (_critical_state)); \
       * modify only the IE flag, while remain rest untouched */ \
      __write_status_register(_critical_state); \
   } while (0)

#define arch_dint() dint()
#define arch_eint() eint()
#define arch_is_dint() ({ !(__read_status_register() & GIE); })

/* format of the context pushed on stack for MSP430 port
 * low address
 *   PC - pushed first
 *   R3(SR) - pushed automatically on ISR enter
 *            (manually during schedule form user)
 *   R4 - R15 - pushed last
 *  hi address
 */

/* This function have to:
 *  - if necessary, disable interrupts to block the nesting
 *  - store all registers (power control bits does not have to be necessarily
 * stored)
 *  - increment the isr_nesting
 *  - if isr_nesting is = 1 then
 *   - store the context curr_tcb->ctx (may take benefit from already stored
 *     registers by storing only the stack pointer)
 *  - end
 *
 *  - in some near future down in the ISR enable interrupts to support the
 *    nesting interrupts
 *
 * Because ISR was called it means that interrupts was enabled. On some arch
 * like MPS430 they may be automatically disabled during the enter to ISR
 * On those architectures interrupts may be enabled when ISR will mask pending
 * interrupt.
 * In general disabling interrupt is usually needed because we touch the
 * task_current (usually need 2 asm instructions) and we cannot be preempted by
 * another interrupt.
 * From other hand enabling the interrupts again as soon as possible is needed
 * for real-time constrains.
 * If your code does not need to be real-time constrained, it is not needed to
 * enable the interrupts in ISR, also the nesting interrupt code can be disabled
 *
 * The reason why we skip the stack pointer storage in case of nesting is
 * obvious. In case of nesting we was not in task but in other ISR. So the SP
 * will not be the task SP.
 * But we have to store all registers anyway. This is why we store all
 * registers and then optionally store the SP in context of tcb */
#define arch_contextstore_i(_isrName) \
   __asm__ __volatile__ ( \
      /* on MSP430 interrupts are disabled when entering ISR */ \
      /* increment isr_nesting, here we destroy original SR but it already lay \
       * on stack*/ \
      "inc %[isr_nesting]\n\t" \
      "pushm 12,r15\n\t"            /* pushing R4 -R15 */ \
      "cmp #1, %[isr_nesting]\n\t"  /* check isr_nesting */ \
      "jne " # _isrName "_contextStoreIsr_Nested\n\t" \
                        "mov %[ctx], r15\n\t" \
                        "mov r1, @r15\n\t" \
      # _isrName "_contextStoreIsr_Nested:\n\t" \
      ::  [ctx] "m" (task_current), \
      [isr_nesting] "m" (isr_nesting))

/**
 * This function have to:
 *  - disable IE (in case we architecture allows for nesting)
 *  - decrement the isr_nesting
 *  - if isr_nesting = 0 then
 *    - restore context from curr_tcb->ctx
 *  - restore all registers
 *  - perform actions that will lead to enable IE after reti
 *  - return by reti
 *
 * Please first read the note for arch_context_StoreI. The important point here
 * is why we need to enable the interrupt after reti in both cases (in normal
 * and nested).
 * This is because we disabled them for task_current manipulation in first
 * step. But we need to enable them because:
 *  - in case of not nested they was for sure enabled (need to be enabled
 *    because we enter ISR ;) )
 *  - in case of nested the was also for sure enabled (from the same reason, we
 *    enter nested ISR) */
#define arch_contextrestore_i(_isrName) \
   __asm__ __volatile__ ( \
      /* disable interrupts in case some ISR will implement nesting interrupt \
       * handling */ \
      "dint\n\t" \
      "dec %[isr_nesting]\n\t" \
      "jnz " # _isrName "_contexRestoreIsr_Nested\n\t" \
                        "mov %[ctx], r1\n\t" \
                        "mov @r1, r1\n\t" \
                        "popm 12,r15\n\t"             /* popping R4 -R15 */ \
      /* enable full power mode in SR that will be popped */ \
                        "bic %[powerbits], @r1 \n\t" \
                        "jmp " # _isrName "_contexRestoreIsr_Done\n\t" \
      # _isrName "_contexRestoreIsr_Nested:\n\t" \
                 "mov.b   #0x80,  &0x0a20\n\t" \
                 "popm 12,r15\n\t"           /* popping R4 -R15 */ \
      # _isrName "_contexRestoreIsr_Done:\n\t" \
      /* enable interrupts in SR that will be popped */ \
                 "bis %[iebits], @r1 \n\t" \
                 "reti\n\t" \
      ::  [ctx] "m" (task_current), \
      [isr_nesting] "m" (isr_nesting), \
      [iebits] "i" (GIE), \
      [powerbits] "i" (SCG1 + SCG0 + OSCOFF + CPUOFF))

#endif /* __OS_PORT_ */

