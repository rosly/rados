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

#include "os_private.h"

/* format of the context on stack for AVR
hi address
    PC - pushed first
    R0 - stored to gain one free register
    SREG
    R1 - R31 - pushed last
low address
*/

/*
  \note Interrupts will be disabled during execution of below code. They need to
        be disabled because of preemption (concurrent access to task_current)
  This function has to:
 - store task context in the same place as arch_contextstore_i (power bits do
   not have to be necessarily stored, IE have to be stored because we need to
   atomically disable the interrupts when we will pop this task) - perform
   task_current = new_task;
 - restore context the same way as in arch_contextrestore
 - perform actions that will lead to sustaining the power enable after popping the
   SR (task could be stored by ISR so task possibly may have the power bits disabled)
 - perform actions that will lead to restoring (after ret) the IE flag state saved
   when task context was dumped (we may switch to preempted task so we need to enable
   IE while IE was disabled when entering arch_context_switch)
 - return by ret
*/

#ifndef __AVR_3_BYTE_PC__
void OS_NAKED OS_HOT arch_context_switch(os_task_t * new_task)
{
  __asm__ __volatile__ ( \
      /* store r16 and use it as temporary register */ \
      "push    r16"           "\n\t" \
      /* calling of this function requires that interrupts are disabled */ \
      /* store dummy cleared SREG (can be clobbered, I bit must be cleared) */ \
      "eor     r16,r16"       "\n\t" \
      "push    r16"           "\n\t" \
      /* push RAMPZ if present */   \
      arch_push_rampz                \
      /* store old frame pointer kept in Y */ \
      "push    r28"           "\n\t" \
      "push    r29"           "\n\t" \
      /* then all call-saved registers */ \
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
      /* skip  r16 - already stored */ \
      "push    r17"           "\n\t" \
      /* remaining registers r18-r27 and r30-r31 we can skip since they are \
       * treated by gcc as call-used and we are in ordinary C function */ \
      "in      r28, __SP_L__" "\n\t" /* load SPL into r28 (means Ya) */ \
      "in      r29, __SP_H__" "\n\t" /* load SPH into r29 (means Yb) */ \
      "sbiw    r28, 12"       "\n\t" /* skip 12bytes on stack */ \
      /* store SP into task_current->ctx */ \
      "lds     r30, task_current"  "\n\t"  /* load Z with current_task pointer */ \
      "lds     r31, task_current+1" "\n\t"  /* load Z with current_task pointer */ \
      "st      Z,   r28"      "\n\t"   /* store SPL into *(task_current) */ \
      "std     Z+1, r29"      "\n\t"   /* store SPH into *(task_current) */ \
      \
      /* switch task_current = new_task */ \
      "sts     task_current, %A0\n\t" \
      "sts     task_current+1, %B0\n\t" \
      \
      /* restore SP from task_current->ctx */ \
      "movw    r30, %0"             "\n\t" /* load Z with new_task pointer */
      "ld      r28, Z"              "\n\t" /* load SPL from *(task_current) */ \
      "ldd     r29, Z+1"            "\n\t" /* load SPH from *(task_current) */ \
      "out     __SP_L__, r28"      "\n\t" /* restore new SPL */ \
      "out     __SP_H__, r29"      "\n\t" /* restore new SPH */ \
      /* restore all registers */ \
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
      /* pop RAMPZ if present */         \
      arch_pop_rampz                      \
      /* in popped SEG, I bit may be either set or cleared depending if popped \
       * task had interrupts disabled (was switched out by internal OS call) \
       * or enabled (switched out by os_tick() from interrupt */ \
      "pop    r16"                 "\n\t" \
      /* check if interrupts should be enabled after return, if not then we \
       * must use ret instead of reti, cause reti always enables interrupts \
       * interrupts must stay disabled if picked task to which we are switching \
       * now was pushed by arch_context_switch from inside of critical section \
       * of OS */ \
      "sbrc   r16, 7"              "\n\t" \
      "rjmp   arch_context_switch_enableint_%=\n\t" \
      "out    __SREG__, r16"       "\n\t" \
      "pop    r16"                 "\n\t" \
      /* we will not get interrupt here even if we modify SREG and 2 \
       * instruction passed, since we know that I bit in SREG is disabled */ \
      "ret"                        "\n\t" \
   "arch_context_switch_enableint_%=:\n\t" \
      /* here we know that I bit in SREG is enabled, we must enable interrupts * \
       * after return, but since between updating SREG and return we will have * \
       * more than 2 instructions we need to temporarily disable the I bit and * \
       * enable interrupts by reti */ \
      "cbr r16, 0x80"              "\n\t" \
      "out    __SREG__, r16"       "\n\t" \
      "pop    r16"                 "\n\t" \
      /* since we return by reti, always one more instruction is executed \
       * after reti and we can use ISR to implement OS single stepping \
       * debugger */ \
      "reti"                       "\n\t" \
      :: [new_task] "r" (new_task)        \
      );
}
#else
#error 3byte PC is not supported yet
#endif

/* for description look at os_private.h */
void arch_os_start(void)
{
   /* nothing to do */
}

void OS_NAKED arch_task_start(os_taskproc_t proc, void* param)
{
   os_task_exit(proc(param));
}

/* this function has to:
- initialize the stack as it will be left after arch_contextstore
- initialize the arch_context_t in os_task_t as it will be left after arch_contextstore
- ensure that task will have the interrupts enabled after it enters proc, on some archs this may be also used in arch_task_start
 /param stack pointer to stack end, it will have the same meaning as sp on a particular arch
*/


void arch_task_init(os_task_t * task, void* stack_param,
                    size_t stack_size, os_taskproc_t proc,
                    void* param)
{
   uint8_t *stack = ((uint8_t*)stack_param) + stack_size - 1; /* for AVR we have descending stack */

   /* in AVR stack works in postdecrement on push (preincrement on pop) */
   *(stack--) = (uint8_t)((uint16_t)arch_task_start & 0xFF);
   *(stack--) = (uint8_t)((uint16_t)arch_task_start >> 8);;
   *(stack--) = 0; /* R16 */
   *(stack--) = 1 << SREG_I; /* SFR with interrupts enabled */
#ifdef __AVR_HAVE_RAMPZ__
   *(stack--) = 0; /* RAMPZ */
#endif

   /* store position of frame pointer reg on stack */
   *(stack--) = (((uint16_t)((uint8_t*)stack_param) + stack_size - 3) & 0x00ff); /* R28 */
   *(stack--) = (((uint16_t)((uint8_t*)stack_param) + stack_size - 3) & 0xff00) >> 8; /* R29 */

   *(stack--) = 0; /* R0 */
   *(stack--) = 0; /* R1 */
   *(stack--) = 0; /* R2 */
   *(stack--) = 0; /* R3 */
   *(stack--) = 0; /* R4 */
   *(stack--) = 0; /* R5 */
   *(stack--) = 0; /* R6 */
   *(stack--) = 0; /* R7 */
   *(stack--) = 0; /* R8 */
   *(stack--) = 0; /* R9 */
   *(stack--) = 0; /* R10 */
   *(stack--) = 0; /* R11 */
   *(stack--) = 0; /* R12 */
   *(stack--) = 0; /* R13 */
   *(stack--) = 0; /* R14 */
   *(stack--) = 0; /* R15 */
   /* skip R16 */
   *(stack--) = 0; /* R17 */
   *(stack--) = 0; /* R18 */
   *(stack--) = 0; /* R19 */
   *(stack--) = 0; /* R20 */
   *(stack--) = 0; /* R21 */
   *(stack--) = (((uint16_t)param) & 0x00ff);      /* R22 */
   *(stack--) = (((uint16_t)param) & 0xff00) >> 8; /* R23 */
   *(stack--) = (((uint16_t)proc) & 0x00ff);       /* R24 */
   *(stack--) = (((uint16_t)proc) & 0xff00) >> 8;  /* R25 */
   *(stack--) = 0; /* R26 */
   *(stack--) = 0; /* R27 */
   *(stack--) = 0; /* R30 */
   *(stack--) = 0; /* R31 */

   /* store stack pointer in task context */
   task->ctx.sp = (uint16_t)stack;
}

void /* \TODO removed because of missing backstack OS_NORETURN */ OS_COLD arch_halt(void)
{
   arch_dint();
   while(1) {
      /* \TODO put CPU into relax */
   }
}

void arch_idle(void)
{
  /* \TODO implement power save code */
}

uint_fast8_t arch_bitmask_fls(arch_bitmask_t bitfield)
{
   static const uint8_t log2lkup[256] = {
      0U, 1U, 2U, 2U, 3U, 3U, 3U, 3U, 4U, 4U, 4U, 4U, 4U, 4U, 4U, 4U,
      5U, 5U, 5U, 5U, 5U, 5U, 5U, 5U, 5U, 5U, 5U, 5U, 5U, 5U, 5U, 5U,
      6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U,
      6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U, 6U,
      7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U,
      7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U,
      7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U,
      7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U, 7U,
      8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U,
      8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U,
      8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U,
      8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U,
      8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U,
      8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U,
      8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U,
      8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U
   };

   return log2lkup[bitfield];
}

