extern void *table0;
#include <printk.h>
#include <stdint.h>
#include <libfdt.h>
#include <processor.h>
#include <machine/gic.h>
#include <arch-psci.h>
#include <arch-timer.h>
#include <thread-bits.h>
_Alignas(0x10000) unsigned long long kernel_translation_table[64];
_Alignas(0x10000) unsigned long long table_l2[8192];
_Alignas(0x10000) unsigned long long table_l2_dev[8192];


void init_exceptions(void)
{
	asm volatile("ldr x0, =table0; msr VBAR_EL1, x0");
	
}

extern void _start_secondary(void);
void arch_processor_poke_secondary(int id, uintptr_t init_stack)
{
	psci_hvc_cmd3(0xC4000003, id, (uintptr_t)(_start_secondary - (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE)), init_stack + KERNEL_STACK_SIZE);
}

extern void main(void);
extern void uart_init(void);
void aarch64_cpu_primary_entry(void)
{
	init_exceptions();
	uart_init();
	gic_init();
	gic_init_percpu();
	main();
}

void aarch64_cpu_secondary_entry(unsigned long mpidr)
{
	printk("Seconary CPU %lx!\n", mpidr);
	init_exceptions();
	gic_init_percpu();
	processor_secondary_main();
}

