#include <stdint.h>
#include <machine/memmap.h>
#include <machine/machine.h>
#include <system.h>
#include <interrupt.h>
#include <x86_64-ioport.h>
#include <thread.h>
#include <blocklist.h>
#include <charbuffer.h>
#define PORT 0x3f8   /* COM1 */

void uart_init()
{
    x86_64_outb(PORT + 3, 0x80); // set up to load divisor latch
    x86_64_outb(PORT + 0, 2); // lsb
    x86_64_outb(PORT + 1, 0); // msb
    x86_64_outb(PORT + 3, 3); // 8N1
    x86_64_outb(PORT + 2, 0x07); // enable FIFO, clear, 14-byte threshold
	x86_64_outb(PORT + 1, 0x01);
}
static struct charbuffer buffer;

static int serial_received()
{
	return x86_64_inb(PORT + 5) & 1;
}

static void uart_interrupt(int flags)
{
	(void)flags;
	uint32_t isr = x86_64_inb(PORT + 2);
	if(isr & (1 << 2)) {
		while(serial_received()) {
			char d = x86_64_inb(PORT);
			charbuffer_write(&buffer, &d, 1, CHARBUFFER_DO_NONBLOCK);
		}
	}
}

__initializer static void uart_init_second(void)
{
	int a = 0x24;
	int b = 0x23;

	charbuffer_create(&buffer, 0x1000);

	interrupt_register(a, &uart_interrupt);
	interrupt_register(b, &uart_interrupt);
	arch_interrupt_unmask(a);
	arch_interrupt_unmask(b);
	while(serial_received()) {
		char d = x86_64_inb(PORT);
		charbuffer_write(&buffer, &d, 1, CHARBUFFER_DO_NONBLOCK);
	}
}

static int is_transmit_empty()
{
	return x86_64_inb(PORT + 5) & 0x20;
}

void uart_putc(unsigned char byte)
{
	while (is_transmit_empty() == 0);
	x86_64_outb(PORT, byte);
}

unsigned char uart_getc()
{
	char c;
	charbuffer_read(&buffer, &c, 1, 0);
	return c;
}

