
#if CONFIG_RUN_TESTS

#include <worker.h>
#include <system.h>
#include <printk.h>
#include <processor.h>
#include <mutex.h>
#include <string.h>
#include <interrupt.h>

#include <obj/object.h>
#include <obj/process.h>

struct object *obj, *ctxobj;

_Alignas(0x1000) unsigned char test_o[] = {
  	0xbf, 0x34, 0x12, 0x00, 0x00, 0xb8, 0x09, 0x00,
  	0x00, 0x00, 0xcd, 0x80, 0xeb, 0xfe, 0x00, 0x00,
};

_Alignas(0x1000) unsigned char test_o_fot[] = {
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  	
  	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  	0x76, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

__initializer static void obj_test_init(void)
{
	obj = kobj_allocate(&kobj_object);
	ctxobj = kobj_allocate(&kobj_object);
	obj->length = 16;

	uint64_t _g[2] = {0x123, 0x0};
	object_assign_guid(obj, _g);

	uintptr_t page = object_allocate_frame(obj, 0);
	
	memcpy((void *)(page + PHYS_MAP_START), test_o, sizeof(test_o));

	obj->fot = (struct fot *)test_o_fot;
	printk(":: %d %x\n", obj->fot->length, obj->fot->flags);
	printk(":: %lx %lx\n", obj->fot->entries[1].guid[0], obj->fot->entries[1].guid[1]);

	for(unsigned i=1;i<USER_TLS_SIZE / arch_mm_page_size(0);i++)
		object_allocate_frame(ctxobj, i);
	ctxobj->length = USER_TLS_SIZE;
	ctxobj->fot = (void *)mm_virtual_allocate(0x1000, true);
}

void __process_test_usermode_entry(void *arg)
{
	(void)arg;
	asm volatile("movq $0x123456, %%rdi; movq $9, %%rax; int $0x80" ::: "rax", "rdi");
	for(;;);
}

static void __process_test_entry(void *arg)
{
	(void)arg;
	printk("Got to process test entry\n");
	struct process *proc = current_thread->process;
	assert(proc != NULL);

	arch_interrupt_set(0);
	for(uintptr_t addr = arch_mm_page_size(0);addr < proc->user_context_object->length + arch_mm_page_size(0);addr+=arch_mm_page_size(0)) {
		size_t nr = addr / arch_mm_page_size(0);
		arch_mm_virtual_map(proc->ctx, addr, (uintptr_t)hash_lookup(&proc->user_context_object->physicals, &nr, sizeof(nr)),
				arch_mm_page_size(0), MAP_USER | MAP_WRITE);
	}
	current_thread->user_tls_base = (void *)arch_mm_page_size(0);
	//size_t zero = 0;
	//arch_mm_virtual_map(proc->ctx, 0x100000000, (uintptr_t)hash_lookup(&obj->physicals, &zero, sizeof(zero)), 0x1000, MAP_USER | MAP_WRITE | MAP_EXECUTE);

	arch_thread_usermode_jump((uintptr_t)0x100000000, NULL);
	for(;;);
}

_Atomic int joined = 0;
void __entry(struct worker *worker)
{
	printk("Worker thread %p %p %ld: %p!\n", worker, current_thread, current_thread->tid, worker_arg(worker));
	assert((uintptr_t)worker_arg(worker) == 0x123456);

	struct thread *thread = kobj_allocate(&kobj_thread);
	struct process *process = kobj_allocate(&kobj_process);
	process_attach_thread(process, thread);

	process->user_context_object = ctxobj;

	ctxobj->fot->entries[1].guid[0] = 0x123;
	ctxobj->fot->entries[1].options |= FOT_PRESENT;
	ctxobj->fot->length = 2;

	arch_thread_create(thread, (uintptr_t)__process_test_entry, NULL);
	struct processor *proc = processor_get_current();
	thread->state = THREADSTATE_RUNNING;
	/* inc refs for adding it to the processor */
	kobj_getref(thread);
	processor_add_thread(proc, thread);
	processor_release(proc);

	worker_exit(worker, 0);
	for(;;);
}

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

static int _order_test = 0;
__orderedinitializer(1) static void test_constructor_second(void)
{
	assert(_order_test == 1);
}

__orderedinitializer(0) static void test_constructor_first(void)
{
	assert(_order_test == 0);
	_order_test = 1;
}

#include <charbuffer.h>
static struct mutex contend_lock;
static struct spinlock contend_spin;
static int _init_test = 0;
__initializer static void test_constructor(void)
{
	printk("Test initializer called :)\n");
	mutex_create(&contend_lock);
	spinlock_create(&contend_spin);
	_init_test = 1;
}

static _Atomic int _c_t = 0;
void contention_test(struct worker *worker)
{
	(void)worker;
	for(;;) {
		//spinlock_acquire(&contend_spin);
		mutex_acquire(&contend_lock);
		assert(_c_t++ == 0);
		printk("%ld", current_thread->tid);
		assert(_c_t-- == 1);
		mutex_release(&contend_lock);
		//spinlock_release(&contend_spin);
		//schedule();
	}
}

static struct worker workertest;
static struct worker echo;
struct worker contend[4];

#define MUTEX_TEST 0

#include <slab.h>
struct inode {
	struct kobj_header _header;

	int id;
};

void _inode_create(void *o)
{
	printk("inode create %p\n", o);
}
void _inode_ko_init(void *o)
{
	printk("inode ko init %p\n", o);
}
void _inode_put(void *o)
{
	printk("inode put %p\n", o);
}

struct kobj kobj_inode = {
	.initialized = false,
	.size = sizeof(struct inode),
	.name = "inode",
	.put = _inode_put,
	.create = _inode_create,
	.init = _inode_ko_init,
	.destroy = NULL,
};

struct kobj_lru inode_lru;

bool _inode_init(void *o, void *id)
{
	printk("Init %p : %d\n", o, *(int *)id);
	struct inode *inode = o;
	inode->id = *(int *)id;
	kobj_lru_mark_ready(&inode_lru, o, &inode->id);
	return true;
}

void test_late(void)
{
	assert(_init_test == 1);
	worker_start(&echo, __echo_entry, NULL);
	//worker_start(&workertest, __entry, (void *)0x123456);
	
	while(!worker_join(&workertest)) preempt();
	joined = 1;
#if MUTEX_TEST
	for(int i=0;i<4;i++) {
		printk("Starting contend worker %d\n", i);
		worker_start(&contend[i], contention_test, NULL);
	}
#endif
}

#if MUTEX_TEST
static struct worker _w;
#endif
void test_secondaries(void)
{
#if MUTEX_TEST
	printk("Starting contend worker (CPU %d)\n", arch_processor_current_id());
	worker_start(&_w, contention_test, NULL);
#endif
}

#endif

