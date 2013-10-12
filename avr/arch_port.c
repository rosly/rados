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
void OS_NAKED OS_HOT arch_context_switch(os_task_t * new_task)
{
  __asm__ __volatile__ ( \
      /* calling of this function requires that interupts are disabled */ \
      /* store dummy for r0 and SREG (both can be clobered), then store r1 */ \
      "push    r0"            "\n\t" \
      "push    r1"            "\n\t" \
      "push    r1"            "\n\t" \
      /* store old fame pointer keept in Y */ \
      "push    r28"           "\n\t" \
      "push    r29"           "\n\t" \
      /* then all call-saved registers */ \
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
      /* remain registers r18-r27 and r30-r31 we can skip since they are \
       * threated by gcc as call-used */ \
      "in      r28, __SP_L__" "\n\t" /* load SPL into r28 (means Ya) */ \
      "in      r29, __SP_H__" "\n\t" /* load SPH into r29 (means Yb) */ \
      "adiw    r28, 12"       "\n\t" /* skip 12bytes on stack */ \
      /* store updated stack pointer (may be needed \TODO realy needed ?) */ \
      "out     __SP_L__, r28" "\n\t" /* store SPL */ \
      "out     __SP_H__, r29" "\n\t" /* store SPH */ \
      /* store SP into task_current->ctx */ \
      "sts     task_current, %0a\n\t" \
      "sts     task_current+1, %0b\n\t" \


      "lds     r30, %[ctx]"   "\n\t"  /* load Z by addr of curent_task */ \
      "lds     r31, %[ctx]+1" "\n\t"  /* load Z by addr of curent_task */ \
      "st      Z,   r28"      "\n\t"   /* store SPL into task_curent->ctx */ \
      "std     Z+1, r29"      "\n\t"   /* store SPH into task_curent->ctx */ \
          ::  [new_task] "d" (new_task));

   task_current = new_task;

    __asm__ __volatile__ ( \
        /* restore SP from task_current->ctx */ \
        "lds     r30, %[ctx]"         "\n\t" /* load Z by addr of curent_task */ \
        "lds     r31, %[ctx]+1"       "\n\t" /* load Z by addr of curent_task */ \
        "ld      r16, Z"              "\n\t" /* load SPL into task_curent->ctx */ \
        "ldd     r17, Z+1"            "\n\t" /* load SPH into task_curent->ctx */ \
        "out     __SP_L__, r16"       "\n\t" /* load SPL from r16 */ \
        "out     __SP_H__, r17"       "\n\t" /* load SPH from r17 */ \
        /* restore all register */ \
        /* skip r18-r27 and r30-31 */ \
        "adiw    r28, 12"            "\n\t" /* skip 12bytes on stack */ \
        "out     __SP_L__, r28"      "\n\t" /* store SPL */ \
        "out     __SP_H__, r29"      "\n\t" /* store SPH */ \
        /* restore call-saved registers */ \
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
        /* dumy SREG and r0 (both can be clobered), then return by ret */ \
        "pop    r0"                  "\n\t" \
        "pop    r0"                  "\n\t" \
        "ret"                       "\n\t" \
            ::  [ctx] "p" (task_current));
}

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
void arch_task_init(os_task_t * OS_UNUSED(task), void* OS_UNUSED(stack_param),
                    size_t OS_UNUSED(stack_size), os_taskproc_t OS_UNUSED(proc),
                    void* OS_UNUSED(param))
{
}

void OS_NORETURN OS_COLD arch_halt(void)
{
   arch_dint();
   while(1) {
      /* \TODO put CPU into relax */
   }
}

