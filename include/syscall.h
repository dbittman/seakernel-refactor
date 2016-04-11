#ifndef __SYSCALL_H
#define __SYSCALL_H


unsigned long syscall_entry(unsigned long num,
		unsigned long arg1,
		unsigned long arg2,
		unsigned long arg3,
		unsigned long arg4,
		unsigned long arg5);


#define SYS_OPEN 1
#define SYS_WRITE 2
#define SYS_READ 3
#define SYS_PWRITE 4
#define SYS_PREAD 5
#define SYS_CLOSE 6

#define SYS_MMAP 20

#define SYS_FORK 30
#define SYS_EXIT 31


#endif

