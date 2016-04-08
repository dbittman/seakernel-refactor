
#if CONFIG_RUN_TESTS

#include <worker.h>
#include <system.h>
#include <printk.h>
#include <processor.h>
#include <mutex.h>
#include <string.h>
#include <interrupt.h>

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

