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
CFLAGS   = -O3
else
#for debug buld we use -O0 do not obstruct the generated code
CFLAGS   = -O0
endif
CFLAGS   += -std=gnu99 
LDFLAGS  = -lrt

