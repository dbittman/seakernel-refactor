#pragma once
#include <lib/hash.h>
#include <spinlock.h>
#include <slab.h>

struct vm_context;
struct thread;

#define MAX_FD 128

struct fildes {
	struct file * _Atomic file;
	int flags;
};

struct filesystem;
struct dirent;
struct process {
	struct kobj _header;
	struct vm_context *ctx;
	struct linkedlist threads;

	struct hash mappings;
	struct spinlock map_lock;

	struct fildes files[MAX_FD];
	int pid;
	_Atomic uintptr_t next_user_tls;

	struct filesystem *root;
	struct dirent *cwd;
};

extern struct kobj kobj_process;
void process_attach_thread(struct process *proc, struct thread *thread);
uintptr_t process_allocate_user_tls(struct process *proc);

#define USER_TLS_REGION_END   0x800000000000
#define USER_TLS_REGION_START 0x700000000000

#define USER_REGION_START     arch_mm_page_size(0)
#define USER_REGION_END       0x800000000000

extern struct process *kernel_process;
