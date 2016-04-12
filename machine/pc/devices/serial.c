#include <machine/memmap.h>

extern void uart_putc(char);
extern unsigned char uart_getc(void);
void serial_putc (char c)
{
	uart_putc(c);
	if (c == '\n')
	{
	    serial_putc('\r');
	}
}

void serial_puts(const char * str)
{
	while (*str) serial_putc (*str++);
}

unsigned char serial_getc(void)
{
	return uart_getc();
}

