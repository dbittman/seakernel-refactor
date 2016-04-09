
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

static void _thread_hello(void)
{
	for(;;);
}

#include <sys.h>
static void _thread_entry(void *arg)
{
	(void)arg;
	sys_fork((uintptr_t)&_thread_hello);
	_thread_hello();
	for(;;);
}

struct worker echo;
void test_late(void)
{
	worker_start(&echo, __echo_entry, NULL);
	struct process *proc = kobj_allocate(&kobj_process);
	struct thread *thread = kobj_allocate(&kobj_thread);
	process_attach_thread(proc, thread);
	arch_thread_create(thread, (uintptr_t)&_thread_entry, NULL);
	thread->user_tls_base = (void *)process_allocate_user_tls(proc);
	thread->state = THREADSTATE_RUNNING;
	processor_add_thread(current_thread->processor, thread);
}

void test_secondaries(void)
{
}

#endif

