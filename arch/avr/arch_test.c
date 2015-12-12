/*
 * This file is a part of RadOs project
 * Copyright (c) 2013, Radoslaw Biernacki <radoslaw.biernacki@gmail.com>
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

#include "os_private.h"
#include "os_test.h"

#include <stdarg.h>
#include <stdio.h>
#include <avr/io.h>
#include <util/delay.h>

#define BLINK_DELAY_MS 500

static test_tick_clbck_t test_tick_clbck = NULL;
static int result_store;

static void uart_init(void)
{
#define BAUD 115200
#define BAUD_TOL 5
#include <util/setbaud.h>
   UBRR0H = UBRRH_VALUE;
   UBRR0L = UBRRL_VALUE;
#if USE_2X
   UCSR0A |= (1 << U2X0);
#else
   UCSR0A &= ~(1 << U2X0);
#endif

   UCSR0C |= (1 << UCSZ00) | (1 << UCSZ01); // Use 8-bit character sizes
   UCSR0B |= (1 << RXEN0) | (1 << TXEN0);   // Turn on the transmission and reception circuitry
   //for IRQ (1 << OS_CONCAT(RXCIE, MAC_USART_NBR)) | (1 << OS_CONCAT(TXCIE, MAC_USART_NBR));
}

#if 0
static void uart_tx_progmem(const OS_PROGMEM char* str)
{
   unsigned i;

   for (i = 0; pgm_read_byte_near(&str[i]) != '\0'; i++)
   {
      while(!(UCSR0A & (1<<UDRE0)));
      UDR0 = pgm_read_byte_near(&str[i]);
   }
}
#endif

static void uart_tx_rammem(const char* str)
{
   unsigned i = 0;
   char val;

   while ((val = str[i]) != '\0')
   {
      while(!(UCSR0A & (1<<UDRE0)));
      UDR0 = val;
      i++;
   }
}

/* for documentation check os_test.h */
void test_debug_printf(const OS_PROGMEM char* format, ...)
{
   va_list vargs;
   char buff[OS_STACK_MINSIZE / 2]; /* half of stack size */

   va_start(vargs, format);
   /* use +_P version of vsnpintf since format is from program memory */
   vsnprintf_P(buff, sizeof(buff) - 1, format, vargs);
   buff[sizeof(buff) - 1] = '\0';
   va_end(vargs);

   uart_tx_rammem(buff);
}

/* for documentation check os_test.h */
#define TEST_BLINK
void test_result(int result)
{
   result_store = result;
   unsigned i = 0;

   if(0 == result) {
      test_debug("Test PASSED");
   } else {
      test_debug("Test FAILURE");
   }

#ifndef TEST_BLINK
   arch_halt();
#else
   /* instead of arch_halt() blik the led */
   while (1) {

    PORTB |= _BV(PORTB5);
    _delay_ms(1000);
    PORTB &= ~_BV(PORTB5);
    _delay_ms(1000);

    test_debug("Result loop %u", i++);
   }
#endif
}

/* for documentation check os_test.h */
void test_setupmain(const OS_PROGMEM char* test_name)
{
#ifdef TEST_BLINK
   /* For Arduino set portB as output */
   DDRB |= _BV(DDB5);
   /* switch off the test LED */
   PORTB &= ~_BV(PORTB5);
#endif

   uart_init();

   /* below we use %S (capital) since test_name param is OS_PROGMEM */
   test_debug("Starting test: %S", test_name);
}

/* for documentation check os_test.h */
void test_setuptick(test_tick_clbck_t clbck, unsigned long nsec)
{
   test_tick_clbck = clbck;

   /* Set timer 1 compare value for configured system tick with a prescaler of 256 */
   OCR1A = F_CPU / 256ul * nsec / 1000000000;

   /* Set prescaler 256 */
   TCCR1B = _BV(CS12) | _BV(WGM12);

   /* Enable compare match 1A interrupt */
#ifdef TIMSK
   TIMSK = _BV(OCIE1A);
#else
   TIMSK1 = _BV(OCIE1A);
#endif
}

/* for documentation check os_test.h */
void test_reqtick(void)
{
   test_assert(!"Missing implementation!");
}

void OS_ISR TIMER1_COMPA_vect(void)
{
   arch_contextstore_i(tick);

   /* we do not allowing or nested interrupts in this ISR, therefore we do not
    * have to enter the critical section to call os_tick() */
   os_tick();
   if (test_tick_clbck)
   {
      test_tick_clbck();
   }

   arch_contextrestore_i(tick);
}

