#include <ksymbol.h>
#include <system.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
/* we can't to bounds checking with ubsan here because we'll be replacing these
 * symbols with real ones later, and it'll confuse the compiler */

extern size_t kernel_symbol_table_length ;
extern const struct ksymbol kernel_symbol_table[];

#ifdef __clang__
__attribute__((no_sanitize("bounds")))
#else
__attribute__((no_sanitize_undefined))
#endif
const struct ksymbol *ksymbol_find_by_name(const char *name)
{
	for(size_t i=0;i<kernel_symbol_table_length;i++) {
		if(!strcmp(kernel_symbol_table[i].name, name))
			return &kernel_symbol_table[i];
	}
	return NULL;
}
#include <printk.h>
#ifdef __clang__
__attribute__((no_sanitize("bounds")))
#else
__attribute__((no_sanitize_undefined))
#endif
const struct ksymbol *ksymbol_find_by_value(void *addr, bool range)
{
	for(size_t i=0;i<kernel_symbol_table_length;i++) {
		const struct ksymbol *sym = &kernel_symbol_table[i];
		if(sym->value == (uintptr_t)addr
				|| (range 
					&& ((uintptr_t)addr >= sym->value 
					&& (uintptr_t)addr < (sym->value + sym->size)))) {
			return sym;
		}
	}
	return NULL;
}

