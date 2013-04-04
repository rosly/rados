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

CFLAGS   = -g -O0 -std=gnu99 -Wall -Wextra
LDFLAGS  = -lrt

PORTSOURCES = \
	os_port.c \
	os_test.c

