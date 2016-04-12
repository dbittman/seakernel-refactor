#include <printk.h>
#include <panic.h>
#include <stdnoreturn.h>
#include <stdarg.h>
#include <thread.h>
#include <machine/machine.h>
#include <mmu.h>
#include <processor.h>
#include <interrupt.h>
#include <timer.h>
#include <slab.h>
#include <worker.h>
#include <debug.h>
#include <trace.h>
#include <system.h>
#include <fs/sys.h>
#include <sys.h>
extern void _init(void);
void test_late(void);
void tlb_shootdown();
void perf_init(void);
void initial_rootfs_init(void);

static char *argv[] = {
	"/init",
	NULL,
};

static char *env[] = {
	"HOME=/",
	"PATH=/",
	"TERM=seaos",
	NULL,
};

static void _init_entry(void *arg)
{
	(void)arg;

	int f = sys_open("/null", O_RDONLY, 0);
	assert(f == 0);
	f = sys_open("/null", O_WRONLY, 0);
	assert(f == 1);
	f = sys_open("/null", O_WRONLY, 0);
	assert(f == 2);

	int ret = sys_execve("/init", argv, env);
	printk("failed to start init: %d\n", ret);
	sys_exit(0);
}

static void init_worker(struct worker *worker)
{
#if CONFIG_RUN_TESTS
	test_late();
#endif
	
	sys_fork((uintptr_t)&_init_entry, 0);
	
	worker_exit(worker, 0);
}

void main(void)
{
	mm_early_init();
	machine_init();
	interrupt_init();
	mm_init();
	/* call _init once we've got the memory manager set up. */
	_init();
	thread_init();
	timer_init();
	processor_start_secondaries();
	initial_rootfs_init();
#if CONFIG_PERF_FUNCTIONS
	perf_init();
#endif
	arch_interrupt_set(1);
	
	struct worker init;
	worker_start(&init, &init_worker, NULL);

	kernel_idle_work();
}

void kernel_idle_work(void)
{
	struct workqueue *wq;
	struct processor *proc = processor_get_current();
	wq = &proc->workqueue;
	processor_release(proc);
	printk("[kernel]: entered background (tid %ld)\n", current_thread->tid);
	arch_interrupt_set(1);
	for(;;) {
		if(!workqueue_empty(wq))
			workqueue_execute(wq);
		schedule();
	}
}

