#ifndef __SYSCALL_H
#define __SYSCALL_H


unsigned long syscall_entry(uintptr_t caller, unsigned long num,
		unsigned long arg1,
		unsigned long arg2,
		unsigned long arg3,
		unsigned long arg4,
		unsigned long arg5);

#endif

