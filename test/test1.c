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

/**
 * /file Test os OS port (step 1)
 * /ingroup tests
 *
 * This is first basic test to check the port.
 * Test if task_proc is called and if it can block on semaphore, test if idle
 * procedure will be called (because of task block)
 * Test in following services are implemented corecly:
 * - task (stack and context) initalization is pefromed corectly
 * - arch_context_switch implemented corectly (at least called 2 times init_idle->task1->idle)
 * /{
 */

#include <os.h>
#include <os_test.h>

static os_task_t task1;
static long int task1_stack[OS_STACK_MINSIZE];
static os_sem_t sem1;
static int task1_started = 0;

void idle(void)
{
   test_result(task1_started ? 0 : -1);
}

int task1_proc(void* OS_UNUSED(param))
{
   int ret;

   task1_started = 1;

   ret = os_sem_down(&sem1, OS_SEMTIMEOUT_INFINITE);
   test_debug("fail: od_sem_down returned with code %d", ret);
   test_result(-1);

   return 0;
}

void init(void)
{
   os_sem_create(&sem1, 0);
   os_task_create(&task1, 1, task1_stack, sizeof(task1_stack), task1_proc, NULL);
}

int main(void)
{
   test_setupmain("Test1");
   os_start(init, idle);
   return 0;
}

/** /} */

