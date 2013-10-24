MCU      = cc430f6137

CC       = msp430-gcc
LD       = msp430-ld
AR       = msp430-ar
AS       = msp430-gcc
GASP     = msp430-gasp
NM       = msp430-nm
OBJCOPY  = msp430-objcopy
OBJDUMP  = msp430-objdump
RANLIB   = msp430-ranlib
STRIP    = msp430-strip
SIZE     = msp430-size
READELF  = msp430-readelf
#MAKETXT  = srec_cat
CP       = cp -p
RM       = rm -f
MV       = mv

#on MSP430 even in DEBUG build -Os is most resonable
CFLAGS   = -mmcu=$(MCU) -Os -std=gnu99 
LDFLAGS  = -mmcu=$(MCU) -mdisable-watchdog

ARCHSOURCES = \
	arch_port.c \
	arch_test.c

