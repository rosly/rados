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

#include <stdint.h>  /* for platform specific types definitions like uint16_t */
#include <limits.h>  /* for UINT_MAX etc */
#include <stddef.h>
#include <string.h>  /* for memcpy */
#include <stdbool.h>

#include <avr/builtins.h>  /* for __builtin_avr_sei */
#include <avr/io.h>        /* for SREG */
#include <avr/pgmspace.h>  /* for PSTR */

/* Following structure held CPU registers which need to be preserved between
 * context switches. In general (at any ARCH), there are two possible
 * approaches:
 * - store all CPU context sensitive registers in arch_context_t structure
 *   (which is placed at the beginning of os_task_t)
 * - store only stack pointer in arch_context_t structure, while storing CPU
 *   registers on the task stack
 * It is up to programmer (who prepare specific port) to chose the appropriate
 * approach (storing registers in arch_context_t uses more static task memory
 * while storing them on stack requires more stack memory).  Most of others RTOS
 * use the second method. This is mainly because storing registers stack is the
 * fastest way (requires less CPU cycles).
 */
typedef struct {
   uint16_t sp;
} arch_context_t;

/* AVR does not support direct memory manipulation, since AVR is LOAD-STORE
 * architecture. At least we can read 8bit values without masking interrupts
 * But for 8bit increment we need to disable interrupts, to make this operation
 * atomic (load-increment-store)
 */
typedef uint8_t arch_atomic_t;
#define ARCH_ATOMIC_MAX UINT8_MAX

/** exactly 16 bits, minimal reasonable type for ticks, requires special
 * handling code */
typedef uint16_t arch_ticks_t;
#define ARCH_TICKS_MAX ((arch_ticks_t)UINT16_MAX)

typedef uint8_t arch_criticalstate_t; /* size of AVR status register */

/* the largest sane type for bit field operations on 8bit CPU. We could try
 * extend that */
typedef uint8_t arch_bitmask_t;
#define ARCH_BITFIELD_MAX ((size_t)((sizeof(arch_bitmask_t) * 8)))

/* since even for 8 bit we need to disable the interrupt for atomic load/store,
 * there is no reason to limit this type to 8 bit (256 mqueue depth) */
typedef uint16_t arch_ridx_t;
#define ARCH_RIDX_MAX UINT16_MAX

/* for ISR we use following attributes:
 * - naked - since we provide own register save-restore macros
 * - signal - seems to be proper attr for ISR, there is also interrupt but from
 *            what I see it is used if we want ISR to handle nesting as fast as
 *            possible
 * - used - to not remove the ISR as non referenced code
 * - externally_visible
 */
#define OS_ISR __attribute__((naked, signal, used, externally_visible))
//#define OS_ISR __attribute__((naked, used, externally_visible))
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
#define OS_PROGMEM __flash
//#define OS_PROGMEM PROGMEM
#define OS_PROGMEM_STR(_s) PSTR(_s)
#define OS_TASKSTACK uint8_t

#define OS_STACK_DESCENDING
#define OS_STACK_MINSIZE ((size_t)(35 * 4)) /* four times of context size */

/** Typical atomic buildins are not available for avr-gcc, we have to implement
 * them by hand written macros.
 * On AVR there is no instruction to load whole X, Y, or whole Z at once, we
 * need to disable interrupts if we want to do it atomically
 *
 * Following macros might seem to be complicated, AND IN FACT IT IS BY REASON:
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

/* enforce calculation of _expr before entering critical section by influencing
 * on register manager. This will prevent from relocation of _val calculation
 * inside critical section which would uneccessarly extend time while IRQ are
 * dissabled */
#define assign_register(_expr) \
   ({ if (!__builtin_constant_p(_expr)) { \
         __asm__ __volatile__ ( "" :: "r" (_expr)); \
      } \
   })

/* AVR does not support direct memory operations (LOAD-STORE architecture)
 * we must disable interrupts to achieve atomicity */

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

#define os_atomic_inc(_ptr) \
   do { \
      /* we cannot do anything smarter so just ignore the return value from \
       * previous macro */ \
      os_atomic_inc_load(_ptr); \
   } while (0)

#define os_atomic_dec(_ptr) \
   do { \
      /* we cannot do anything smarter so just ignore the return value from \
       * previous macro */ \
      os_atomic_dec_load(_ptr); \
   } while (0)

#define os_atomic_load(_ptr) \
   ({ \
      (typeof(*(_ptr)))__builtin_choose_expr(sizeof(typeof(*(_ptr))) == 2, \
         ({ \
            uint16_t *__ptr = (uint16_t*)(_ptr); \
            uint16_t __val; \
            arch_criticalstate_t cristate; \
            arch_critical_enter(cristate); \
            __val = *(volatile uint16_t*)__ptr; \
            arch_critical_exit(cristate); \
            __val; \
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
         arch_criticalstate_t cristate; \
         \
         assign_register(__val); \
         arch_critical_enter(cristate); \
         *(volatile uint16_t*)__ptr = __val; \
         arch_critical_exit(cristate); \
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
            \
            assign_register(__val); \
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
#define os_atomic_cmp_exch(_ptr, _ptr_exp_val, _val) \
   ({ \
      bool fail; \
      if (sizeof(typeof(*(_ptr))) == 2) { \
         uint16_t *__ptr = (uint16_t*)(_ptr); \
         uint16_t *__ptr_exp_val = (uint16_t*)(_ptr_exp_val); \
         uint16_t __exp_val = *__ptr_exp_val; \
         uint16_t __val = (uint16_t)(_val); \
         uint16_t __tmp; \
         arch_criticalstate_t cristate; \
         \
         assign_register(__val); \
         arch_critical_enter(cristate); \
         __tmp = *(volatile uint16_t*)__ptr; \
         if (__tmp == __exp_val) { \
            *(volatile uint16_t*)__ptr = __val; \
            arch_critical_exit(cristate); \
            fail = false; \
         } else { \
            arch_critical_exit(cristate); \
            *__ptr_exp_val = __tmp; \
            fail = true; \
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
      (_critical_state) = SREG; \
      arch_dint(); \
   } while (0)

#define arch_critical_exit(_critical_state) \
   do { \
      SREG = (_critical_state); \
   } while (0)

/* We are not using "memory" as clobbered list in following macro
 * Using "memory" for clobber list is not usefull at all. Any shared value need
 * to be volatile of exchanged by atomic macros. Adding "memory" for clobber
 * list will not help stupid code while it removes all other (intentional)
 * compiler optimizations for memory access.
 * http://www.atmel.com/webdoc/AVRLibcReferenceManual/optimization_1optim_code_reorder.html
 * */
#define arch_dint() __asm__ __volatile__ ( "cli\n\t" :: )
#define arch_eint() __asm__ __volatile__ ( "sei\n\t" :: )
#define arch_is_dint() ({ !(SREG & (1 << SREG_I)); })

/* format of the context pushed on stack for AVR port
 *  hi adress
 *   PC - pushed first
 *   R0 - stored to gain one free register
 *   SREG
 *   R1 - R31 - pushed last
 *  low address
 */

#ifdef __AVR_HAVE_RAMPZ__
# define arch_push_rampz \
   "in r16, __RAMPZ__        \n\t" \
   "push r16                 \n\t"
#else
# define arch_push_rampz
#endif

/** Interrupt entrance code. This function has to:
 * - if necessary, disable interrupts to block the nesting
 * - store all registers (power control bits do not have to be necessarily
 *   stored)
 * - increment the isr_nesting
 * - if isr_nesting is = 1 then
 *   - store the context curr_tcb->ctx (may take benefit from already stored
 *     registers by storing only the stack pointer)
 * - end
 *
 * - in some near future down in the ISR enable interrupts to support the
 *   nesting interrupts
 *
 * For support of nested interrupts, we should enable IE bit as soon as we will
 * mask the pending interrupt.
 * For sake of this function, global IE disable is needed because we touch the
 * task_current (usually need 2 asm instructions) and we cannot be preempted by
 * another interrupt. Nested interrupts are needed for real-time constraints. If
 * your code does not need to be real-time constrained, it is not needed to
 * enable the interrupts in ISR (no nesting interrupts means some of code in
 * this function can be disabled)
 */
#ifndef __AVR_3_BYTE_PC__
# define arch_contextstore_i(_isrName) \
   __asm__ __volatile__ ( \
      /* store r16 and use it as temporary register */ \
      "push    r16             \n\t" \
      /* on AVR interrupts will be masked when entering ISR \
       * but since we entered ISR it means that they where enabled. \
       * Therefore we need to save the content of SREG as if global interrupt \
       * flag was set. using sbr r16, 0x80 for that */ \
      "in      r16, __SREG__   \n\t" \
      "sbr     r16, 0x80       \n\t" \
      "push    r16             \n\t" \
      /* push RAMPZ if pressent */   \
      arch_push_rampz                \
      /* store remain registers */   \
      /* gcc uses Y as frame register, it will be easier if we store it \
       * first */ \
      "push    r28             \n\t" \
      "push    r29             \n\t" \
      /* gcc treats r2-r17 as call-saved registers, if we store them first * \
       * then arch_context_switch can be optimized */ \
      "push    r0              \n\t" \
      "push    r1              \n\t" \
      "push    r2              \n\t" \
      "push    r3              \n\t" \
      "push    r4              \n\t" \
      "push    r5              \n\t" \
      "push    r6              \n\t" \
      "push    r7              \n\t" \
      "push    r8              \n\t" \
      "push    r9              \n\t" \
      "push    r10             \n\t" \
      "push    r11             \n\t" \
      "push    r12             \n\t" \
      "push    r13             \n\t" \
      "push    r14             \n\t" \
      "push    r15             \n\t" \
      /* skip  r16 - already saved */ \
      "push    r17             \n\t" \
      "push    r18             \n\t" \
      "push    r19             \n\t" \
      "push    r20             \n\t" \
      "push    r21             \n\t" \
      "push    r22             \n\t" \
      "push    r23             \n\t" \
      "push    r24             \n\t" \
      "push    r25             \n\t" \
      "push    r26             \n\t" \
      "push    r27             \n\t" \
      "push    r30             \n\t" \
      "push    r31             \n\t" \
      /* increment isr_nesting */ \
      "lds     r16, isr_nesting   \n\t" \
      "inc     r16                \n\t" \
      "sts     isr_nesting, r16   \n\t" \
      /* prepare frame pointer for future C code (usual ISR prolog skipped \
       * since OS_ISR was used */ \
      "in      r28, __SP_L__   \n\t"   /* load SPL into r28 (means Ya) */ \
      "in      r29, __SP_H__   \n\t"   /* load SPH into r29 (means Yb) */ \
      "eor     r1, r1          \n\t"   /* clear r1 */ \
      /* skip SP update if isr_nesting != 1 */ \
      "cpi     r16, 1           \n\t" \
      "brne    isr_contextstore_nested_%=\n\t" \
      /* store SP into task_current->ctx */ \
      "lds     r30, task_current  \n\t"   /* load Z with current_task pointer */ \
      "lds     r31, task_current+1\n\t"   /* load Z with current_task pointer */ \
      "st      Z,   r28           \n\t"   /* store SPL into *(task_current)   */ \
      "std     Z+1, r29           \n\t"   /* store SPH into *(task_current)   */ \
      "isr_contextstore_nested_%=:\n\t" \
      :: )
#else
# error CPUs with extended memory registers are not supported yet
/*      "in r0,_SFR_IO_ADDR(RAMPZ)\n\t"
 *       "push r0                 \n\t"
 *       "in r0,_SFR_IO_ADDR(EIND)\n\t"
 *       "push r0                 \n\t" */
#endif

#ifdef __AVR_HAVE_RAMPZ__
# define arch_pop_rampz \
   "pop r16                 \n\t" \
   "out __RAMPZ__, r16      \n\t"
#else
# define arch_pop_rampz
#endif

/** Interrupt leave code. This function has to:
 * - disable IE (in case the architecture allows for nesting)
 * - decrement the isr_nesting
 * - if isr_nesting = 0 then
 *   - restore context from curr_tcb->ctx
 * - restore all registers
 * - perform actions that will lead to enabling IE after reti
 * - return by reti
 */
#ifndef __AVR_3_BYTE_PC__
#define arch_contextrestore_i(_isrName) \
   __asm__ __volatile__ ( \
      /* disable interrupts in case we add nesting interrupt support */ \
      "cli                           \n\t" \
      /* decrement isr_nesting */ \
      "lds     r16, isr_nesting      \n\t" \
      "dec     r16                   \n\t" \
      "sts     isr_nesting, r16      \n\t" \
      /* check isr_nesting and skip restoring of SP if isr_nesting != 0 */ \
      "brne    isr_contextrestore_nested_%=\n\t" \
      /* restore SP from task_current->ctx */ \
      "lds     r30, task_current     \n\t"   /* load Z with curent_task ptr */ \
      "lds     r31, task_current+1   \n\t"   /* load Z with curent_task ptr */ \
      "ld      r16, Z                \n\t"   /* load from *(task_current)   */ \
      "ldd     r17, Z+1              \n\t"   /* load from *(task_current+1) */ \
      "out     __SP_L__, r16         \n\t"   /* save to SPL */ \
      "out     __SP_H__, r17         \n\t"   /* save to SPH */ \
      "isr_contextrestore_nested_%=:\n\t" \
      /* restore all register */ \
      "pop    r31                   \n\t" \
      "pop    r30                   \n\t" \
      "pop    r27                   \n\t" \
      "pop    r26                   \n\t" \
      "pop    r25                   \n\t" \
      "pop    r24                   \n\t" \
      "pop    r23                   \n\t" \
      "pop    r22                   \n\t" \
      "pop    r21                   \n\t" \
      "pop    r20                   \n\t" \
      "pop    r19                   \n\t" \
      "pop    r18                   \n\t" \
      "pop    r17                   \n\t" \
      /* skip r16 - will pop later */ \
      "pop    r15                   \n\t" \
      "pop    r14                   \n\t" \
      "pop    r13                   \n\t" \
      "pop    r12                   \n\t" \
      "pop    r11                   \n\t" \
      "pop    r10                   \n\t" \
      "pop    r9                    \n\t" \
      "pop    r8                    \n\t" \
      "pop    r7                    \n\t" \
      "pop    r6                    \n\t" \
      "pop    r5                    \n\t" \
      "pop    r4                    \n\t" \
      "pop    r3                    \n\t" \
      "pop    r2                    \n\t" \
      "pop    r1                    \n\t" \
      "pop    r0                    \n\t" \
      "pop    r29                   \n\t" \
      "pop    r28                   \n\t" \
      /* pop RAMPZ if pressent */         \
      arch_pop_rampz                      \
      /* in popped SEG, I bit may be either set or cleared depending if popped \
       * task had interrupts disabled (was switched out by internal OS call) \
       * or enabled (switched out by os_tick() from interrupt */ \
      "pop    r16                   \n\t" \
      /* check if interrupts should be enabled after return, if not then we \
       * must use ret instead of reti, cause reti always enables interrupts \
       * interrupts must stay disabled if picked task to which we are       \
       * switching now was pushed by arch_context_switch from inside of     \
       * critical section of OS */ \
      "sbrc   r16, 7                \n\t" \
      "rjmp   isr_contextrestore_enableint_%=\n\t" \
      "out    __SREG__, r16         \n\t" \
      "pop    r16                   \n\t" \
      /* we will not get interrupt here even if we modify SREG and 2 \
       * instruction passed, since we know that I bit in SREG is disabled */ \
      "ret                          \n\t" \
      "isr_contextrestore_enableint_%=:\n\t" \
      /* here we know that I bit in SREG is enabled, we must enable interrupts \
       * after return, but since between updating SREG and return we will have \
       * more than 2 instructions we need to temporarily disable the I bit and \
       * enable interrupts by reti */ \
      "cbr r16, 0x80                \n\t" \
      "out    __SREG__, r16         \n\t" \
      "pop    r16                   \n\t" \
      /* since we return by reti, always one more instruction is executed \
       * after reti and we can use ISR's to implement OS single stepping \
       * debugger */ \
      "reti                         \n\t" \
      :: )
#else
# error CPUs with extended memory registers are not supported yet
/*      "pop r0                       \n\t" \
 *       "in r0,_SFR_IO_ADDR(EIND)     \n\t" \
 *       "pop r0                       \n\t" \
 *       "in r0,_SFR_IO_ADDR(RAMPZ)    \n\t" */
#endif

#endif /* __OS_PORT_ */

