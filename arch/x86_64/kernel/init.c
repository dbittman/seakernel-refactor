#include <stdint.h>
#include <printk.h>
#include <panic.h>
#include <mmu.h>
#include <processor.h>
extern void main(void);
extern void uart_init(void);
extern void idt_init(void);
void pc_save_multiboot(void *header);

static void init_proc(void)
{
	uint64_t cr0, cr4;
	asm volatile("finit");
	asm volatile("mov %%cr0, %0" : "=r"(cr0));
	cr0 |= (1 << 2);
	cr0 |= (1 << 5);
	cr0 |= (1 << 1);
	cr0 &= ~(1 << 30); // make sure caching is on
	asm volatile("mov %0, %%cr0" :: "r"(cr0));
	
	
	asm volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= (1 << 7); //enable page global
	cr4 |= (1 << 10); //enable fast fxsave etc, sse
	/* TODO: bit 16 of CR4 enables wrfsbase etc instructions.
	 * But we need to check CPUID before using them. */
	//cr4 |= (1 << 16); //enable wrfsbase etc
	asm volatile("mov %0, %%cr4" :: "r"(cr4));
}

void x86_64_cpu_primary_entry(void *mth, uint32_t magic)
{
	uart_init();
	init_proc();
	if(magic != 0x2BADB002) {
		panic(0, "multiboot magic number not present");
	}
	pc_save_multiboot(mth);
	idt_init();
	main();
}

void x86_64_lapic_init_percpu(void);
void idt_init_secondary(void);
void x86_64_cpu_secondary_entry(uint32_t id)
{
	arch_mm_context_switch(&kernel_context);
	idt_init_secondary();
	init_proc();
	x86_64_lapic_init_percpu();
	assert((int)id == arch_processor_current_id());
	processor_secondary_main();
}

