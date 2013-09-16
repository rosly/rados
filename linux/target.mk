CC       = gcc
LD       = ld
AR       = ar
AS       = gcc
GASP     = gasp
NM       = nm
OBJCOPY  = objcopy
OBJDUMP  = objdump
RANLIB   = ranlib
STRIP    = strip
SIZE     = size
READELF  = readelf
#MAKETXT  = srec_cat
CP       = cp -p
RM       = rm -f
MV       = mv

ifeq ($(DEBUG),)
#for debug buld we use -O0 do not obstruct the generated code
CFLAGS   = -O0
else
CFLAGS   = -O2
endif
CFLAGS   += -std=gnu99 
LDFLAGS  = -lrt

ARCHSOURCES = \
	arch_port.c \
	arch_test.c

