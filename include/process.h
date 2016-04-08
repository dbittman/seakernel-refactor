#pragma once
#include <lib/hash.h>
#include <spinlock.h>

struct vm_context;
struct process {
	struct vm_context *ctx;
	struct hash files;
	struct linkedlist threads;

	struct hash mappings;
	struct spinlock map_lock;
};

