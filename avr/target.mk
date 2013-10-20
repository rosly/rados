#there are two defines which influence on code genration
#
#MCU_OPSET is abreviation from Operation Code Set, it is passed to gcc as an
#-mmcu parameter, by changing this parameter you force gcc to generate diferent
#opcodes. Chosing wrong opcode set will result in strange behavior, or even if
#your CPU will seems to do right thing it may crash at random operations
#
#MCU_TYPE is switch used for definition of __AVR_X__ define for preprocesor. By
#changing this define you influence on register address map used by standard
#library and your code. Chosing wrong seting here may result in changing wrong
#registers in memory map
#
#at the bottom of this file there is complementary comment with settings which
#should be chosen for two mentioned settings

#MCU_OPSET = avr4
#MCU_TYPE  = ATmega8
#MCU_OPSET = avr5
#MCU_TYPE  = ATmega16A

MCU_OPSET = avr51
MCU_TYPE  = ATmega1284P

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

CFLAGS   = -mmcu=$(MCU_OPSET) -D__AVR_$(MCU_TYPE)__ # -std=gnu99 commented out cause __flash extension
ifeq ($(DEBUG),)
CFLAGS   += -Os
else
#for debug buld we use -O0 do not obstruct the generated code
CFLAGS   += -Os
endif
LDFLAGS  =

ARCHSOURCES = \
	arch_port.c \
	arch_test.c

# taken from http://www.nongnu.org/avr-libc/user-manual/using_tools.html
#avr1    at90s1200       __AVR_AT90S1200__
#avr1    attiny11        __AVR_ATtiny11__
#avr1    attiny12        __AVR_ATtiny12__
#avr1    attiny15        __AVR_ATtiny15__
#avr1    attiny28        __AVR_ATtiny28__
#
#avr2    at90s2313       __AVR_AT90S2313__
#avr2    at90s2323       __AVR_AT90S2323__
#avr2    at90s2333       __AVR_AT90S2333__
#avr2    at90s2343       __AVR_AT90S2343__
#avr2    attiny22        __AVR_ATtiny22__
#avr2    attiny26        __AVR_ATtiny26__
#avr2    at90s4414       __AVR_AT90S4414__
#avr2    at90s4433       __AVR_AT90S4433__
#avr2    at90s4434       __AVR_AT90S4434__
#avr2    at90s8515       __AVR_AT90S8515__
#avr2    at90c8534       __AVR_AT90C8534__
#avr2    at90s8535       __AVR_AT90S8535__
#
#avr2/avr25 [1]  at86rf401       __AVR_AT86RF401__
#avr2/avr25 [1]  ata6289 __AVR_ATA6289__
#avr2/avr25 [1]  attiny13        __AVR_ATtiny13__
#avr2/avr25 [1]  attiny13a       __AVR_ATtiny13A__
#avr2/avr25 [1]  attiny2313      __AVR_ATtiny2313__
#avr2/avr25 [1]  attiny2313a     __AVR_ATtiny2313A__
#avr2/avr25 [1]  attiny24        __AVR_ATtiny24__
#avr2/avr25 [1]  attiny24a       __AVR_ATtiny24A__
#avr2/avr25 [1]  attiny25        __AVR_ATtiny25__
#avr2/avr25 [1]  attiny261       __AVR_ATtiny261__
#avr2/avr25 [1]  attiny261a      __AVR_ATtiny261A__
#avr2/avr25 [1]  attiny4313      __AVR_ATtiny4313__
#avr2/avr25 [1]  attiny43u       __AVR_ATtiny43U__
#avr2/avr25 [1]  attiny44        __AVR_ATtiny44__
#avr2/avr25 [1]  attiny44a       __AVR_ATtiny44A__
#avr2/avr25 [1]  attiny45        __AVR_ATtiny45__
#avr2/avr25 [1]  attiny461       __AVR_ATtiny461__
#avr2/avr25 [1]  attiny461a      __AVR_ATtiny461A__
#avr2/avr25 [1]  attiny48        __AVR_ATtiny48__
#avr2/avr25 [1]  attiny84        __AVR_ATtiny84__
#avr2/avr25 [1]  attiny84a       __AVR_ATtiny84A__
#avr2/avr25 [1]  attiny85        __AVR_ATtiny85__
#avr2/avr25 [1]  attiny861       __AVR_ATtiny861__
#avr2/avr25 [1]  attiny861a      __AVR_ATtiny861A__
#avr2/avr25 [1]  attiny87        __AVR_ATtiny87__
#avr2/avr25 [1]  attiny88        __AVR_ATtiny88__
#
#avr3    atmega603       __AVR_ATmega603__
#avr3    at43usb355      __AVR_AT43USB355__
#
#avr3/avr31 [3]  atmega103       __AVR_ATmega103__
#avr3/avr31 [3]  at43usb320      __AVR_AT43USB320__
#
#avr3/avr35 [2]  at90usb82       __AVR_AT90USB82__
#avr3/avr35 [2]  at90usb162      __AVR_AT90USB162__
#avr3/avr35 [2]  atmega8u2       __AVR_ATmega8U2__
#avr3/avr35 [2]  atmega16u2      __AVR_ATmega16U2__
#avr3/avr35 [2]  atmega32u2      __AVR_ATmega32U2__
#avr3/avr35 [2]  attiny167       __AVR_ATtiny167__
#
#avr3    at76c711        __AVR_AT76C711__
#avr4    atmega48        __AVR_ATmega48__
#avr4    atmega48a       __AVR_ATmega48A__
#avr4    atmega48p       __AVR_ATmega48P__
#avr4    atmega8 __AVR_ATmega8__
#avr4    atmega8515      __AVR_ATmega8515__
#avr4    atmega8535      __AVR_ATmega8535__
#avr4    atmega88        __AVR_ATmega88__
#avr4    atmega88a       __AVR_ATmega88A__
#avr4    atmega88p       __AVR_ATmega88P__
#avr4    atmega88pa      __AVR_ATmega88PA__
#avr4    atmega8hva      __AVR_ATmega8HVA__
#avr4    at90pwm1        __AVR_AT90PWM1__
#avr4    at90pwm2        __AVR_AT90PWM2__
#avr4    at90pwm2b       __AVR_AT90PWM2B__
#avr4    at90pwm3        __AVR_AT90PWM3__
#avr4    at90pwm3b       __AVR_AT90PWM3B__
#avr4    at90pwm81       __AVR_AT90PWM81__
#
#avr5    at90can32       __AVR_AT90CAN32__
#avr5    at90can64       __AVR_AT90CAN64__
#avr5    at90pwm216      __AVR_AT90PWM216__
#avr5    at90pwm316      __AVR_AT90PWM316__
#avr5    at90scr100      __AVR_AT90SCR100__
#avr5    at90usb646      __AVR_AT90USB646__
#avr5    at90usb647      __AVR_AT90USB647__
#avr5    at94k   __AVR_AT94K__
#avr5    atmega16        __AVR_ATmega16__
#avr5    atmega161       __AVR_ATmega161__
#avr5    atmega162       __AVR_ATmega162__
#avr5    atmega163       __AVR_ATmega163__
#avr5    atmega164a      __AVR_ATmega164A__
#avr5    atmega164p      __AVR_ATmega164P__
#avr5    atmega165       __AVR_ATmega165__
#avr5    atmega165a      __AVR_ATmega165A__
#avr5    atmega165p      __AVR_ATmega165P__
#avr5    atmega168       __AVR_ATmega168__
#avr5    atmega168a      __AVR_ATmega168A__
#avr5    atmega168p      __AVR_ATmega168P__
#avr5    atmega169       __AVR_ATmega169__
#avr5    atmega169a      __AVR_ATmega169A__
#avr5    atmega169p      __AVR_ATmega169P__
#avr5    atmega169pa     __AVR_ATmega169PA__
#avr5    atmega16a       __AVR_ATmega16A__
#avr5    atmega16hva     __AVR_ATmega16HVA__
#avr5    atmega16hva2    __AVR_ATmega16HVA2__
#avr5    atmega16hvb     __AVR_ATmega16HVB__
#avr5    atmega16hvbrevb __AVR_ATmega16HVBREVB__
#avr5    atmega16m1      __AVR_ATmega16M1__
#avr5    atmega16u4      __AVR_ATmega16U4__
#avr5    atmega32        __AVR_ATmega32__
#avr5    atmega323       __AVR_ATmega323__
#avr5    atmega324a      __AVR_ATmega324A__
#avr5    atmega324p      __AVR_ATmega324P__
#avr5    atmega324pa     __AVR_ATmega324PA__
#avr5    atmega325       __AVR_ATmega325__
#avr5    atmega325a      __AVR_ATmega325A__
#avr5    atmega325p      __AVR_ATmega325P__
#avr5    atmega3250      __AVR_ATmega3250__
#avr5    atmega3250a     __AVR_ATmega3250A__
#avr5    atmega3250p     __AVR_ATmega3250P__
#avr5    atmega328       __AVR_ATmega328__
#avr5    atmega328p      __AVR_ATmega328P__
#avr5    atmega329       __AVR_ATmega329__
#avr5    atmega329a      __AVR_ATmega329A__
#avr5    atmega329p      __AVR_ATmega329P__
#avr5    atmega329pa     __AVR_ATmega329PA__
#avr5    atmega3290      __AVR_ATmega3290__
#avr5    atmega3290a     __AVR_ATmega3290A__
#avr5    atmega3290p     __AVR_ATmega3290P__
#avr5    atmega32c1      __AVR_ATmega32C1__
#avr5    atmega32hvb     __AVR_ATmega32HVB__
#avr5    atmega32hvbrevb __AVR_ATmega32HVBREVB__
#avr5    atmega32m1      __AVR_ATmega32M1__
#avr5    atmega32u4      __AVR_ATmega32U4__
#avr5    atmega32u6      __AVR_ATmega32U6__
#avr5    atmega406       __AVR_ATmega406__
#avr5    atmega64        __AVR_ATmega64__
#avr5    atmega640       __AVR_ATmega640__
#avr5    atmega644       __AVR_ATmega644__
#avr5    atmega644a      __AVR_ATmega644A__
#avr5    atmega644p      __AVR_ATmega644P__
#avr5    atmega644pa     __AVR_ATmega644PA__
#avr5    atmega645       __AVR_ATmega645__
#avr5    atmega645a      __AVR_ATmega645A__
#avr5    atmega645p      __AVR_ATmega645P__
#avr5    atmega6450      __AVR_ATmega6450__
#avr5    atmega6450a     __AVR_ATmega6450A__
#avr5    atmega6450p     __AVR_ATmega6450P__
#avr5    atmega649       __AVR_ATmega649__
#avr5    atmega649a      __AVR_ATmega649A__
#avr5    atmega6490      __AVR_ATmega6490__
#avr5    atmega6490a     __AVR_ATmega6490A__
#avr5    atmega6490p     __AVR_ATmega6490P__
#avr5    atmega649p      __AVR_ATmega649P__
#avr5    atmega64c1      __AVR_ATmega64C1__
#avr5    atmega64hve     __AVR_ATmega64HVE__
#avr5    atmega64m1      __AVR_ATmega64M1__
#avr5    m3000   __AVR_M3000__
#
#avr5/avr51 [3]  at90can128      __AVR_AT90CAN128__
#avr5/avr51 [3]  at90usb1286     __AVR_AT90USB1286__
#avr5/avr51 [3]  at90usb1287     __AVR_AT90USB1287__
#avr5/avr51 [3]  atmega128       __AVR_ATmega128__
#avr5/avr51 [3]  atmega1280      __AVR_ATmega1280__
#avr5/avr51 [3]  atmega1281      __AVR_ATmega1281__
#avr5/avr51 [3]  atmega1284p     __AVR_ATmega1284P__
#
#avr6    atmega2560      __AVR_ATmega2560__
#avr6    atmega2561      __AVR_ATmega2561__
#
#avrxmega2       atxmega16a4     __AVR_ATxmega16A4__
#avrxmega2       atxmega16d4     __AVR_ATxmega16D4__
#avrxmega2       atxmega32a4     __AVR_ATxmega32A4__
#avrxmega2       atxmega32d4     __AVR_ATxmega32D4__
#
#avrxmega4       atxmega64a3     __AVR_ATxmega64A3__
#avrxmega4       atxmega64d3     __AVR_ATxmega64D3__
#
#avrxmega5       atxmega64a1     __AVR_ATxmega64A1__
#avrxmega5       atxmega64a1u    __AVR_ATxmega64A1U__
#
#avrxmega6       atxmega128a3    __AVR_ATxmega128A3__
#avrxmega6       atxmega128d3    __AVR_ATxmega128D3__
#avrxmega6       atxmega192a3    __AVR_ATxmega192A3__
#avrxmega6       atxmega192d3    __AVR_ATxmega192D3__
#avrxmega6       atxmega256a3    __AVR_ATxmega256A3__
#avrxmega6       atxmega256a3b   __AVR_ATxmega256A3B__
#avrxmega6       atxmega256d3    __AVR_ATxmega256D3__
#
#avrxmega7       atxmega128a1    __AVR_ATxmega128A1__
#avrxmega7       atxmega128a1u   __AVR_ATxmega128A1U__
#
#avrtiny10       attiny4 __AVR_ATtiny4__
#avrtiny10       attiny5 __AVR_ATtiny5__
#avrtiny10       attiny9 __AVR_ATtiny9__
#avrtiny10       attiny10        __AVR_ATtiny10__
#avrtiny10       attiny20        __AVR_ATtiny20__
#avrtiny10       attiny40        __AVR_ATtiny40__

