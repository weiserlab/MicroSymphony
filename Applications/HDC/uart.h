#ifndef UART_H_
#define UART_H_

#include <stdint.h>

void uart0_init(void);
static void uart0_send(char c);
void uart0_print(const char *s);
void uart0_println(const char *s);
void uart0_print_uint(uint32_t num);
void uart0_print_hex(uint32_t num);
void uart0_print_float(float f, uint8_t decimals);

#endif /* UART_H_ */
