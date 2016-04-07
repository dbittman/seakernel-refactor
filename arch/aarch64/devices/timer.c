#include <machine/gic.h>
#include <machine/machine.h>
int arch_timer_init(void)
{
	asm volatile ("mov x0, #0; msr cntp_tval_el0, x0" ::: "x0");
	asm volatile ("mov x0, #1; msr cntp_ctl_el0, x0" ::: "x0");
	return TIMER_INTERRUPT;
}

void arch_timer_tick(void)
{
	asm volatile ("mov x0, #0; msr cntp_ctl_el0, x0" ::: "x0");
	asm volatile ("mov x0, %0; msr cntp_tval_el0, x0" ::"r"(100ul): "x0");
	asm volatile ("mov x0, #1; msr cntp_ctl_el0, x0" ::: "x0");
}

