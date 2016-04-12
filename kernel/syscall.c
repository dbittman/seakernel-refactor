#include <thread.h>
#include <printk.h>
#include <interrupt.h>
#include <syscall.h>
#include <sys.h>
#include <fs/sys.h>

#define MAX_SYSCALL 256


typedef long (*syscall_t)(long, long, long, long, long, long);

#define SC (syscall_t)&

static syscall_t syscall_table[MAX_SYSCALL] = {
	[SYS_open]   = SC sys_open,
	[SYS_close]  = SC sys_close,
	[SYS_write]  = SC sys_write,
	[SYS_read]   = SC sys_read,
	[SYS_pwrite64] = SC sys_pwrite,
	[SYS_pread64]  = SC sys_pread,
	[SYS_mmap]   = SC sys_mmap,
	[SYS_fork]   = SC sys_fork,
	[SYS_exit]   = SC sys_exit,

	[SYS_arch_prctl] = SC sys_arch_prctl,
};

long syscall_entry(long num,
		long arg1,
		long arg2,
		long arg3,
		long arg4,
		long arg5,
		long arg6)
{
	arch_interrupt_set(1);

	long ret;
	syscall_t call = syscall_table[num];
	if(call) {
		ret = call(arg1, arg2, arg3, arg4, arg5, arg6);
	} else {
		printk("UNIMP\n");
		ret = -1; //TODO -ENOSYS;
	}

	arch_interrupt_set(0);
	if(current_thread->flags & THREAD_RESCHEDULE)
		schedule();
	return ret;
}

