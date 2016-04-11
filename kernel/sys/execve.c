#include <fs/sys.h>
#include <lib/elf.h>
#include <string.h>
#include <thread.h>

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
		printk("here %d %c %c %c\n", fd, header.ident[0], header.ident[1], header.ident[2]);
		err = -1;
		goto out_close;
	}

	/* other tests... */

	uintptr_t max;
	if(elf_parse_executable(&header, fd, &max) < 0) {
		/* ...die */
	}

	arch_thread_usermode_jump(header.entry, (uintptr_t)current_thread->user_tls_base + USER_TLS_SIZE);

out_close:
	sys_close(fd);
	return err;
}

