
#if CONFIG_RUN_TESTS

#include <worker.h>
#include <system.h>
#include <printk.h>
#include <processor.h>
#include <mutex.h>
#include <string.h>
#include <interrupt.h>
#include <process.h>

void perf_print_report(void);
extern unsigned char serial_getc(void);
static void __echo_entry(struct worker *worker)
{
	(void)worker;
	printk("Starting echo service %ld!\n", current_thread->tid);
	for(;;) {
		char c = serial_getc();
		if(c == '`') {
			printk("==TESTS PASSED==\n");
			arch_processor_reset();
#if CONFIG_PERF_FUNCTIONS
		} else if(c == 'p') {
			printk("PERF REPORT\n");
			perf_print_report();
#endif
		} else {
			printk("%c", c);
			if(c == '\r')
				printk("\n");
		}
	}
}

#include <sys.h>
void _thread_hello(void)
{
	int r = sys_execve("/test", NULL, NULL);
	printk("exec returned %d\n", r);
	for(;;);
}

#include <sys.h>
static void _thread_entry(void *arg)
{
	(void)arg;
	printk("Test!\n");
	sys_fork((uintptr_t)&_thread_hello, 0);
	for(;;);
}

struct worker echo;
void test_late(void)
{
	worker_start(&echo, __echo_entry, NULL);
	sys_fork((uintptr_t)&_thread_entry, 0);
}

void test_secondaries(void)
{
}

#endif

