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

#include "os_private.h"

/* format of the context on stack for MSP430
hi address
    PC - pushed frst
    R0 - stored to gain one free register
    SREG
    R1 - R31 - pusched last
low address
*/

/*
  \note Interrupts will be disabled during execution of below code. they need to
        be disabled because of preemption (concurent access to task_current)
  This function have to:
 - store task context in the same place as arch_contextstore_i (power bits does
   not have to be necessarly stored, IE have to be stored because we need to
   atomicaly disable the interupts when we will pop this task) - perform
   task_current = new_task;
 - restore context as same as in arch_contextrestore
 - perform actions that will lead to sustain the power enable after poping the
   SR (task could be stored by ISR so task possibly may have the power bits dis
 - perform actions that will lead to restore after ret the IE flag state saved
   when task context was dumped (we may swith to peempted task so we ned to enable
   IE while IE was disabled durring enter of arch_context_switch)
 - return by ret
*/
#ifndef __AVR_3_BYTE_PC__
//#ifndef __AVR_HAVE_RAMPZ__
void OS_NAKED OS_HOT arch_context_switch(os_task_t * new_task)
{
  __asm__ __volatile__ ( \
      /* calling of this function requires that interupts are disabled */ \
      "push    r16"           "\n\t" \
      /* store dummy cleared SREG (can be clobered) */ \
      "eor     r16,r16"       "\n\t" \
      "push    r16"           "\n\t" \
      /* store old fame pointer keept in Y */ \
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
      /* skip  r16 - aleady stored */ \
      "push    r17"           "\n\t" \
      /* remain registers r18-r27 and r30-r31 we can skip since they are \
       * threated by gcc as call-used and we are in ordinar C function */ \
      "in      r28, __SP_L__" "\n\t" /* load SPL into r28 (means Ya) */ \
      "in      r29, __SP_H__" "\n\t" /* load SPH into r29 (means Yb) */ \
      "sbiw    r28, 12"       "\n\t" /* skip 12bytes on stack */ \
      /* store SP into task_current->ctx */ \
      "lds     r30, task_current"  "\n\t"  /* load Z with curent_task pointer */ \
      "lds     r31, task_current+1" "\n\t"  /* load Z with curent_task pointer */ \
      "st      Z,   r28"      "\n\t"   /* store SPL into *(task_current) */ \
      "std     Z+1, r29"      "\n\t"   /* store SPH into *(task_current) */ \
      \
      /* switch task_current = new_task */ \
      "sts     task_current, %A0\n\t" \
      "sts     task_current+1, %B0\n\t" \
      \
      /* restore SP from task_current->ctx */ \
      "movw    r30, %0"             "\n\t" /* load Z with new_task pointer */ \
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
      /* poping true value of SREG is important here, since we may restore task \
       * which was pushed by ISR (for instance preemption tick) */ \
      "pop    r16"                 "\n\t" \
      "out    __SREG__, r16"       "\n\t" \
      /* restore r16 and ret */           \
      "pop    r16"                  "\n\t" \
      /* in this moment we can get interrupt !!!! \
         \TODO this may be critical in case of constant interrupt, we will end \
         up in filling the stack!!! */
      "ret"                        "\n\t" \
            ::  [new_task] "r" (new_task));
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

/* this function have to:
- initialize the stack as it will be left after arch_contextstore
- initialize the arch_context_t in os_task_t as it will be left after arch_contextstore
- ensure that task will have the interrupts enabled after it enters proc, on some arch this may be also used in arch_task_start
 /param stack pointer to stack end, it will have the same meaning as sp on paticular arch
*/
void arch_task_init(os_task_t * task, void* stack_param,
                    size_t stack_size, os_taskproc_t proc,
                    void* param)
{
   uint8_t *stack = ((uint8_t*)stack_param) + stack_size - 1; /* for AVR we have descending stack */

   /* in AVR stack works in postdectement on push (preincrement on pop) */
   *(stack--) = (uint8_t)((uint16_t)arch_task_start & 0xFF);
   *(stack--) = (uint8_t)((uint16_t)arch_task_start >> 8);;
   *(stack--) = 0; /* R16 */
   *(stack--) = 1 << SREG_I; /* SFR with interupts enabled */

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

void OS_NORETURN OS_COLD arch_halt(void)
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

