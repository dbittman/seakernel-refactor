#include <lib/elf.h>
#include <fs/sys.h>
#include <sys.h>
#include <map.h>
#include <mmu.h>
#include <printk.h>
#include <string.h>
int elf_parse_executable(struct elf_header *header, int fd, uintptr_t *max, uintptr_t *phdr)
{
	int r;
	char buffer[header->phnum * header->phsize];
	if((r = sys_pread(fd, buffer, sizeof(buffer), header->phoff)) < 0)
		return r;
	*max = 0;
	for(int i=0;i<header->phnum;i++) {
		struct elf_program_header *ph = (void *)(buffer + (i*header->phsize));

		if(ph->p_addr + ph->p_memsz > *max)
			*max = ph->p_addr + ph->p_memsz;

		if(ph->p_offset <= header->phoff && ph->p_offset + ph->p_filesz > header->phoff)
			*phdr = ph->p_addr + (header->phoff - ph->p_offset);

		if(ph->p_type == PH_LOAD) {
		
			size_t additional = ph->p_memsz - ph->p_filesz;
            size_t inpage_offset = ph->p_addr & (arch_mm_page_size(0) - 1);
            uintptr_t newend = (ph->p_addr + ph->p_filesz);
            size_t page_free = arch_mm_page_size(0) - (newend % arch_mm_page_size(0));

            int prot = 0;
            if(ph->p_flags & ELF_PF_R)
                prot |= PROT_READ;
            if(ph->p_flags & ELF_PF_W)
                prot |= PROT_WRITE;
            if(ph->p_flags & ELF_PF_X)
                prot |= PROT_EXEC;

			int flags = MMAP_MAP_FIXED;
			if(prot & PROT_WRITE)
				flags |= MMAP_MAP_PRIVATE;
			else
				flags |= MMAP_MAP_SHARED;
			/* TODO: we should have macros for this */
			sys_mmap(ph->p_addr & ~(arch_mm_page_size(0) - 1), ph->p_filesz + inpage_offset,
					prot, flags, fd, ph->p_offset & ~(arch_mm_page_size(0) - 1));
			if(additional > page_free) {
				sys_mmap(((newend - 1) & ~(arch_mm_page_size(0) - 1)) + arch_mm_page_size(0),
						additional - page_free, prot, flags | MMAP_MAP_ANON, -1, 0);
			}
			memset((void *)newend, 0, additional);
		}

	}
	if(!*max)
		return -1;
	*max = ((*max - 1) & ~(arch_mm_page_size(0) - 1)) + arch_mm_page_size(0);

	return 0;
}

intptr_t elf_load_interp(char *path, uintptr_t *entry)
{
	/* hack */
	path = "/libc.so";
	int fd = sys_open(path, O_RDWR, 0);
	if(!fd)
		return -1;

	struct elf_header header;
	sys_pread(fd, &header, sizeof(header), 0);

	*entry = header.entry;
	/* TODO: verify executable */

	assert(header.type == ET_DYN);

	char buffer[header.phnum * header.phsize];
	sys_pread(fd, buffer, sizeof(buffer), header.phoff);

	uintptr_t base = 0;
	for(int i=0;i<header.phnum;i++) {
		struct elf_program_header *ph = (void *)(buffer + (i*header.phsize));
		if(ph->p_type == PH_LOAD) {
			size_t additional = ph->p_memsz - ph->p_filesz;
            size_t inpage_offset = ph->p_addr & (arch_mm_page_size(0) - 1);
            uintptr_t newend = (ph->p_addr + ph->p_filesz);
            size_t page_free = arch_mm_page_size(0) - (newend % arch_mm_page_size(0));

            int prot = 0;
            if(ph->p_flags & ELF_PF_R)
                prot |= PROT_READ;
            if(ph->p_flags & ELF_PF_W)
                prot |= PROT_WRITE;
            if(ph->p_flags & ELF_PF_X)
                prot |= PROT_EXEC;

			int flags = 0;
			if(prot & PROT_WRITE)
				flags |= MMAP_MAP_PRIVATE;
			else
				flags |= MMAP_MAP_SHARED;
			/* TODO: we should have macros for this */
			uintptr_t v = sys_mmap((ph->p_addr & page_mask(0)) + base, ph->p_filesz + inpage_offset,
					prot, flags, fd, ph->p_offset & page_mask(0));
			printk("Mapping %lx (%lx) %lx\n", ph->p_addr + base, base, v);
			if(additional > page_free) {
				sys_mmap(((newend - 1) & page_mask(0)) + arch_mm_page_size(0) + base,
						additional - page_free, prot, flags | MMAP_MAP_ANON, -1, 0);
			}
			memset((void *)newend + base, 0, additional);
			if(base == 0)
				base = v;
		}
	}
	return base;
}

intptr_t elf_check_interp(struct elf_header *header, int fd, uintptr_t *entry)
{
	int r;
	char buffer[header->phnum * header->phsize];
	if((r = sys_pread(fd, buffer, sizeof(buffer), header->phoff)) < 0)
		return r;
	for(int i=0;i<header->phnum;i++) {
		struct elf_program_header *ph = (void *)(buffer + (i*header->phsize));
		if(ph->p_type == PH_INTERP) {
			char interp_string[256];
			sys_pread(fd, interp_string, sizeof(interp_string), ph->p_offset);
			interp_string[ph->p_filesz - 1] = 0;
			printk("interpreter: %s\n", interp_string);
			return elf_load_interp(interp_string, entry);
		}
	}
	return 0;
}

