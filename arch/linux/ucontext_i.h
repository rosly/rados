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

#include <asm/ucontext.h>

--

SIG_BLOCK
SIG_SETMASK

_NSIG8		(_NSIG / 8)

#define ucontext(member)	offsetof (ucontext_t, member)
#define mcontext(member)	ucontext (uc_mcontext.member)
#define mreg(reg)		mcontext (gregs[REG_##reg])

oRBP		mreg (RBP)
oRSP		mreg (RSP)
oRBX		mreg (RBX)
oR8		mreg (R8)
oR9		mreg (R9)
oR10		mreg (R10)
oR11		mreg (R11)
oR12		mreg (R12)
oR13		mreg (R13)
oR14		mreg (R14)
oR15		mreg (R15)
oRDI		mreg (RDI)
oRSI		mreg (RSI)
oRDX		mreg (RDX)
oRAX		mreg (RAX)
oRCX		mreg (RCX)
oRIP		mreg (RIP)
oEFL		mreg (EFL)
oFPREGS		mcontext (fpregs)
oSIGMASK	ucontext (uc_sigmask)
oFPREGSMEM	ucontext (__fpregs_mem)
oMXCSR		ucontext (__fpregs_mem.mxcsr)
