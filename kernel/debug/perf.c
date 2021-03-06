#if CONFIG_PERF_FUNCTIONS

#if !FEATURE_SUPPORTED_CYCLE_COUNT
#error "CONFIG_PERF_FUNCTIONS requires FEATURE_SUPPORTED_CYCLE_COUNT"
#endif

#include <printk.h>
#include <system.h>
#include <stdbool.h>
#include <stdint.h>
#include <mmu.h>
#include <ksymbol.h>
#include <processor.h>
#include <klibc.h>
#include <string.h>
#include <fs/proc.h>
#include <errno.h>

static _Atomic bool enable = false;

extern int kernel_text_start;
extern int kernel_text_end;
struct fcall {
	size_t count;
	uintptr_t base;
	uint64_t start, mean;
	_Atomic int level;
	int iflag;
};

static struct fcall *calls;
static int num_calls;
static size_t length;
static uint64_t calibration = 0;
__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
	if(likely(enable)) {
		(void)call_site;
		int index = (uintptr_t)this_fn - (uintptr_t)&kernel_text_start;
		calls[index].base = (uintptr_t)this_fn;
		if(likely(atomic_fetch_add(&calls[index].level, 1) == 0)) {
			//calls[index].iflag = arch_interrupt_set(0);
			calls[index].start = arch_processor_get_cycle_count();
		}
	}
}

__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
	(void)call_site;
	if(likely(enable)) {
		uint64_t c = arch_processor_get_cycle_count();
		int index = (uintptr_t)this_fn - (uintptr_t)&kernel_text_start;
		//int flag = calls[index].iflag;
		if(likely(atomic_fetch_sub(&calls[index].level, 1) == 1)) {
			//arch_interrupt_set(flag);
			int64_t x = (c - calls[index].start) - calibration;
			if(likely(x > 0))
				calls[index].mean = (calls[index].mean * calls[index].count + x) / (calls[index].count+1);
			calls[index].count++;
		}
	}
}

__attribute__((no_instrument_function))
void perf_init(void)
{
	size_t len = ((uintptr_t)&kernel_text_end - (uintptr_t)&kernel_text_start) * sizeof(struct fcall);
	len = __round_up_pow2(len);
	if(len < 0x800000)
		len = 0x800000;
	calls = (void *)mm_virtual_allocate(len, true);
	num_calls = len / sizeof(struct fcall);
	length = len;
	enable = true;
	int index = (uintptr_t)&perf_init - (uintptr_t)&kernel_text_start;
	uint64_t min = UINT64_MAX;
	for(int i=0;i<100000;i++) {
		__cyg_profile_func_enter(&perf_init, NULL);
		__cyg_profile_func_exit(&perf_init, NULL);
		if(calls[index].mean < min)
			min = calls[index].mean;
	}
	calibration = min;
	calls[index].count = 0;
	printk("[perf]: allocated %ld KB of space for profiling, calibrated to %ld\n", len, calibration);
}

#if 0 /* sort by total time */
static int __compar(const void *_a, const void *_b)
{
	const struct fcall *a = _a, *b = _b;
	return b->count * b->mean - a->count * a->mean;
}
#else /* sort by number of calls */
 int __compar(const void *_a, const void *_b)
{
	const struct fcall *a = _a, *b = _b;
	return b->count - a->count;
}
#endif

void perf_print_report(void)
{
	enable = false;
	printk("                 FUNCTION NAME     #CALLS    MEAN TIME   TOTAL TIME\n");
	struct fcall *sorted_calls = (void *)mm_virtual_allocate(length, true);
	memcpy(sorted_calls, calls, length);
	size_t total = 0;
	for(int i=0;i<num_calls;i++) {
		struct fcall *call = &sorted_calls[i];
		if(call->count) {
			memcpy(&sorted_calls[total++], call, sizeof(*call));
		}
	}
	qsort(sorted_calls, total, sizeof(struct fcall), __compar);
	for(unsigned i=0;i<total;i++) {
		struct fcall *call = &sorted_calls[i];
		if(call->count) {
			const struct ksymbol *ks = ksymbol_find_by_value((void *)call->base, false);
			printk("%30.30s %10ld %12ld %12ld\n", ks ? ks->name : "???", call->count, call->mean, call->count * call->mean);
		}
	}
	mm_virtual_deallocate((uintptr_t)sorted_calls);
	enable = true;
}

static ssize_t _proc_perf(void *data, int rw, size_t off, size_t len, char *buf)
{
	if(rw != 0)
		return -EINVAL;
	(void)data;
	size_t current = 0;
	enable = false;
	PROCFS_PRINTF(off, len, buf, current,
			"                 FUNCTION NAME     #CALLS    MEAN TIME   TOTAL TIME\n");
	struct fcall *sorted_calls = (void *)mm_virtual_allocate(length, true);
	memcpy(sorted_calls, calls, length);
	size_t total = 0;
	for(int i=0;i<num_calls;i++) {
		struct fcall *call = &sorted_calls[i];
		if(call->count) {
			memcpy(&sorted_calls[total++], call, sizeof(*call));
		}
	}
	//qsort(sorted_calls, total, sizeof(struct fcall), __compar);
	for(unsigned i=0;i<total;i++) {
		struct fcall *call = &sorted_calls[i];
		if(call->count) {
			const struct ksymbol *ks = ksymbol_find_by_value((void *)call->base, false);
			PROCFS_PRINTF(off, len, buf, current,
					"%30.30s %10ld %12ld %12ld\n", ks ? ks->name : "???", call->count, call->mean, call->count * call->mean);
		}
	}
	mm_virtual_deallocate((uintptr_t)sorted_calls);

	enable = true;
	return current;
}

static void _late_init(void)
{
	proc_create("/proc/perf", _proc_perf, NULL);
}

__initializer static void _init_perf_proc(void) { init_register_late_call(&_late_init, NULL); }

#endif
void sys_dump_perf(void)
{
#if CONFIG_PERF_FUNCTIONS
	perf_print_report();
#endif
}


