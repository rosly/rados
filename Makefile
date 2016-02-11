# This file is a part of RadOs project
# Copyright (c) 2013, Radoslaw Biernacki <radoslaw.biernacki@gmail.com>
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
# THIS SOFTWARE IS PROVIDED BY THE RADOS PROJECT AND CONTRIBUTORS "AS IS" AND
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

#by defining ARCH enviroment variable, user can compile for different architectures
ifeq ($(ARCH),)
$(error ARCH is not defined, check your enviroment ARCH variable)
endif
#each subproject should use the same master configuration taken from master
#project, if master project configdir is not given then use the local one
ifeq ($(CONFIGDIR),)
export CONFIGDIR = $(CURDIR)
endif
#there are common tools used in build system
include $(CONFIGDIR)/tools.mk
#each architecture have its own target.mk file where CC, CFLAGS variables are defined
include $(CONFIGDIR)/arch/$(ARCH)/target.mk
#ARCHSOURCES are defined separately
include arch/$(ARCH)/source.mk

KERNELSOURCEDIR = source
LIBSOURCEDIR = lib
BUILDDIR ?= build/$(ARCH)
ARCHDIR = arch/$(ARCH)
INCLUDEDIR = $(ARCHDIR) $(KERNELSOURCEDIR) $(LIBSOURCEDIR)
KERNELSOURCES = \
	os_sched.c \
	os_sem.c \
	os_mtx.c \
	os_waitqueue.c \
	os_timer.c \
	os_mbox.c \
	os_test.c
LIBSOURCES = \
	ring.c
SOURCES = \
   $(KERNELSOURCES) \
   $(LIBSOURCES) \
	$(ARCHSOURCES)

#in target.mk for each source the optimal optimization level (CFLAGS = -Ox) is defined
#but here we add CFLAGS += -g if debug build
ifneq ($(DEBUG),)
CFLAGS += -g
endif
#regardles architecture we use highest warning level
CFLAGS += -Wall -Wextra -Werror -ffunction-sections -fdata-sections
#we only produce library in this Makefile, but this is default LDFLAGS which can
#be used in executables
#LDFLAGS += -Wl,--gc-sections
#if you encounter problem with stripong data or code, check following -Wl,--print-gc-sections

vpath %.c $(KERNELSOURCEDIR) $(LIBSOURCEDIR) $(ARCHDIR)
vpath %.o $(BUILDDIR)
vpath %.elf $(BUILDDIR)
STYLESOURCES = \
   $(addprefix $(KERNELSOURCEDIR)/, $(KERNELSOURCES:.c=.c)) \
   $(addprefix $(KERNELSOURCEDIR)/, $(KERNELSOURCES:.c=.h)) \
   $(addprefix $(LIBSOURCEDIR)/, $(LIBSOURCES:.c=.c)) \
   $(addprefix $(LIBSOURCEDIR)/, $(LIBSOURCES:.c=.h)) \
   $(addprefix $(ARCHDIR)/, $(ARCHSOURCES:.c=.c)) \
   $(addprefix $(ARCHDIR)/, $(ARCHSOURCES:.c=.h)) 
DEPEND = $(addprefix $(BUILDDIR)/, $(SOURCES:.c=.d))
OBJECTS = $(addprefix $(BUILDDIR)/, $(SOURCES:.c=.o))
LISTINGS = $(addprefix $(BUILDDIR)/, $(SOURCES:.c=.lst))
BUILDTARGET = $(BUILDDIR)/libkernel.a

all: $(BUILDTARGET) size
lst: $(LISTINGS)

$(BUILDTARGET): $(OBJECTS)
	@$(ECHO) "[AR]\t$@"
	@$(RM) $@
	@$(AR) -cq $@ $^

$(BUILDDIR)/%.o: %.c
	@$(ECHO) "[CC]\t$<"
	@$(CC) -save-temps=obj -c $(CFLAGS) -o $@ $(addprefix -I, $(INCLUDEDIR)) $<

$(BUILDDIR)/%.lst: %.o
	@$(ECHO) "[LST]\t$<"
	@$(OBJDUMP) -dStw $< > $@

size: $(BUILDTARGET)
	@$(ECHO) "[SIZE]\t$^"
	@$(SIZE) -t $^

# include the dependencies unless we're going to clean, then forget about them.
ifneq ($(MAKECMDGOALS), clean)
-include $(DEPEND)
endif
# dependencies file
$(BUILDDIR)/%.d: %.c
	@$(ECHO) "[DEP]\t$<"
	@$(CC) -MM -MT $(@:.d=.o) ${CFLAGS} $(addprefix -I, $(INCLUDEDIR)) $< >$@

.PHONY: clean test testrun testloop lst size

clean:
	@$(ECHO) "[RM]\t$(BUILDTARGET)"; $(RM) $(BUILDTARGET)
	@$(ECHO) "[RM]\t$(OBJECTS)"; $(RM) $(OBJECTS)
	@$(ECHO) "[RM]\t$(DEPEND)"; $(RM) $(DEPEND)
	@$(ECHO) "[RM]\t$(LISTINGS)"; $(RM) $(LISTINGS)
	@$(ECHO) "[RM]\t[temps]"; $(RM) $(BUILDDIR)/*.s $(BUILDDIR)/*i
	@$(MAKE) --no-print-directory -C test clean

test: $(BUILDTARGET)
	@$(MAKE) --no-print-directory -C test

testrun: test
	@$(MAKE) --no-print-directory -C test testrun

testloop: test
	@$(MAKE) --no-print-directory -C test testloop

style:
	@$(STYLE) -c uncrustify.cfg $(STYLESOURCES)
	@$(MAKE) --no-print-directory -C test style

