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
	if((r = sys_pread(fd, header->phoff, buffer, sizeof(buffer))) < 0)
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

