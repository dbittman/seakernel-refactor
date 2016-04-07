#include <stdint.h>
#include <machine/memmap.h>
#include <machine/machine.h>
#include <system.h>
#include <interrupt.h>
static inline void mmio_write(uintptr_t reg, uint32_t data)
{
	*(volatile uint32_t *)reg = data;
}
 
static inline uint32_t mmio_read(uintptr_t reg)
{
	return *(volatile uint32_t *)reg;
}

enum
{
    // The base address for UART.
    UART0_BASE = UART(0) + PHYS_MAP_START,
 
    // The offsets for reach register for the UART.
    UART0_DR     = (UART0_BASE + 0x00),
    UART0_RSRECR = (UART0_BASE + 0x04),
    UART0_FR     = (UART0_BASE + 0x18),
    UART0_ILPR   = (UART0_BASE + 0x20),
    UART0_IBRD   = (UART0_BASE + 0x24),
    UART0_FBRD   = (UART0_BASE + 0x28),
    UART0_LCRH   = (UART0_BASE + 0x2C),
    UART0_CR     = (UART0_BASE + 0x30),
    UART0_IFLS   = (UART0_BASE + 0x34),
    UART0_IMSC   = (UART0_BASE + 0x38),
    UART0_RIS    = (UART0_BASE + 0x3C),
    UART0_MIS    = (UART0_BASE + 0x40),
    UART0_ICR    = (UART0_BASE + 0x44),
    UART0_DMACR  = (UART0_BASE + 0x48),
    UART0_ITCR   = (UART0_BASE + 0x80),
    UART0_ITIP   = (UART0_BASE + 0x84),
    UART0_ITOP   = (UART0_BASE + 0x88),
    UART0_TDR    = (UART0_BASE + 0x8C),
};

void uart_init()
{
	// Disable UART0.
	mmio_write(UART0_CR, 0x00000000);
	// Setup the GPIO pin 14 && 15.
 
	// Clear pending interrupts.
	mmio_write(UART0_ICR, 0x7FF);
 
	// Set integer & fractional part of baud rate.
	// Divider = UART_CLOCK/(16 * Baud)
	// Fraction part register = (Fractional part * 64) + 0.5
	// UART_CLOCK = 3000000; Baud = 115200.
 
	// Divider = 3000000 / (16 * 115200) = 1.627 = ~1.
	// Fractional part register = (.627 * 64) + 0.5 = 40.6 = ~40.
	mmio_write(UART0_IBRD, 1);
	mmio_write(UART0_FBRD, 40);
 
	// Enable FIFO & 8 bit data transmissio (1 stop bit, no parity).
	mmio_write(UART0_LCRH, (1 << 4) | (1 << 5) | (1 << 6));
 
	// Mask all interrupts.
	mmio_write(UART0_IMSC, (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6) |
	                       (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10));
 	
 	mmio_write(UART0_IFLS, 0);

	// Enable UART0, receive & transfer part of UART.
	mmio_write(UART0_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

#include <printk.h>

static char readbuf[128];
static _Atomic int rbp=0;

static void uart_interrupt(int flags)
{
	(void)flags;
	uint32_t isr = mmio_read(UART0_RIS);
	printk("UART INT %x\n", isr);
	if(isr & (1 << 4)) {
		while(!(mmio_read(UART0_FR) & (1 << 4))) {
			char d = mmio_read(UART0_DR);
			if(rbp < 127) readbuf[rbp++] = d;
		}
	}
}

__initializer static void uart_init_second(void)
{
	printk("UART initialized with interrupts\n");
	mmio_write(UART0_IMSC, (1 << 4));
	interrupt_register(UART_INT(0), uart_interrupt);
	arch_interrupt_unmask(UART_INT(0));
}

void uart_putc(unsigned char byte)
{
	// Wait for UART to become ready to transmit.
	while ( mmio_read(UART0_FR) & (1 << 5) ) { }
	mmio_write(UART0_DR, byte);
}
 
unsigned char uart_getc()
{
	//printk("%x\n", mmio_read(UART0_RIS));
    // Wait for UART to have recieved something.
    //while ( mmio_read(UART0_FR) & (1 << 4) ) { }
    //return mmio_read(UART0_DR);
    while(rbp == 0);
    return readbuf[--rbp];
}

