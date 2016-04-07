#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct ksymbol {
	uintptr_t value;
	size_t size;
	const char *name;
};

extern const struct ksymbol kernel_symbol_table[];
extern size_t kernel_symbol_table_length;

const struct ksymbol *ksymbol_find_by_value(void *addr, bool range);
const struct ksymbol *ksymbol_find_by_name(const char *name);

