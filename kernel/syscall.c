#include <thread.h>
#include <printk.h>
#include <interrupt.h>
unsigned long syscall_entry(unsigned long num,
		unsigned long arg1,
		unsigned long arg2,
		unsigned long arg3,
		unsigned long arg4,
		unsigned long arg5)
{
	arch_interrupt_set(1);
	printk("syscall %lu: %lx %lx %lx %lx %lx", num, arg1, arg2, arg3, arg4, arg5);


	arch_interrupt_set(0);
	if(current_thread->flags & THREAD_RESCHEDULE)
		schedule();
	return 0;
}

