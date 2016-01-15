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

#include "os_private.h"

#include <sched.h> /* sched_yield used only here */

/* signal set masked during critical sections, we use global variable to set/unset the signal mask in fast way */
sigset_t arch_crit_signals;

/* this function is x86 port specific.
   To make atomic signal masking and task switch we must use the signal service, for that we use SIGUSR1 */
void OS_ISR arch_sig_switch(int OS_UNUSED(signum), siginfo_t * OS_UNUSED(siginfo), void *ucontext)
{
   memcpy(&(((ucontext_t*)ucontext)->uc_stack), &(task_current->ctx.context.uc_stack), sizeof(task_current->ctx.context.uc_stack));
   memcpy(&(((ucontext_t*)ucontext)->uc_mcontext.gregs), &(task_current->ctx.context.uc_mcontext.gregs), 8 * 18);
   memcpy(&(((ucontext_t*)ucontext)->__fpregs_mem), &(task_current->ctx.context.__fpregs_mem), sizeof(task_current->ctx.context.__fpregs_mem));
   memcpy(&(((ucontext_t*)ucontext)->uc_sigmask), &(task_current->ctx.context.uc_sigmask), sizeof(task_current->ctx.context.uc_sigmask));
}

void arch_os_start(void)
{
   int ret;
   sigset_t sigmask_all;
   struct sigaction switch_sigaction = {
      .sa_sigaction = arch_sig_switch,
      .sa_mask = { { 0 } }, /* additional (beside the current signal) mask (they will be added to the mask instead of set) */
      .sa_flags = SA_SIGINFO , /* use sa_sigaction instead of old sa_handler */
      /* SA_NODEFER could be used if we would like to have the nesting enabled right durring the signal handler enter */
      /* SA_ONSTACK could be used if we would like to use the signal stack instead of thread stack */
   };

   /* prepare the global set for signals masked during critical sections
    * we cannot be interrupted by any signal, beside SIGUSR1 used as a helper
    * for context switching */
   ret = sigfillset(&arch_crit_signals); /* blocking all signals */
   OS_ASSERT(0 == ret);
   ret = sigdelset(&arch_crit_signals, SIGUSR1); /* beside SIGUSR1 used for context switching */
   OS_ASSERT(0 == ret);

   /* setup the signal disposition for SIGUSR1, to call arch_sig_switch */
   ret = sigfillset(&sigmask_all); /* we forbid the signal nesting while handling SIGUSR1, we cannot be interrupted while handling SIGUSR1 or it will break everything */
   OS_ASSERT(0 == ret);
   switch_sigaction.sa_mask = sigmask_all;
   ret = sigaction(SIGUSR1, &switch_sigaction, NULL);
   OS_ASSERT(0 == ret);
}

/*
  \note Interrupts will be disabled during execution of below code. They need to be disabled because of preemption (concurent access to task_current)
  This function has to:
 - store task context in the same place as arch_contextstore_i (power bits do not have to be necessarily stored, IE have to be stored because we need to atomically disable interrupts when we will pop this task)
 - perform task_current = new_task;
 - restore context the same way as in arch_contextrestore
 - perform actions that will lead to sustaining the power enable after popping the SR (task could be stored by ISR so task possibly may have the power bits disabled)
 - perform actions that will lead to restoring (after ret) the IE flag state saved when task context was dumped (we may switch to preempted task so we need to enable IE if IE was disabled when entering arch_context_switch)
 - return by ret
*/
void /* OS_NAKED */ OS_HOT arch_context_switch(os_task_t *new_task)
{
   /* for x86 this should be the OS_NAKED function but gcc does not support naked attribute in Linux user space environment
   because of this and because gtcontext is a function (not a macro) we need to fix the IP SP and BP */

   (void)getcontext(&(task_current->ctx.context));
   task_current->ctx.context.uc_mcontext.gregs[REG_RIP] = (long int)__builtin_return_address(0);
   task_current->ctx.context.uc_mcontext.gregs[REG_RSP] += 0x20;
   task_current->ctx.context.uc_mcontext.gregs[REG_RBP] = (long int)__builtin_frame_address(1);
   task_current = new_task;
   raise(SIGUSR1); /* we need to atomically switch the signal mask, this is achievable only by signal service */
}

void arch_task_start(os_taskproc_t proc, void* param)
{
   arch_eint();
   os_task_exit(proc(param));
}

/* This function has to:
- initialize the stack as it will be left after arch_contextstore
- initialize the arch_context_t in os_task_t as it will be left after arch_contextstore
- ensure the task will have the interrupts enabled after it enters proc, on some archs this may be also made in arch_task_start
 /param stack pointer to stack end, it will have the same meaning as sp on a particular arch
*/
void arch_task_init(os_task_t *task, void* stack_param, size_t stack_size, os_taskproc_t proc, void* param)
{
   unsigned long *stack = (unsigned long*)stack_param;
   OS_ASSERT(0 == ((unsigned long)stack_param & 1)); /* in Linux stack has to be aligned to unsigned long */

   if(getcontext(&(task->ctx.context))) OS_ASSERT(0);
   task->ctx.context.uc_stack.ss_sp = ((char*)stack);
   task->ctx.context.uc_stack.ss_size = stack_size;
   task->ctx.context.uc_link = NULL;
   makecontext(&(task->ctx.context), (void(*)(void))arch_task_start, 2, proc, param);
}

void OS_NORETURN OS_COLD arch_halt(void)
{
   while(1) {
      raise(SIGABRT);
      abort();
   }
}

void arch_idle(void)
{
  (void)sched_yield();
}

