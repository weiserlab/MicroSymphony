#include <msp430.h>
#include <stdint.h>
#include "uart.h"


void uart0_init(void) // baud rate = 19200 with 8MHz SMCLK
{
    UCA0CTLW0 = UCSWRST;
    UCA0CTLW0 |= UCSSEL__SMCLK;

    UCA0BR0 = 160u;
    UCA0BR1 = 1u;
    UCA0MCTLW = (uint16_t)(0xAAu << 8);

    P2SEL1 |= (BIT0 | BIT1);
    P2SEL0 &= (uint8_t)~(BIT0 | BIT1);

    UCA0CTLW0 &= ~UCSWRST;
}

static void uart0_send(char c) {
    while (!(UCA0IFG & UCTXIFG));
    UCA0TXBUF = c;
}

void uart0_print(const char *s) {
    while (*s) uart0_send(*s++);
}

void uart0_println(const char *s) {
    uart0_print(s);
    uart0_send('\r');
    uart0_send('\n');
}

void uart0_print_uint(uint32_t num) {
    char buf[16];
    uint8_t i = 0;
    if (num == 0) {
        uart0_send('0');
        return;
    }
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    while (i--) uart0_send(buf[i]);
}

void uart0_print_hex(uint32_t num) {
    char buf[9];
    int i;
    buf[8] = '\0';
    for (i = 7; i >= 0; i--) {
        uint8_t nibble = num & 0x0F;
        if (nibble < 10) buf[i] = '0' + nibble;
        else buf[i] = 'A' + (nibble - 10);
        num >>= 4;
    }
    uart0_print(buf);
}

void uart0_print_float(float f, uint8_t decimals) {
    if (f < 0) {
        uart0_send('-');
        f = -f;
    }
    uint32_t int_part = (uint32_t)f;
    uart0_print_uint(int_part);
    uart0_send('.');
    float frac_part = f - (float)int_part;
    while (decimals--) {
        frac_part *= 10.0f;
        uint8_t digit = (uint8_t)frac_part;
        uart0_send('0' + digit);
        frac_part -= (float)digit;
    }
}
