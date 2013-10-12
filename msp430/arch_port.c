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

#include "os_private.h"

/* format of the context on stack for MSP430
low adress
    PC - pushed frst
    R3(SR) - pushed automaticly on ISR enter
    R4 - R15 - pusched last
hi address
*/

/*
  \note Interrupts will be disabled durring execution of below code. they ned to be disabled becouse of preemption (concurent access to task_current)
  This function have to:
 - store task context in the same place as arch_contextstore_i (power bits does not have to be necessarly stored, IE have to be stored becouse we need to atomicaly disable the interupts when we will pop this task)
 - perform task_current = new_task;
 - restore context as same as in arch_contextrestore
 - perform actions that will lead to sustain the power enable after poping the SR (task could be stored by ISR so task possibly may have the power bits dis
 - perform actions that will lead to restore after ret the IE flag state saved when task context was dumped (we may swith to peempted task so we ned to enable IE while IE was disabled durring enter of arch_context_switch)
 - return by ret
*/
void OS_NAKED OS_HOT arch_context_switch(os_task_t *new_task)
{
   __asm__ __volatile__ (
      "push r2\n\t"
      "pushm 12,r15\n\t" /* pushing R4 -R15 */
      "mov %[ctx], r4\n\t"
      "mov r1, @r4\n\t"
         ::  [ctx] "m" (task_current));
   task_current = new_task;
   __asm__ __volatile__ (
      "mov %[ctx], r1\n\t"
      "mov @r1, r1\n\t"
      "popm 12,r15\n\t" /* poping R4 -R15 */
      "bic %[powerbits], @r1 \n\t" /* enable full power mode in SR which will be poped */
      "pop r2\n\t"
      "ret\n\t"
         ::  [ctx] "m" (task_current),
             [powerbits] "i" (SCG1+SCG0+OSCOFF+CPUOFF));
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
void arch_task_init(os_task_t *task, void* stack_param, size_t stack_size, os_taskproc_t proc, void* param)
{
   uint16_t *stack = (uint16_t*)(((uint8_t*)stack_param) + stack_size); /* for msp430 we have descending stack */
   OS_ASSERT(0 == ((uint16_t)stack_param & 1)); /* in MSP430 stack has to be alligned to uint16_t */

   stack--; /* in MSP430 stack works in predectement on push */
   *(stack--) = (uint16_t)arch_task_start;
   *(stack--) = GIE; /* enable interrupts after task starts */
   *(stack--) = (uint16_t)proc; /* R15 */
   *(stack--) = (uint16_t)param; /* R14 */
   *(stack--) = 0; /* R13 */
   *(stack--) = 0; /* R12 */
   *(stack--) = 0; /* R11 */
   *(stack--) = 0; /* R10 */
   *(stack--) = 0; /* R9 */
   *(stack--) = 0; /* R8 */
   *(stack--) = 0; /* R7 */
   *(stack--) = 0; /* R6 */
   *(stack--) = 0; /* R5 */
   *(stack--) = 0; /* R4 */
   stack++; /* in MSP430 stack works in postincrement on pop, in other words sp point to value which will be poped in next op */

   task->ctx.sp = (uint16_t)stack;
}

void OS_NORETURN OS_COLD arch_halt(void)
{
   arch_dint();
   while(1) {
      __bis_status_register(LPM0_bits);
      nop();
   }
}

