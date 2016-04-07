#include <stddef.h>
#include <processor.h>
#include <libfdt.h>
#include <assert.h>
static uint64_t memlen;
size_t machine_get_memlen(void)
{
	return memlen;
}

static void parse_device_tree(void)
{
	const void *fdt = (void *)KERNEL_VIRT_BASE;
    int err = fdt_check_header(fdt);
	if(err >= 0) {
		int depth = 0;
		int offset = 0;
		for (;;) {
	    	offset = fdt_next_node(fdt, offset, &depth);
	    	if (offset < 0)
	        	break;

	    	/* get the name */
	    	const char *name = fdt_get_name(fdt, offset, NULL);
	    	if (!name)
	        	continue;
			if (strcmp(name, "memory") == 0) {
                int lenp;
                const void *prop_ptr = fdt_getprop(fdt, offset, "reg", &lenp);
                if (prop_ptr && lenp == 0x10) {
                    /* we're looking at a memory descriptor */
                    uint64_t len = fdt64_to_cpu(*((const uint64_t *)prop_ptr + 1));
					memlen = len;
                    /* trim size on certain platforms */
				}
			} else if(!strcmp(name, "psci")) {
                int lenp;
                const void *prop_ptr = fdt_getprop(fdt, offset, "method", &lenp);
                assert(strcmp(prop_ptr, "hvc") == 0);
			} else if (!strncmp(name, "cpu@", 4)) {
				int lenp;
				const void *prop_ptr = fdt_getprop(fdt, offset, "reg", &lenp);
				int id = *((unsigned int *)prop_ptr) >> 24;
				processor_create(id, id == 0 ? PROCESSOR_UP : 0);

			}
		}
	} else {
		panic(0, "failed to parse device tree");
	}
}

void machine_init(void)
{
	parse_device_tree();
}

