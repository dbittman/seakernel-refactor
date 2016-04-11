#pragma once
#include <stddef.h>
void machine_init(void);
size_t machine_get_memlen(void);

struct boot_module {
	uintptr_t start;
	size_t length;
	char name[128];
};

struct boot_module *machine_get_boot_module(int i);

