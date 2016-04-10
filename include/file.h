#pragma once

#include <slab.h>

struct dirent;
struct file {
	struct kobj_header _header;
	struct dirent *dirent;
};

extern struct kobj kobj_file;

struct file *process_get_file(int fd);
void process_release_fd(int fd);
int process_allocate_fd(struct file *file);
