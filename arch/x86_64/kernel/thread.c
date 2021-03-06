#include <thread.h>
#include <printk.h>
#include <errno.h>
#include <process.h>
#include <x86_64-msr.h>
#include <processor.h>
extern void x86_64_do_context_switch(void *oldsp, void *newsp);

/* okay, so. We need to save and restore FPU+SSE registers during
 * a context switch. But we want to try to avoid doing it if it isn't
 * needed (if a thread hasn't done FPU stuff, etc).
 *
 * Fortunately, there's a solution. We disable FPU and SSE stuff by default
 * for threads, and wait for them to generate a #UD exception. When this
 * happens, we can then set a flag saying that the thread needed this
 * stuff, and then enable them before returning from the exception.
 *
 * This means that we do a little bit of work during a context switch (messing
 * with cr0 and cr4 to control the enable and disable bits of these features),
 * but the benefit is that we only need to do fxsave/fxrstor for threads
 * that need it. */

void x86_64_change_fpusse_allow(bool enable)
{
	uint64_t tmp;
	asm volatile("mov %%cr0, %0" : "=r"(tmp));
	if(enable)
		tmp &= ~(1 << 2);
	else
		tmp |= (1 << 2);
	asm volatile("mov %0, %%cr0" :: "r"(tmp));
	asm volatile("mov %%cr4, %0" : "=r"(tmp));
	if(enable)
		tmp |= (1 << 9);
	else
		tmp &= ~(1 << 9);
	asm volatile("mov %0, %%cr4" :: "r"(tmp));
}

void x86_64_tss_ctxswitch(struct processor *proc, uintptr_t stacktop);
void arch_thread_context_switch(struct thread *old, struct thread *next)
{
	if(old->ctx != next->ctx)
		arch_mm_context_switch(next->ctx);
	if(old->arch.usedfpu) {
		/* store FPU sate */
		asm volatile ("fxsave (%0)" 
				:: "r" (old->arch.fpu_data) : "memory");
	}
	x86_64_change_fpusse_allow(next->arch.usedfpu);
	if(next->arch.usedfpu) {
		/* restore FPU state */
		asm volatile ("fxrstor (%0)" 
				:: "r" (next->arch.fpu_data) : "memory");

	}
	x86_64_tss_ctxswitch(next->processor, (uintptr_t)next->kernel_tls_base + KERNEL_STACK_SIZE);
	x86_64_wrmsr(X86_MSR_FS_BASE, next->arch.fs & 0xFFFFFFFF, (next->arch.fs >> 32) & 0xFFFFFFFF);
	x86_64_do_context_switch(&old->stackpointer, &next->stackpointer);
}

extern void x86_64_do_thread_create(void *stackbase, void **stackpointer, struct thread *,
		void *fn, void *arg);
void arch_thread_create(struct thread *next, uintptr_t entry, void *arg)
{
	x86_64_do_thread_create((void *)next->kernel_tls_base, &next->stackpointer,
			next, (void *)entry, arg);
}

void x86_64_tss_init(struct processor *proc);
void x86_64_gdt_init(struct processor *proc);
void arch_thread_init(struct thread *us)
{
	/* also use this to initialize the TSS for each CPU */
	x86_64_gdt_init(us->processor);
	x86_64_tss_init(us->processor);
	us->arch.usedfpu = false;
	*(struct thread **)(us->kernel_tls_base) = us;
}

void arch_thread_usermode_jump(uintptr_t entry, uintptr_t initial_stack)
{
	long trap = 0;
	if(current_thread->tid == 14)
		trap = 1 << 8;
	
	trap = 0;
	asm volatile("cli;"
			"movq $0, %%rbp;"
			"mov $0x23, %%ax;"
			"mov %%ax, %%ds;"
			"mov %%ax, %%es;"
			"pushq $0x23;"
			"pushq %1;"
			"pushfq;"
			"popq %%rax;"
			"orq $0x200, %%rax;"
			"orq %2, %%rax;"
			"pushq %%rax;"
			"pushq $0x1b;"
			"pushq %0;"
			"movq $0x0, %%rax;" /* for fork() */
			"iretq"
			:: "r"(entry),
			   "r"(initial_stack), "r"(trap)
			   : "rax", "memory");
}

long sys_arch_prctl(int code, unsigned long addr)
{
	switch(code) {
		case 0x1002:
			if(addr >= USER_REGION_END || addr < USER_REGION_START)
				return -EFAULT;
			current_thread->arch.fs = addr;
			x86_64_wrmsr(X86_MSR_FS_BASE, addr & 0xFFFFFFFF, (addr >> 32) & 0xFFFFFFFF);
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

