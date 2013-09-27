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

#ifndef __OS_PORT_
#define __OS_PORT_

#define _GNU_SOURCE

#include <stdlib.h> /* for exit */
#include <stdint.h> /* for patform specific types definitions like uint16_t */
#include <stdbool.h>
#include <limits.h> /* for UINT_MAX etc */
#include <stddef.h>
#include <string.h> /* for memcpy */
#include <signal.h>
#include <ucontext.h>

/* in this struct you may find CPU registers which need to be preserved from context swich perspective.
    There are two possible aproatches:
    - store all CPU context sensitive registers in arch_context_t structure (which is placed at the begining of os_task_t)
    - store only SP in arch_context_t structure, while CPU registers on the task stack
    it is up to user (which prepate specific port) to chose the right method.
    Most simple RTOS use the second method. This is mainly becouse storing registers value on stack
    is the fastest vay to store the registers value. But this solution allows badly writen task code to overwrite
    the context of another task and crash the whole OS (for instance if it will have the pointer to data on other task stack and it will perform buffer overflow)
    Becouse of simmilar sequrity reason in more complicated OS (like Linux) task context is saved on task control structure (aka our arch_context_t),
    This is mainly becouse user code must not be able to compromise the kernel. In out OS we dont have the memory management code
    (aka OS data protection) so both solutions are equaly good if we assume that bad code will at any case make disaster in our system :) */
typedef struct {
    ucontext_t context;
} arch_context_t;

typedef sig_atomic_t arch_atomic_t; /* this is guaranteed to be at least 24 bits wide from POSIX */
typedef uint_fast16_t arch_ticks_t;
#define ARCH_TICKS_MAX ((arch_ticks_t)UINT_FAST16_MAX)
typedef sigset_t arch_criticalstate_t;

extern sigset_t arch_crit_signals;

#define OS_ISR /* not naked since signal in linux are handling the context registers via parameter. Signal handler always sees clobered registers */
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
#define OS_STACK_MINSIZE ((size_t)SIGSTKSZ) /* one 4k page */

#define os_atomic_inc(_atomic) \
    __asm__ __volatile__ ( \
        "incq %[atomic]\n\t" \
            ::  [atomic] "m" (_atomic))

#define os_atomic_dec(_atomic) \
    __asm__ __volatile__ ( \
        "decq %[atomic]\n\t" \
            ::  [atomic] "m" (_atomic))

/* all pointers in any linux are size of CPU register, no need to read or write
 * with any concurent handling */
#define os_atomicptr_read(_ptr) (_ptr)
#define os_atomicptr_write(_ptr, _val) ((_ptr) = (_val))
#define os_atomicptr_xchnge(_ptr, _val) (OS_ASSERT(!"not implemented"))

#define arch_ticks_atomiccpy(_dst, _src) \
  do { \
      *(_dst) = *(_src); /* we can copy as val since linux works on arch > 32bit */ \
  }while(0)

#define arch_critical_enter(_critical_state) \
   do { \
      sigprocmask(SIG_BLOCK, &arch_crit_signals, &(_critical_state)); /* previous signal mask will be stored under _critical_state */ \
   }while(0)

#define arch_critical_exit(_critical_state) \
   do { \
      sigprocmask(SIG_SETMASK, &(_critical_state), NULL); \
   }while(0)

#define arch_dint() \
   do { \
      sigprocmask(SIG_BLOCK, &arch_crit_signals, NULL); \
   }while(0)

#define arch_eint() \
   do { \
      (void)sigprocmask(SIG_UNBLOCK, &arch_crit_signals, NULL); \
   }while(0)

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
 But we have to store all registers anyway. This is why we store all registers and then optionaly store the SP in context of tcb

 For X86 port this macro need to be placed in signal handler function. This function have to be declared as compatible with SA_SIGINFO disposition and it has to have the ucontext parameter
 which will point to stored context. This macoro will use this variable to copy the context (probably temporaly stored on stack) into tack_current->ctx.context. This is required both by OS
 design and becouse this temporary context wil be destroyed once signal handler will return. Linux will use this context to restore the process state. We use this feature to switch betwen tasks.
 Therefore arch_contextrestore_i only copy the newly chosen task into context on stack while Linux kernel do the main job which is restoring the register context. */
#define arch_contextstore_i(_isrName) \
    do { \
      /* context of the task is allready saved on stack by linux kernel, so we can freely use C here (we wont destroy any regs) */ \
      if( 1 >= (++isr_nesting) ) { \
         task_current->ctx.context = *(ucontext_t*)ucontext; /* here we copy the context prepared by linux kernel on current stack, to preserve it for later task restoration */ \
      } \
    } while(0)

/* This function have to:
 - disable IE (in case we achritecture allows for nesting)
 - decrement the isr_nesting
 - if isr_nesting = 0 then
     - restore context from curr_tcb->ctx
     - perform actions that will lead to sustain the power enable after ret
 - else
    - restore all registers
 - perform actions that will lead to enable IE after reti
 - return by reti

 Please first read the note for arch_context_StoreI. The important point here is why we need to enable the interrupt after reti in both cases (in normal and nested).
 This is because we disabled them for task_current manipulation in first step. But we need to enable them because:
 - in case of not nested they was for sure enabled (need to be enabled because we enter ISR ;) )
 - in case of nested the was also for sure enabled (from the same reason, we enter nested ISR) */
#define arch_contextrestore_i(_isrName) \
    do { \
      arch_dint(); \
      if( 0 == (--isr_nesting) ) { \
         memcpy(&(((ucontext_t*)ucontext)->uc_stack), &(task_current->ctx.context.uc_stack), sizeof(task_current->ctx.context.uc_stack)); \
         memcpy(&(((ucontext_t*)ucontext)->uc_mcontext.gregs), &(task_current->ctx.context.uc_mcontext.gregs), 8 * 18); \
         memcpy(&(((ucontext_t*)ucontext)->__fpregs_mem), &(task_current->ctx.context.__fpregs_mem), sizeof(task_current->ctx.context.__fpregs_mem)); \
      } \
      /* in oposite case registers will be automaticly poped by linux kernel */ \
      memcpy(&(((ucontext_t*)ucontext)->uc_sigmask), &(task_current->ctx.context.uc_sigmask), sizeof(task_current->ctx.context.uc_sigmask)); \
    }while(0)

#endif /* __OS_PORT_ */

