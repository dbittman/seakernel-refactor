C_SOURCES+=kernel/main.c kernel/printk.c kernel/thread.c kernel/mmu.c kernel/processor.c kernel/pm_buddy.c kernel/mutex.c kernel/spinlock.c kernel/timer.c kernel/interrupt.c kernel/slab.c kernel/panic.c kernel/syscall.c kernel/schedule.c kernel/worker.c kernel/workqueue.c kernel/ksymbol.c kernel/random.c kernel/test.c kernel/blocklist.c kernel/ksymbol_weak.c kernel/charbuffer.c kernel/queue.c kernel/kobj_lru.c kernel/mapping.c kernel/frame.c kernel/process.c kernel/elf.c

include kernel/debug/include.mk
include kernel/fs/include.mk
include kernel/sys/include.mk

