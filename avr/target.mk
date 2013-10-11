MCU_OPSET = avr4
MCU_TYPE  = ATmega8

CC       = avr-gcc
LD       = avr-ld
AR       = avr-ar
AS       = avr-gcc
GASP     = avr-gasp
NM       = avr-nm
OBJCOPY  = avr-objcopy
OBJDUMP  = avr-objdump
RANLIB   = avr-ranlib
STRIP    = avr-strip
SIZE     = avr-size
READELF  = avr-readelf
#MAKETXT  = srec_cat
CP       = cp -p
RM       = rm -f
MV       = mv

CFLAGS   = -mmcu=$(MCU_OPSET) -D__AVR_$(MCU_TYPE)__ -std=gnu99 
ifeq ($(DEBUG),)
#for debug buld we use -O0 do not obstruct the generated code
CFLAGS   += -O0
else
CFLAGS   += -O2
endif
LDFLAGS  =

ARCHSOURCES = \
	arch_port.c \
	arch_test.c

