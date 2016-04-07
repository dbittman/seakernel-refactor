#include <stdbool.h>
#include <lib/bitmap.h>
#include <stddef.h>
#include <stdint.h>
#include <mutex.h>
#include <lib/stack.h>
#include <mmu.h>
#include <string.h>
#include <assert.h>
#define IS_POWER2(x) ((x != 0) && ((x & (~x + 1)) == x))

#define MIN_PHYS_MEM PHYS_MEMORY_START

#define MAX_ORDER 21
#define MIN_SIZE MM_BUDDY_MIN_SIZE
#define MAX_SIZE ((uintptr_t)MIN_SIZE << MAX_ORDER)
#define MEMORY_SIZE (MAX_SIZE)

#define NOT_FREE (-1)
struct mutex pm_buddy_mutex;
uint8_t *bitmaps[MAX_ORDER + 1];

struct stack freelists[MAX_ORDER+1];
static char static_bitmaps[((MEMORY_SIZE / MIN_SIZE) / 8) * 2];
static bool inited = false;
static size_t num_allocated[MAX_ORDER + 1];

static _Atomic size_t free_memory = 0;
static _Atomic size_t total_memory = 0;

static inline int min_possible_order(uintptr_t address)
{
	address /= MIN_SIZE;
	int o = 0;
	while(address && !(address & 1)) {
		o++;
		address >>= 1;
	}
	return o;
}

static inline size_t buddy_order_max_blocks(int order)
{
	return MEMORY_SIZE / ((uintptr_t)MIN_SIZE << order);
}

#include <printk.h>
static uintptr_t __do_pmm_buddy_allocate(size_t length)
{
	assert(inited);
	if(!IS_POWER2(length))
		panic(0, "can only allocate in powers of 2 (not %lx)", length);
	if(length < MIN_SIZE)
		panic(0, "length less than minimum size");
	if(length > MAX_SIZE) {
		panic(0, "out of physical memory");
	}

	int order = min_possible_order(length);

	if(stack_is_empty(&freelists[order])) {
		uintptr_t a = __do_pmm_buddy_allocate(length * 2);

		struct stack_elem *elem1 = (void *)(a + PHYS_MAP_START);
		struct stack_elem *elem2 = (void *)(a + length + PHYS_MAP_START);

		stack_push(&freelists[order], elem1, (void *)a);
		stack_push(&freelists[order], elem2, (void *)(a + length));
	}

	uintptr_t address = (uintptr_t)stack_pop(&freelists[order]);
	int bit = address / length;
	assert(!bitmap_test(bitmaps[order], bit));
	bitmap_set(bitmaps[order], bit);
	num_allocated[order]++;

	return address;
}

static int deallocate(uintptr_t address, int order)
{
	assert(inited);
	if(order > MAX_ORDER)
		return -1;
	int bit = address / ((uintptr_t)MIN_SIZE << order);
	if(!bitmap_test(bitmaps[order], bit)) {
		return deallocate(address, order + 1);
	} else {
		uintptr_t buddy = address ^ ((uintptr_t)MIN_SIZE << order);
		int buddy_bit = buddy / ((uintptr_t)MIN_SIZE << order);
		bitmap_reset(bitmaps[order], bit);

		if(!bitmap_test(bitmaps[order], buddy_bit)) {
			struct stack_elem *elem = (void *)(buddy + PHYS_MAP_START);
			stack_delete(&freelists[order], elem);
			deallocate(buddy > address ? address : buddy, order + 1);
		} else {
			struct stack_elem *elem = (void *)(address + PHYS_MAP_START);
			stack_push(&freelists[order], elem, (void *)address);
		}
		num_allocated[order]--;
		return order;
	}
}

uintptr_t pmm_buddy_allocate(size_t length)
{
	mutex_acquire(&pm_buddy_mutex);
	uintptr_t ret = __do_pmm_buddy_allocate(length);
	free_memory -= length;
	mutex_release(&pm_buddy_mutex);
	return ret;
}

void pmm_buddy_deallocate(uintptr_t address)
{
	if(address >= MIN_PHYS_MEM + MEMORY_SIZE)
		return;
	mutex_acquire(&pm_buddy_mutex);
	int order = deallocate(address, 0);
	if(order >= 0) {
		free_memory += MIN_SIZE << order;
		if(total_memory < free_memory)
			total_memory = free_memory;
	}
	mutex_release(&pm_buddy_mutex);
}

void pmm_buddy_init()
{
	mutex_create(&pm_buddy_mutex);
	uintptr_t start = (uintptr_t)static_bitmaps;
	int length = ((MEMORY_SIZE / MIN_SIZE) / (8));
	for(int i=0;i<=MAX_ORDER;i++) {
		bitmaps[i] = (uint8_t *)start;
		memset(bitmaps[i], ~0, length);
		stack_create(&freelists[i], STACK_LOCKLESS);
		start += length;
		length /= 2;
		num_allocated[i] = buddy_order_max_blocks(i);
	}
	inited = true;
}

int mm_physical_get_usage(void)
{
	int use = 100 - (100 * free_memory) / total_memory;
	if(use < 0)
		use = 0;
	if(use > 100)
		use = 100;
	return use;
}

