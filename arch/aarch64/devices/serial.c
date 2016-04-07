#include <machine/memmap.h>

#define SERIAL_BASE (UART(UART_SERIAL) + PHYS_MAP_START)
#define SERIAL_FLAG_REGISTER 0x18
#define SERIAL_BUFFER_FULL (1 << 5)

extern void uart_putc(char);
extern unsigned char uart_getc(void);
static void serial_putc (char c)
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

