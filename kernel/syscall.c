#include <thread.h>
#include <printk.h>
#include <interrupt.h>
#include <syscall.h>
#include <sys.h>
#include <fs/sys.h>
#include <errno.h>
#include <process.h>
#include <processor.h>
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
	[SYS_munmap]   = SC sys_munmap,
	[SYS_mprotect] = SC sys_mprotect,
	[SYS_clone]    = SC sys_clone,
	[SYS_fork]     = SC sys_fork,
	[SYS_exit]     = SC sys_exit,
	[SYS_mknod]    = SC sys_mknod,
	[SYS_pipe]     = SC sys_pipe,
	[SYS_writev]   = SC sys_writev,
	[SYS_pwritev]  = SC sys_pwritev,
	[SYS_readv]    = SC sys_readv,
	[SYS_preadv]   = SC sys_preadv,
	[SYS_execve]   = SC sys_execve,
	[SYS_dup]      = SC sys_dup,
	[SYS_dup2]     = SC sys_dup2,
	[SYS_fstat]    = SC sys_fstat,
	[SYS_stat]    = SC sys_stat,
	[SYS_wait4]   = SC sys_wait4,
	[SYS_getdents64] = SC sys_getdents,
	[SYS_mremap]    = SC sys_mremap,
	[SYS_mkdir]     = SC sys_mkdir,
	[SYS_access]    = SC sys_access,
	[SYS_lstat]     = SC sys_lstat,
	[SYS_lseek]     = SC sys_lseek,
	[SYS_chdir]     = SC sys_chdir,
	[SYS_fchdir]    = SC sys_fchdir,
	[SYS_mount]     = SC sys_mount,
	[SYS_chroot]    = SC sys_chroot,
	[SYS_readlink]  = SC sys_readlink,
	[SYS_link]      = SC sys_link,
	[SYS_unlink]    = SC sys_unlink,
	[SYS_utimes]    = SC sys_utimes,
	[SYS_unlinkat]  = SC sys_unlinkat,
	[SYS_rmdir]     = SC sys_rmdir,
	[SYS_symlink]   = SC sys_symlink,
	[SYS_openat]    = SC sys_openat,
	[SYS_utimensat] = SC sys_utimensat,
	[SYS_fchmod]    = SC sys_fchmod,
	[SYS_newfstatat]= SC sys_fstatat,
	[SYS_linkat]    = SC sys_linkat,
	[SYS_renameat]  = SC sys_renameat,
	[SYS_rename]    = SC sys_rename,
	[SYS_faccessat] = SC sys_faccessat,
	[SYS_umask]     = SC sys_umask,
	[SYS_fchmodat]  = SC sys_fchmodat,
	[SYS_chmod]     = SC sys_chmod,
	[SYS_fchownat]  = SC sys_fchownat,
	[SYS_fchown]    = SC sys_fchown,
	[SYS_chown]     = SC sys_chown,
	[SYS_lchown]    = SC sys_lchown,

	[SYS_socket]   = SC sys_socket,
	[SYS_socketpair]   = SC sys_socketpair,
	[SYS_connect]  = SC sys_connect,
	[SYS_accept]   = SC sys_accept,
	[SYS_listen]   = SC sys_listen,
	[SYS_bind]     = SC sys_bind,
	[SYS_sendto]   = SC sys_sendto,
	[SYS_recvfrom]   = SC sys_recvfrom,
	[SYS_setsockopt] = SC sys_setsockopt,
	[SYS_getsockopt] = SC sys_getsockopt,
	[SYS_getpeername] = SC sys_getpeername,
	[SYS_getsockname] = SC sys_getsockname,

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
	[SYS_setgroups]   = SC sys_setgroups,

	[SYS_fcntl]    = SC sys_fcntl,
	[SYS_ioctl]    = SC sys_ioctl,
	[SYS_pselect6]  = SC sys_pselect,
	[SYS_select]   = SC sys_select,
	[SYS_ppoll]    = SC sys_ppoll,
	[SYS_poll]    = SC sys_poll,
	[SYS_nanosleep] = SC sys_nanosleep,

	[SYS_gettid]   = SC sys_gettid,
	[SYS_kill]     = SC sys_kill,
	[SYS_rt_sigaction] = SC sys_sigaction,
	[SYS_rt_sigprocmask] = SC sys_sigprocmask,
	[SYS_getitimer] = SC sys_getitimer,
	[SYS_setitimer] = SC sys_setitimer,
	[SYS_fadvise64] = SC sys_fadvise,
	[SYS_clock_gettime] = SC sys_clock_gettime,
	[SYS_clock_getres] = SC sys_clock_getres,
	[SYS_fsync] = SC sys_fsync,
	[SYS_sync] = SC sys_sync,
	[SYS_uname] = SC sys_uname,
	[SYS_setpgid] = SC sys_setpgid,
	[SYS_setsid] = SC sys_setsid,

	[SYS_futex] = SC sys_futex,
	[SYS_set_tid_address] = SC sys_set_tid_address,
	[SYS_exit_group]      = SC sys_exit_group,

	[SYS_arch_prctl]      = SC sys_arch_prctl,


	/* non-linux syscalls */
	[SYS_dump_perf]       = SC sys_dump_perf,


	/* syscalls I don't want to provide TODO */
	[SYS_brk]             = SC sys_brk,

};

long syscall_entry(void *frame, long num,
		long arg1,
		long arg2,
		long arg3,
		long arg4,
		long arg5,
		long arg6)
{
	assert(current_thread->processor->preempt_disable == 0);
	arch_interrupt_set(1);

	if(num == SYS_vfork)
		num = SYS_fork;
	long ret;
	syscall_t call = syscall_table[num];
	if(call) {
		if(num == SYS_fork)
			arg1 = (long)frame;
		else if(num == SYS_clone)
			arg6 = (long)frame;
		ret = call(arg1, arg2, arg3, arg4, arg5, arg6);
	} else {
		printk("[p%d, t%ld]: unimplemented syscall %ld\n", current_thread->process->pid, current_thread->tid, num);
		ret = -ENOSYS;
	}

	arch_interrupt_set(0);
#if CONFIG_DEBUG
	assert(current_thread->held_spinlocks == 0);
#endif
	if(current_thread->processor->preempt_disable != 0)
		panic(0, "returning to userspace with preempt_disable=%d (#%ld)\n", current_thread->processor->preempt_disable, num);
	assert(current_thread->processor->preempt_disable == 0);
	if(current_thread->flags & THREAD_RESCHEDULE)
		schedule();
	return ret;
}

