#include <thread.h>
#include <mmu.h>
#include <sys.h>
#include <panic.h>
#include <system.h>
#include <errno.h>
#include <lib/hash.h>
#include <printk.h>
static struct hash map;
static struct spinlock lock;

struct futex {
	struct kobj_header _header;
	uintptr_t phys;
	struct hashelem elem;
	struct blocklist wait;
};

__initializer static void __futex_init_map(void)
{
	hash_create(&map, HASH_LOCKLESS, 1024);
	spinlock_create(&lock);
}

static void _futex_init(void *obj)
{
	struct futex *f = obj;
	f->phys = 0;
}

static void _futex_create(void *obj)
{
	struct futex *f = obj;
	blocklist_create(&f->wait);
	_futex_init(obj);
}

struct kobj kobj_futex = {
	KOBJ_DEFAULT_ELEM(futex),
	.create = _futex_create,
	.destroy = NULL,
	.init = _futex_init,
	.put = NULL,
};

static struct futex *get_futex(_Atomic int *uaddr, bool alloc)
{
	uintptr_t v = (uintptr_t)uaddr;
	size_t size;
	uintptr_t phys_page;
	arch_mm_virtual_getmap(current_thread->ctx, v, &phys_page, NULL, &size);
	uintptr_t phys = phys_page + (v & (size-1));

	spinlock_acquire(&lock);
	struct futex *f;
	if((f=hash_lookup(&map, &phys, sizeof(uintptr_t))) == NULL) {
		if(!alloc) {
			spinlock_release(&lock);
			return NULL;
		}
		f = kobj_allocate(&kobj_futex);
		f->phys = phys;
		hash_insert(&map, &f->phys, sizeof(uintptr_t), &f->elem, f);
	} else {
		kobj_getref(f);
	}
	spinlock_release(&lock);
	return f;
}

static void drop_futex(struct futex *f)
{
	spinlock_acquire(&lock);
	uintptr_t p = f->phys;
	if(kobj_putref(f) == 0) {
		hash_delete(&map, &p, sizeof(uintptr_t));
	}
	spinlock_release(&lock);
}

static int __futex_wait(_Atomic int *uaddr, int val, const struct timespec *timeout)
{
	struct futex *f = get_futex(uaddr, true);
	struct blockpoint bp;
	uint64_t time = 0;
	if(timeout) {
		time = timeout->tv_nsec / 1000 + timeout->tv_sec * 1000000;
	}
	_Atomic int *loc = (_Atomic int *)(f->phys + PHYS_MAP_START);
	
	blockpoint_create(&bp, timeout ? BLOCK_TIMEOUT : 0, time);
	blockpoint_startblock(&f->wait, &bp);
	atomic_thread_fence(memory_order_seq_cst);
	if(atomic_load(loc) == val) {
		schedule();
	}
	atomic_thread_fence(memory_order_seq_cst);
	enum block_result res = blockpoint_cleanup(&bp);
	drop_futex(f);
	switch(res) {
		case BLOCK_RESULT_UNBLOCKED: case BLOCK_RESULT_BLOCKED:
			return 0;
		case BLOCK_RESULT_TIMEOUT:
			return -ETIMEDOUT;
		case BLOCK_RESULT_INTERRUPTED:
			return -EINTR;
	}
}

static int __futex_wake(_Atomic int *uaddr, int num)
{
	struct futex *f = get_futex(uaddr, false);
	if(!f)
		return 0;
	int woken = 0;
	for(int i=0;i<num;i++) {
		if(!blocklist_unblock_one(&f->wait))
			break;
		woken++;
	}
	drop_futex(f);
	return woken;
}

sysret_t sys_futex(_Atomic int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3)
{
	int ret;
	(void)uaddr2;
	(void)val3;
	op &= ~FUTEX_PRIVATE;
	switch(op) {
		case FUTEX_WAIT:
			ret = __futex_wait(uaddr, val, timeout);
			break;
		case FUTEX_WAKE:
			ret = __futex_wake(uaddr, val);
			break;
		default:
			panic(0, "futex op %d not implemented", op);
	}

	return ret;
}

