#include <processor.h>
#include <interrupt.h>
#include <system.h>
int arch_processor_current_id(void)
{
	uint64_t id;
	asm volatile("mrs %0, MPIDR_EL1" : "=r"(id));
	return (id & 0xFFFFFF) | (id & 0xFF00000000) >> 8;
}

__attribute__((no_instrument_function))
uint64_t arch_processor_get_cycle_count(void)
{
	uint64_t c;
	asm volatile("mrs %0, PMCCNTR_EL0" : "=r"(c));
	return c;
}

__initializer static void aarch64_init_cycle_counter(void)
{
	long x = 1 << 31;
	asm volatile("msr pmcntenset_el0, %0" :: "r"(x));
	x = 1;
	asm volatile("msr pmcr_el0, %0" :: "r"(x));
}

void arch_processor_reset(void)
{
	/* TODO */
}

void arch_panic_begin(void)
{
}

