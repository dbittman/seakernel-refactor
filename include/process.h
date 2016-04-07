#pragma once
#include <lib/hash.h>

struct vm_context;
struct process {
	struct vm_context *ctx;
	struct hash files;
	struct linkedlist threads;
};

