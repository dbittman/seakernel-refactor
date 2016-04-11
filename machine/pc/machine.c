#include <multiboot.h>
#include <stddef.h>
#include <panic.h>
#include <mmu.h>
#include <printk.h>
#include <string.h>
#include <system.h>
#include <machine/machine.h>
static struct multiboot *multiboot;

void pc_save_multiboot(void *header)
{
	multiboot = header;
}

struct boot_modules_info {
	long count;
	struct boot_module modules[];
};

static uintptr_t bootmods_start = 0;
static uintptr_t bootmods_infos = 0;

struct boot_module *machine_get_boot_module(int i)
{
	struct boot_modules_info *bmi = (void *)bootmods_infos;
	if(i >= bmi->count)
		return NULL;
	return &bmi->modules[i];
}

void machine_init(void)
{
	/* assume that the modules passed to us are vaguely in the same
	 * place. */
	if(!(multiboot->flags & MULTIBOOT_FLAG_MODS))
		return;
	printk("Parsing %d boot modules\n", multiboot->mods_count);
	uintptr_t min = multiboot->mods_addr, minpage;
	uintptr_t max = min, maxpage;
	size_t datalength = 0;
	struct mboot_module *mods = (void *)((uintptr_t)multiboot->mods_addr);
	struct mboot_module saved_mod_infos[multiboot->mods_count];
	char saved_names[multiboot->mods_count][128];
	for(uint32_t i=0;i<multiboot->mods_count;i++, mods++) {
		if(mods->start < min)
			min = mods->start;
		if(mods->end > max)
			max = mods->end;
		memcpy(saved_names[i], (void *)(uintptr_t)mods->string, 128);
		datalength += (mods->end - mods->start) + sizeof(struct boot_module);
		memcpy(&saved_mod_infos[i], mods, sizeof(*mods));
	}
	minpage = (min & ~(0x1000 - 1));
	maxpage = (max & ~(0x1000 - 1)) + 0x1000;
	printk("Allocating %ld KB (%lx - %lx) for boot modules\n", (maxpage - minpage) / 1024, min, max);

	bootmods_start = mm_physical_allocate(maxpage - minpage, false);
	memmove((void *)bootmods_start, (void *)minpage, (maxpage - minpage));

	bootmods_infos = mm_physical_allocate(__round_up_pow2(multiboot->mods_count * sizeof(struct boot_module)), true);

	struct boot_modules_info *bmi = (void *)(bootmods_infos);
	bmi->count = multiboot->mods_count;

	for(int i=0;i<bmi->count;i++) {
		bmi->modules[i].start = (saved_mod_infos[i].start - minpage) + bootmods_start + PHYS_MAP_START;
		bmi->modules[i].length = saved_mod_infos[i].end - saved_mod_infos[i].start;
		memcpy(bmi->modules[i].name, saved_names[i], 128);
	}
}

size_t machine_get_memlen(void)
{
	if(!(multiboot->flags & MULTIBOOT_FLAG_MEM))
		panic(0, "don't know how to detect memory!");
	return multiboot->mem_upper * 1024 - PHYS_MEMORY_START;
}

