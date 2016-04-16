#include <thread.h>
#include <printk.h>
#include <interrupt.h>
#include <syscall.h>
#include <sys.h>
#include <fs/sys.h>
#include <errno.h>

#define MAX_SYSCALL 1024

typedef long (*syscall_t)(long, long, long, long, long, long);

#define SC (syscall_t)&

void sys_dump_perf(void);

static syscall_t syscall_table[MAX_SYSCALL] = {
	[SYS_open]     = SC sys_open,
	[SYS_close]    = SC sys_close,
	[SYS_write]    = SC sys_write,
	[SYS_read]     = SC sys_read,
	[SYS_pwrite64] = SC sys_pwrite,
	[SYS_pread64]  = SC sys_pread,
	[SYS_mmap]     = SC sys_mmap,
	[SYS_fork]     = SC sys_fork,
	[SYS_exit]     = SC sys_exit,
	[SYS_mknod]    = SC sys_mknod,
	[SYS_pipe]     = SC sys_pipe,
	[SYS_writev]   = SC sys_writev,
	[SYS_pwritev]  = SC sys_pwritev,
	[SYS_readv]    = SC sys_readv,
	[SYS_preadv]   = SC sys_preadv,
	[SYS_execve]   = SC sys_execve,

	[SYS_socket]   = SC sys_socket,
	[SYS_socketpair]   = SC sys_socketpair,
	[SYS_connect]  = SC sys_connect,
	[SYS_accept]   = SC sys_accept,
	[SYS_listen]   = SC sys_listen,
	[SYS_bind]     = SC sys_bind,
	[SYS_sendto]   = SC sys_sendto,
	[SYS_recvfrom]   = SC sys_recvfrom,

	[SYS_getpid]   = SC sys_getpid,
	[SYS_getppid]   = SC sys_getppid,
	[SYS_getgid]   = SC sys_getgid,
	[SYS_getegid]   = SC sys_getegid,
	[SYS_getuid]   = SC sys_getuid,
	[SYS_geteuid]   = SC sys_geteuid,
	[SYS_setuid]   = SC sys_setuid,
	[SYS_setresuid]   = SC sys_setresuid,
	[SYS_setgid]   = SC sys_setgid,
	[SYS_setresgid]   = SC sys_setresgid,

	[SYS_fcntl]    = SC sys_fcntl,
	[SYS_ioctl]    = SC sys_ioctl,
	[SYS_pselect6]  = SC sys_pselect,
	[SYS_select]   = SC sys_select,

	[SYS_gettid]   = SC sys_gettid,

	[SYS_arch_prctl] = SC sys_arch_prctl,


	[SYS_dump_perf]  = SC sys_dump_perf,
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
		//printk("UNIMP\n");
		ret = -ENOSYS;
	}

	arch_interrupt_set(0);
	if(current_thread->flags & THREAD_RESCHEDULE)
		schedule();
	return ret;
}

