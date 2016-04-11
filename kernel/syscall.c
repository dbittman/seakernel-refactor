#include <thread.h>
#include <printk.h>
#include <interrupt.h>
#include <syscall.h>
#include <sys.h>
#include <fs/sys.h>

#define MAX_SYSCALL 256


typedef long (*syscall_t)(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);

#define SC (syscall_t)&

static syscall_t syscall_table[MAX_SYSCALL] = {
	[SYS_OPEN]   = SC sys_open,
	[SYS_CLOSE]  = SC sys_close,
	[SYS_WRITE]  = SC sys_write,
	[SYS_READ]   = SC sys_read,
	[SYS_PWRITE] = SC sys_pwrite,
	[SYS_PREAD]  = SC sys_pread,
	[SYS_MMAP]   = SC sys_mmap,
	[SYS_FORK]   = SC sys_fork,
	[SYS_EXIT]   = SC sys_exit,
};


unsigned long syscall_entry(unsigned long num,
		unsigned long arg1,
		unsigned long arg2,
		unsigned long arg3,
		unsigned long arg4,
		unsigned long arg5)
{
	arch_interrupt_set(1);
	printk("syscall %lu: %lx %lx %lx %lx %lx\n", num, arg1, arg2, arg3, arg4, arg5);

	long ret;
	syscall_t call = syscall_table[num];
	if(call) {
		ret = call(arg1, arg2, arg3, arg4, arg5);
	} else {
		printk("UNIMP\n");
		ret = -1; //TODO -ENOSYS;
	}

	arch_interrupt_set(0);
	if(current_thread->flags & THREAD_RESCHEDULE)
		schedule();
	return ret;
}

