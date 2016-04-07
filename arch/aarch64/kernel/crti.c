/* crti.c for ARM - BPABI - use -std=c99 */
typedef void (*func_ptr)(void);
#include <printk.h>
extern func_ptr _init_array_start, _init_array_end;
void _init(void)
{
	/* _init is called by main (which, if you know about how a C program usually
	 * initializes) is pretty funny. But here, we want to be able to allocate
	 * memory inside constructor functions, so we need to wait until we have a memory
	 * manager.
	 *
	 * Anyway. This function calls constructor functions. This is handled with a little
	 * bit of linker magic - we let the linker script tell us where this section is
	 * so that we can iterate over the array and call the functions.
	 */
	for ( func_ptr* func = &_init_array_start; func != &_init_array_end; func++ ) {
		(*func)();
	}
}

