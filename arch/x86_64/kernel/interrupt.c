#include <stdint.h>
#include <printk.h>
#include <interrupt.h>
#include <mmu.h>
#include <panic.h>
#include <thread.h>
#include <syscall.h>
#include <process.h>
void x86_64_signal_eoi(void);
void x86_64_change_fpusse_allow(bool enable);

static void x86_64_do_signal(struct arch_exception_frame *frame)
{
	int signal = current_thread->signal;
	if(signal) {
		current_thread->signal = 0;
		spinlock_acquire(&current_thread->process->signal_lock);
		struct sigaction *action = &current_thread->process->actions[signal];
		if(action->handler != SIG_IGN
				&& action->handler != SIG_DFL
				&& action->handler != SIG_ERR) {
			interrupt_push_frame(frame, action);
			frame->rip = (uint64_t)action->handler;
			frame->rdi = signal;
			frame->userrsp -= 128;
			frame->userrsp &= 0xFFFFFFFFFFFFFFF0;
			*(uint64_t *)frame->userrsp = SIGNAL_RESTORE_PAGE;
		}
		spinlock_release(&current_thread->process->signal_lock);
	}
}

static void x86_64_restore_frame(struct arch_exception_frame *frame)
{
	struct exception_frame *rest = interrupt_pop_frame();
	if(rest) {
		memcpy(frame, &rest->arch, sizeof(*frame));
		kobj_putref(rest);
	}
}

static void __fault(struct arch_exception_frame *frame)
{
	if(frame->int_no == 6 && current_thread && !current_thread->arch.usedfpu) {
		/* we're emulating FPU instructions / disallowing SSE. Set a flag,
		 * and allow the thread to do its thing */
		current_thread->arch.usedfpu = true;
		x86_64_change_fpusse_allow(true);
		asm volatile ("finit"); /* also, we may need to clear the FPU state */
	} else if(frame->int_no == 14) {
		/* page-fault */
		uint64_t cr2;
		asm volatile("mov %%cr2, %0" : "=r"(cr2));
		int flags = 0;
		if(frame->err_code & 1) {
			flags |= FAULT_ERROR_PERM;
		} else {
			flags |= FAULT_ERROR_PRES;
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
		//if(frame->rip >= KERNEL_VIRT_BASE)
		//printk("pagefault (tid=%ld,pid=%d): %lx, %x, from %lx, %lx\n", current_thread->tid, current_thread->process->pid, cr2, flags, frame->rip, frame->rax);
		mm_fault_entry(cr2, flags, frame->rip);
	} else {
		if(frame->int_no == 1) {
			if(current_thread->tid == 5 || 1) {
				printk("trap %ld: %lx, %lx, %lx\n", current_thread->tid, frame->rip, frame->userrsp, frame->rax);
				//for(;;);
			}
			return;
		}
		if(frame->cs == 0x8) {
			panic(0, "kernel exception %ld: %lx err=%lx",
					frame->int_no, frame->rip, frame->err_code);
		} else {
			switch(frame->int_no) {
				case 0: case 4: case 7: case 16: case 19:
					thread_send_signal(current_thread, SIGFPE);
					break;
				case 1: case 3:
					thread_send_signal(current_thread, SIGTRAP);
					break;
				case 5: case 6: case 11: case 12: case 13: case 17: case 18:
				case 20:
					thread_send_signal(current_thread, SIGILL);
					break;
				default:
					panic(0, "unhandled exception: %ld, kernel error.", frame->int_no);
			}
			thread_send_signal(current_thread, SIGILL);
		}
	}
}

extern void x86_64_fork_return(void *);
extern int kernel_text_start, kernel_text_end;
void arch_thread_fork_entry(void *_frame)
{
	struct arch_exception_frame *frame = _frame;
	if((uintptr_t)_frame >= (uintptr_t)&kernel_text_start && (uintptr_t)_frame < (uintptr_t)&kernel_text_end)
		((void (*)(void))_frame)();
	if(frame->rax == SYS_clone) {
		frame->userrsp = (uintptr_t)current_thread->user_tls_base + USER_TLS_SIZE;
	}
	thread_fork_init();
	x86_64_fork_return(_frame);
}

#include <processor.h>
void x86_64_exception_entry(struct arch_exception_frame *frame)
{
	if(frame->int_no < 32) {
		__fault(frame);
	} else {
		interrupt_entry(frame->int_no, frame->cs == 0x8 ? INTERRUPT_INKERNEL : 0);
	}
	x86_64_signal_eoi();
	if(frame->cs != 0x8) {
		if(thread_check_status_retuser(current_thread))
			x86_64_do_signal(frame);
	}
}

#define DEBUG_SYS 1
void x86_64_syscall_entry(struct arch_exception_frame *frame)
{
	if(frame->rax == SYS_rt_sigreturn) {
		x86_64_restore_frame(frame);
		return;
	}
#if DEBUG_SYS
	if(current_thread->process->pid >= 8 || 0)
		printk("syscall %ld(%2d) %3lu: %lx %lx %lx %lx %lx %lx (from %lx)\n", current_thread->tid, current_thread->processor->id, frame->rax, frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9, frame->rip);
	long num = frame->rax;
#endif
	frame->rax = syscall_entry(frame, frame->rax, frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9);
#if DEBUG_SYS
	long ret = frame->rax;

	if((current_thread->process->pid >= 8 || 0) && 1) {
		if(ret < 0)
			printk("syscall %ld     %3lu: RET %ld\n", current_thread->tid, num, ret);
		else
			printk("syscall %ld     %3lu: RET %lx\n", current_thread->tid, num, ret);
	}
#endif
	if(thread_check_status_retuser(current_thread))
		x86_64_do_signal(frame);
}

