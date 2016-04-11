#include <fs/sys.h>
#include <lib/elf.h>
#include <string.h>
#include <thread.h>
#include <process.h>
#include <printk.h>
int sys_execve(const char *path, char **arg, char **env)
{
	(void)arg;
	(void)env;
	int err = 0;
	int fd = sys_open(path, O_RDONLY, 0);
	if(fd < 0)
		return fd;

	struct elf_header header;
	if(sys_pread(fd, 0, &header, sizeof(header)) != sizeof(header)) {
		err = -1;
		goto out_close;
	}

	if(memcmp(header.ident, "\177ELF", 3)) {
		err = -1;
		goto out_close;
	}

	/* other tests... */

	uintptr_t max;
	if(elf_parse_executable(&header, fd, &max) < 0) {
		/* ...die */
	}

	sys_close(fd);

	process_close_files(current_thread->process, false);

	arch_thread_usermode_jump(header.entry, (uintptr_t)current_thread->user_tls_base + USER_TLS_SIZE);

out_close:
	sys_close(fd);
	return err;
}

