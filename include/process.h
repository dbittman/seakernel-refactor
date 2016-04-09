#pragma once
#include <lib/hash.h>
#include <spinlock.h>
#include <slab.h>

struct vm_context;
struct thread;
struct process {
	struct kobj _header;
	struct vm_context *ctx;
	struct hash files;
	struct linkedlist threads;

	struct hash mappings;
	struct spinlock map_lock;

	int pid;
	_Atomic uintptr_t next_user_tls;
};

extern struct kobj kobj_process;
void process_attach_thread(struct process *proc, struct thread *thread);
uintptr_t process_allocate_user_tls(struct process *proc);

#define USER_TLS_REGION_END   0x800000000000
#define USER_TLS_REGION_START 0x700000000000

#define USER_REGION_START     0
#define USER_REGION_END       0x800000000000
