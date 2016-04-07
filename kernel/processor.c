#include <printk.h>
#include <processor.h>
#include <stddef.h>
#include <arch-timer.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <thread-bits.h>
#include <mmu.h>
#include <interrupt.h>
#include <worker.h>
#include <priqueue.h>
extern int initial_boot_stack;
static struct processor plist[MAX_PROCESSORS];

struct processor *processor_get_id(int id)
{
	return &plist[id];
}

struct processor *processor_get_current(void)
{
	int old = arch_interrupt_set(0);
	struct processor *proc = processor_get_id(arch_processor_current_id());
	atomic_fetch_add(&proc->preempt_disable, 1);
	arch_interrupt_set(old);
	return proc;
}

void processor_add_thread(struct processor *proc, struct thread *thread)
{
	if(thread == &proc->idle_thread)
		return;
	priqueue_insert(&proc->runqueue, &thread->runqueue_node, thread, thread_current_priority(thread));
	thread->processor = proc;
}

void processor_create(int id, int flags)
{
	if(id >= MAX_PROCESSORS) {
		printk("warning - refusing to initialize processor %d. Too many processors!\n", id);
		return;
	}
	struct processor *proc = &plist[id];
	proc->flags = flags | PROCESSOR_PRESENT;
	proc->preempt_disable = 0;
	proc->id = id;

	/* add one, since we're passing a count */
	priqueue_create(&proc->runqueue, MAX_THREAD_PRIORITY + 1);
	workqueue_create(&proc->workqueue);
	proc->running = &proc->idle_thread;
	proc->idle_thread.state = THREADSTATE_RUNNING;
	if(flags & PROCESSOR_UP)
		proc->idle_stack = (void *)&initial_boot_stack;
	else
		proc->idle_stack = (void *)mm_virtual_allocate(KERNEL_STACK_SIZE, false);
}

void processor_start_secondaries(void)
{
	for(int i=0;i<MAX_PROCESSORS;i++) {
		if(!(plist[i].flags & PROCESSOR_UP) && (plist[i].flags & PROCESSOR_PRESENT)) {
			arch_processor_poke_secondary(i, (uintptr_t)plist[i].idle_stack);
		}
	}
}

void test_secondaries(void);

static void secondary_test_worker(struct worker *w)
{
#if CONFIG_RUN_TESTS
	test_secondaries();
#endif
	worker_exit(w, 0);
}

void processor_secondary_main(void)
{
	arch_timer_init();
	int id = arch_processor_current_id();
	thread_init();

	plist[id].flags |= PROCESSOR_UP;

	printk("Processor %d initialized, tid %lu\n", id, current_thread->tid);
	arch_interrupt_set(1);

	struct worker test_worker;
	worker_start(&test_worker, &secondary_test_worker, NULL);

	kernel_idle_work();
}

