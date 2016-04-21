#pragma once

#define PH_LOAD 1
#define PH_INTERP	3
#define ET_EXEC		2
#define ET_DYN		3
#define ELF_PF_R 4
#define ELF_PF_W 2
#define ELF_PF_X 1

#define AT_NULL 0
#define AT_IGNORE 1
#define AT_EXECFD 2
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_FLAGS 8
#define AT_ENTRY 9

#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_PLATFORM 15


struct elf_header {
	uint8_t ident[16];
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint64_t entry;
	uint64_t phoff;
	uint64_t shoff;
	uint32_t flags;
	uint16_t size;
	uint16_t phsize;
	uint16_t phnum;
	uint16_t shsize;
	uint16_t shnum;
	uint16_t strndx;
} __attribute__((packed));

struct elf_section_header {
	uint32_t name;
	uint32_t type;
	uint64_t flags;
	uint64_t address;
	uint64_t offset;
	uint64_t size;
	uint32_t link;
	uint32_t info;
	uint64_t alignment;
	uint64_t sect_size;
} __attribute__((packed));

struct elf_program_header
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_addr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
} __attribute__((packed));

int elf_parse_executable(struct elf_header *header, int fd, uintptr_t *max, uintptr_t *, uintptr_t *);
intptr_t elf_check_interp(struct elf_header *header, int fd, uintptr_t *);
