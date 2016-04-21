#include <printk.h>
#include <stdbool.h>
#include <stdint.h>
#include <panic.h>
#include <interrupt.h>
#include <machine/gic.h>
#include <syscall.h>
void aarch64_mm_pagefault(uintptr_t addr, int re, bool wr, bool ex, bool us);

static void __fault(unsigned long esr, bool el0, struct exception_frame *frame)
{
	int ec = (esr >> 26) & 0x3F;
	bool exec = false;
	uint64_t addr;
	switch(ec) {
		case 0x20: case 0x21:
			exec = true;
			/* fall through */
		case 0x24: case 0x25:
			asm volatile("mrs %0, FAR_EL1" : "=r"(addr));
			bool write = !!(esr & (1 << 6));
			int reason = esr & 0x1F;
			aarch64_mm_pagefault(addr, reason, write, exec, el0);
			break;
		case 0x15:
			if((esr & 0xFFFF) == SYS_FORK) {
				frame->x0 = frame->elr;
				frame->x1 = frame->spsr;
			}
			frame->x0 = syscall_entry(esr & 0xFFFF, frame->x0, frame->x1, frame->x2, frame->x3, frame->x4);
			break;
		default:
			panic(0, "unknown exception");
	}
}

void arm_entry_l64_sync(unsigned long esr, struct exception_frame *frame)
{
	printk("SYNC: %lx\n", esr);
	__fault(esr, true, frame);
}

void arm_entry_l64_serror(unsigned long esr)
{
	printk("SERR: %lx\n", esr);
}

void arm_entry_l64_irq(unsigned long esr)
{
	(void)esr;
	uint32_t iar = gic_read_interrupt_number();
	int v = iar & 0x3FF;
	gic_signal_eoi(iar);
	interrupt_entry(v, 0);
}

void arm_entry_l64_fiq(unsigned long esr)
{
	(void)esr;
	uint32_t iar = gic_read_interrupt_number();
	int v = iar & 0x3FF;
	gic_signal_eoi(iar);
	interrupt_entry(v, 0);
}

void arm_entry_spx_sync(unsigned long esr, struct exception_frame *frame)
{
	uint64_t elr;
	asm volatile("mrs %0, ELR_EL1" : "=r"(elr));
	printk("S_SYNC: %lx, %lx\n", esr, elr);
	__fault(esr, false, frame);
}

void arm_entry_spx_serror(unsigned long esr)
{
	printk("S_SERR: %lx\n", esr);
}

void arm_entry_spx_irq(unsigned long esr)
{
	(void)esr;
	uint64_t elr;
	asm volatile("mrs %0, ELR_EL1" : "=r"(elr));
	uint32_t iar = gic_read_interrupt_number();
	int v = iar & 0x3FF;
	gic_signal_eoi(iar);
	interrupt_entry(v, INTERRUPT_INKERNEL);
}

void arm_entry_spx_fiq(unsigned long esr)
{
	(void)esr;
	uint32_t iar = gic_read_interrupt_number();
	int v = iar & 0x3FF;
	gic_signal_eoi(iar);
	interrupt_entry(v, INTERRUPT_INKERNEL);
}

void arm_entry_unknown(unsigned long esr)
{
	printk("UNKNOWN: %lx\n", esr);
}


