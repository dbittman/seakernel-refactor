#include <stdint.h>
#include <printk.h>
#include <interrupt.h>
#include <mmu.h>
#include <panic.h>
#include <thread.h>
#include <syscall.h>
void x86_64_signal_eoi(void);
struct __attribute__((packed)) exception_frame
{
	uint64_t r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8, rax, rcx, rdx, rsi, rdi;
	uint64_t int_no, err_code;
	uint64_t rip, cs, rflags, userrsp, ss;
};

void x86_64_change_fpusse_allow(bool enable);
static void __fault(struct exception_frame *frame)
{
	if(frame->int_no == 6 && current_thread && !current_thread->arch.usedfpu) {
		/* we're emulating FPU instructions / disallowing SSE. Set a flag,
		 * and allow the thread to do its thing */
		current_thread->arch.usedfpu = true;
		x86_64_change_fpusse_allow(true);
		asm volatile ("finit"); /* also, we may need to clear the FPU state */
		return;
	} else if(frame->int_no == 14) {
		/* page-fault */
		uint64_t cr2;
		asm volatile("mov %%cr2, %0" : "=r"(cr2));
		int flags = FAULT_ERROR_PRES;
		if(frame->err_code & 1) {
			flags |= FAULT_ERROR_PERM;
		}
		if(frame->err_code & (1 << 1)) {
			flags |= FAULT_WRITE;
		}
		if(frame->err_code & (1 << 2)) {
			flags |= FAULT_USER;
		}
		if(frame->err_code & (1 << 4)) {
			flags |= FAULT_EXEC;
		}
		printk("PF %lx\n", frame->rip);
		mm_fault_entry(cr2, flags);
	} else {
		if(frame->cs == 0x8) {
			panic(0, "kernel exception %ld: %lx err=%lx",
					frame->int_no, frame->rip, frame->err_code);
		} else {
			panic(0, "usermode exception %ld: %lx err=%lx",
					frame->int_no, frame->rip, frame->err_code);
		}
	}
}

void x86_64_exception_entry(struct exception_frame *frame)
{
	x86_64_signal_eoi();
	if(frame->int_no < 32) {
		__fault(frame);
	} else {
		interrupt_entry(frame->int_no, frame->cs == 0x8 ? INTERRUPT_INKERNEL : 0);
	}
}

void x86_64_syscall_entry(struct exception_frame *frame)
{
	frame->rax = syscall_entry(frame->rax, frame->rdi, frame->rsi, frame->rdx, frame->rcx, frame->r8);
}

