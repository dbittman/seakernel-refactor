#pragma once

#include <mmu.h>
#include <lib/linkedlist.h>
#include <slab.h>
#include <thread.h>
#include <obj/object.h>

struct process {
	struct kobj_header _header;
	struct vm_context *ctx;
	struct linkedlist threads;
	struct linkedlist objects;

	struct object *user_context_object;
};

extern struct kobj kobj_process;

void process_attach_thread(struct process *proc, struct thread *thread);
void process_pagefault_handle(uintptr_t addr, int flags);

