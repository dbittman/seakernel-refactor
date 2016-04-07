#include <panic.h>
#include <stdnoreturn.h>
#include <printk.h>
#include <interrupt.h>
#include <debug.h>
#include <processor.h>
#include <thread.h>
noreturn void panic(int flags, const char *fmt, ...)
{
	arch_interrupt_set(0);
	arch_panic_begin();
	(void)flags;
	/* clear interrupts */
	va_list args;
	va_start(args, fmt);
	printk("panic [tid %ld, cpu %d] - ", current_thread ? current_thread->tid : 0, arch_processor_current_id());
	vprintk(fmt, args);
	printk("\n");
	/*if(current_thread)
		mm_print_context(current_thread->ctx);
	else
		mm_print_context(&kernel_context);*/
#if FEATURE_SUPPORTED_UNWIND
	debug_print_backtrace();
#endif
	for(;;);
}

