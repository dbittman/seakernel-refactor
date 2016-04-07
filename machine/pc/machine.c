#include <multiboot.h>
#include <stddef.h>
#include <panic.h>
#include <mmu.h>
static struct multiboot *multiboot;

void pc_save_multiboot(void *header)
{
	multiboot = header;
}

void machine_init(void)
{

}

size_t machine_get_memlen(void)
{
	if(!(multiboot->flags & MULTIBOOT_FLAG_MEM))
		panic(0, "don't know how to detect memory!");
	return multiboot->mem_upper * 1024 - PHYS_MEMORY_START;
}

