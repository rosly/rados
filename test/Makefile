# 
# This file is a part of RadOs project
# Copyright (c) 2013, Radoslaw Biernaki <radoslaw.biernacki@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1) Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2) Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3) No personal names or organizations' names associated with the 'RadOs' project
#    may be used to endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE RADOS PROJET AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

ECHO = /bin/echo -e

#by defining ARCH enviroment variable, user can compile for different architectures
#if no ARCH variable is defined, architecture is defined by target.mk
ifeq ($(ARCH),)
include ../target.mk
endif
#each architecture have its own target.mk file where CC, ARCHSOURCES, CFLAGS
#variables are defined
include ../$(ARCH)/target.mk

TESTSOURCE = \
	test1.c \
	test2.c \
	test3.c \
	test4.c
ifeq ("$(ARCH)", "linux")
TESTSOURCE += \
	test_join.c \
	test_sem.c \
	test_mtx.c \
	test_timer.c
endif

INCLUDES = . .. ../$(ARCH)
BUILDDIR = ../build/$(ARCH)
LIBDIR = ../build/$(ARCH)
LIBS = kernel

#in target.mk for each source the optima optimization level (CFLAGS = -Ox) is defined
#but here we add CFLAGS += -g if debug build
ifeq ($(DEBUG),)
CFLAGS += -g
endif
#regardles architecture we use highest warning level
CFLAGS += -Wall -Wextra -Werror
LDFLAGS +=

vpath %.c .
vpath %.o $(BUILDDIR)
vpath %.a $(BUILDDIR)
DEPEND = $(addprefix $(BUILDDIR)/, $(TESTSOURCE:.c=.d))
OBJECTS = $(addprefix $(BUILDDIR)/, $(TESTSOURCE:.c=.o))
TARGETS = $(addprefix $(BUILDDIR)/, $(TESTSOURCE:.c=.elf))
LIBFILE = $(addprefix $(LIBDIR)/lib, $(addsuffix .a, $(LIBS)))

all: $(LIBFILE) $(TARGETS)

$(BUILDDIR)/%.elf: $(BUILDDIR)/%.o $(LIBFILE)
	@$(ECHO) "[LN]\t$@"
	@$(CC) $< -L$(LIBDIR) -o $@ $(addprefix -l, $(LIBS)) $(LDFLAGS) 

$(BUILDDIR)/%.o: %.c
	@$(ECHO) "[CC]\t$<"
	@$(CC) -c $(CFLAGS) -o $@ $(addprefix -I, $(INCLUDES)) $<

$(LIBFILE):
	@$(ECHO) "Missing kernel objects, build the kernel first"
	abort

# include the dependencies unless we're going to clean, then forget about them.
ifneq ($(MAKECMDGOALS), clean)
-include $(DEPEND)
endif
# dependencies file
# includes also considered, since some of these are our own
# (otherwise use -MM instead of -M)
$(BUILDDIR)/%.d: %.c
	@$(ECHO) "[DEP]\t$<"
	@$(CC) -M ${CFLAGS} $(addprefix -I, $(INCLUDES)) $< >$@

.PHONY: clean testrun

testrun: all
	@$(addsuffix ;, $(TARGETS))

clean:
	@$(RM) $(TARGETS); $(ECHO) "[RM]\t$(TARGETS)"
	@$(RM) $(OBJECTS); $(ECHO) "[RM]\t$(OBJECTS)"
	@$(RM) $(DEPEND); $(ECHO) "[RM]\t$(DEPEND)"

