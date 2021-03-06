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
#include <arena.h>
#include <process.h>
#include <lib/linkedlist.h>
extern void _init(void);
void test_late(void);
void tlb_shootdown();
void perf_init(void);
void initial_rootfs_init(void);

static char *argv[] = {
	"/bin/init",
	NULL,
};

static char *env[] = {
	"HOME=/",
	"PATH=/bin:/usr/bin",
	"TERM=seaos",
	NULL,
};

static void _init_entry(void *arg)
{
	(void)arg;

	current_thread->process->pgroupid = 1;
	current_thread->process->seshid = 1;
	sys_open("/dev/null", O_RDWR, 0);
	sys_open("/dev/null", O_RDWR, 0);
	sys_open("/dev/null", O_RDWR, 0);
	int ret = sys_execve("/bin/init", argv, env);
	printk("failed to start init: %d\n", ret);
	for(;;);
}

static struct linkedlist late_init_calls;
struct arena li_calls_arena;
struct li_call {
	void *call;
	void *data;
	struct linkedentry entry;
};

#include <charbuffer.h>
static void init_worker(struct worker *worker)
{
	printk("[kernel]: doing late init calls\n");
	struct linkedentry *entry;
	for(entry = linkedlist_back_iter_start(&late_init_calls);
			entry != linkedlist_back_iter_end(&late_init_calls);
			entry = linkedlist_back_iter_next(entry)) {
		struct li_call *call = linkedentry_obj(entry);
		assert(call != NULL);
		((void (*)(void *))call->call)(call->data);
	}
	
#if CONFIG_RUN_TESTS
	test_late();
#endif
	sys_fork(&_init_entry);
	
	worker_exit(worker, 0);
}


void init_register_late_call(void *call, void *data)
{
	struct li_call *lic = arena_allocate(&li_calls_arena, sizeof(struct li_call));
	lic->call = call;
	lic->data = data;
	linkedlist_insert(&late_init_calls, &lic->entry, lic);
}

void main(void)
{
	mm_early_init();
	machine_init();
	interrupt_init();
	mm_init();
	linkedlist_create(&late_init_calls, 0);
	arena_create(&li_calls_arena);
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

#include <x86_64-ioport.h>
#include <processor.h>
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

