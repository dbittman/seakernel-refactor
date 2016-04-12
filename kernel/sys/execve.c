#include <fs/sys.h>
#include <lib/elf.h>
#include <string.h>
#include <thread.h>
#include <process.h>
#include <printk.h>

static void write_data(uintptr_t *end, void *data, size_t len)
{
	*end -= len;
	memcpy((void *) *end, data, len);
}

static void write_aux(uintptr_t *end, long key, long value)
{
	write_data(end, &value, sizeof(long));
	write_data(end, &key, sizeof(long));
}

int sys_execve(const char *path, char **arg, char **env)
{
	(void)arg;
	(void)env;
	int err = 0;
	int fd = sys_open(path, O_RDONLY, 0);
	if(fd < 0)
		return fd;

	struct elf_header header;
	if(sys_pread(fd, &header, sizeof(header), 0) != sizeof(header)) {
		err = -1;
		goto out_close;
	}

	if(memcmp(header.ident, "\177ELF", 3)) {
		err = -1;
		goto out_close;
	}

	/* other tests... */

	uintptr_t max, phdrs=0;
	if(elf_parse_executable(&header, fd, &max, &phdrs) < 0) {
		/* ...die */
	}

	process_close_files(current_thread->process, false);

	uintptr_t aux = (uintptr_t)current_thread->user_tls_base + USER_TLS_SIZE;

	write_aux(&aux, AT_NULL, AT_NULL);
	write_aux(&aux, AT_PAGESZ, arch_mm_page_size(0));
	write_aux(&aux, AT_EXECFD, fd);
	if(phdrs) {
		write_aux(&aux, AT_PHDR, phdrs);
		write_aux(&aux, AT_PHENT, header.phsize);
		write_aux(&aux, AT_PHNUM, header.phnum);
	}
	write_aux(&aux, AT_ENTRY, header.entry);
	/* TODO: aux UID, etc */

	
	long nil = 0;
	write_data(&aux, &nil, sizeof(nil));
	write_data(&aux, &nil, sizeof(nil));

	long argc = 0;
	write_data(&aux, &argc, sizeof(long));
	arch_thread_usermode_jump(header.entry, aux);

out_close:
	sys_close(fd);
	return err;
}

