#pragma once
#include <lib/hash.h>
#include <spinlock.h>
#include <slab.h>
#include <signal.h>

struct vm_context;
struct thread;

#define MAX_FD 128

struct fildes {
	struct file *file;
	int flags;
};

struct filesystem;
struct dirent;
struct pty_file;
struct process {
	struct kobj _header;
	struct vm_context *ctx;
	struct linkedlist threads;
	struct process * _Atomic parent;
	struct spinlock lock;

	struct hash mappings;
	struct spinlock map_lock;

	struct fildes files[MAX_FD];
	struct spinlock files_lock;

	int pid;
	_Atomic uintptr_t next_user_tls, next_mmap_reg;

	struct filesystem *root;
	struct dirent *cwd;
	int cmask;
	_Atomic int uid, gid, euid, egid, sgid, suid;
	int exit_code;

	struct sigaction actions[_NSIG+1];
	struct spinlock signal_lock;

	struct pty_file *pty;
};

extern struct kobj kobj_process;
void process_attach_thread(struct process *proc, struct thread *thread);
uintptr_t process_allocate_user_tls(struct process *proc);
uintptr_t process_allocate_mmap_region(struct process *proc, size_t len);
void process_copy_mappings(struct process *from, struct process *to);
void process_remove_mappings(struct process *proc, bool);
void process_copy_files(struct process *from, struct process *to);
void process_close_files(struct process *proc, bool all);
void process_exit(struct process *proc, int code);
struct process *process_get_by_pid(int pid);
void process_send_signal(struct process *target, int sig);


extern struct kobj_idmap processids;

#define USER_TLS_REGION_END   0x800000000000
#define USER_TLS_REGION_START 0x700000000000

#define USER_MMAP_REGION_END   0x700000000000
#define USER_MMAP_REGION_START 0x400000000000


#define USER_REGION_START     arch_mm_page_size(0)
#define USER_REGION_END       0x800000000000

#define SIGNAL_RESTORE_PAGE   0x3000

extern struct process *kernel_process;
