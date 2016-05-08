#include <fs/sys.h>
#include <lib/elf.h>
#include <string.h>
#include <thread.h>
#include <process.h>
#include <printk.h>
#include <errno.h>
#include <sys.h>
#include <file.h>
#include <arena.h>
#include <fs/stat.h>
#include <fcntl.h>
#include <processor.h>

static char *write_data(uintptr_t *end, void *data, size_t len)
{
	*end -= len;
	memcpy((void *) *end, data, len);
	return (char *)(*end);
}

static void write_aux(uintptr_t *end, long key, long value)
{
	write_data(end, &value, sizeof(long));
	write_data(end, &key, sizeof(long));
}

sysret_t sys_execve(const char *path, char **arg, char **env)
{
	int err = 0;
	int fd = sys_open(path, O_RDONLY, 0);
	if(fd < 0)
		return fd;

	struct elf_header header;
	if(sys_pread(fd, &header, sizeof(header), 0) != sizeof(header)) {
		err = -ENOEXEC;
		goto out_close;
	}

	if(memcmp(header.ident, "\177ELF", 3)) {
		err = -ENOEXEC;
		goto out_close;
	}

	printk("**** EXECVE: p%d t%ld, c%d - %s\n", current_thread->process->pid, current_thread->tid, current_thread->processor->id, path);
	/* set-id */
	struct file *file = process_get_file(fd);
	struct inode *node = file_get_inode(file);
	kobj_putref(file);

	if(node->mode & S_ISUID) {
		current_thread->process->suid = current_thread->process->euid;
		current_thread->process->euid = node->uid;
	}
	if(node->mode & S_ISGID) {
		current_thread->process->sgid = current_thread->process->egid;
		current_thread->process->egid = node->gid;
	}

	inode_put(node);

	/* other tests... */

	char proc_exe[128];
	snprintf(proc_exe, 128, "/proc/%d/exe", current_thread->process->pid);
	sys_unlink(proc_exe);
	sys_symlink(path, proc_exe);

	/* backup */
	struct arena arena;
	arena_create(&arena);
	long argc = 0;
	char **savedargv = arena_allocate(&arena, sizeof(char *) * 4096);
	for(int i=0;i<4095 && arg && arg[i];i++) {
		int len = strlen(arg[i]);
		char *space = arena_allocate(&arena,len+1);
		memcpy(space, arg[i], len+1);
		savedargv[i] = space;
		argc++;
	}
	savedargv[argc] = NULL;
	int envc = 0;
	char **savedenv = arena_allocate(&arena, sizeof(char *) * 4096);
	for(int i=0;i<4095 && env && env[i];i++) {
		int len = strlen(env[i]);
		char *space = arena_allocate(&arena,len+1);
		memcpy(space, env[i], len+1);
		savedenv[i] = space;
		envc++;
	}
	savedenv[envc] = NULL;
	
	process_remove_mappings(current_thread->process, false);

	intptr_t base = 0;
	uintptr_t interp_entry;
	base = elf_check_interp(&header, fd, &interp_entry);
	if(base < 0) {
		err = -ELIBBAD;
		goto out_close;
	}

	uintptr_t max, phdrs=0;
	uintptr_t exe_base = 0;
	if(elf_parse_executable(&header, fd, &max, &phdrs, &exe_base) < 0) {
		sys_exit(-ENOEXEC);
		/* ...die */
	}

	if(header.type == ET_DYN) {
		base = exe_base;
		interp_entry = header.entry;
	}

	/* TODO: mark executable as "busy" */

	current_thread->process->brk = (max + arch_mm_page_size(0)) & page_mask(0);

	process_close_files(current_thread->process, false);
	memset(current_thread->process->actions, 0, sizeof(current_thread->process->actions));

	current_thread->user_tls_base = (void *)process_allocate_user_tls(current_thread->process);
	uintptr_t aux = (uintptr_t)current_thread->user_tls_base + USER_TLS_SIZE;

	for(int i=0;i<argc;i++) {
		savedargv[i] = write_data(&aux, savedargv[i], strlen(savedargv[i]) + 1);
	}

	for(int i=0;i<envc;i++) {
		savedenv[i] = write_data(&aux, savedenv[i], strlen(savedenv[i]) + 1);
	}

	/* align aux */
	aux &= 0xFFFFFFFFFFFFFFF0;

	write_aux(&aux, AT_NULL, AT_NULL);
	write_aux(&aux, AT_PAGESZ, arch_mm_page_size(0));
	sys_fcntl(fd, F_SETFD, FD_CLOEXEC);
	write_aux(&aux, AT_EXECFD, fd);
	if(phdrs) {
		write_aux(&aux, AT_PHDR, phdrs);
		write_aux(&aux, AT_PHENT, header.phsize);
		write_aux(&aux, AT_PHNUM, header.phnum);
	}
	if(base > 0)
		write_aux(&aux, AT_BASE, base);
	write_aux(&aux, AT_ENTRY, header.entry);
	/* TODO: aux UID, etc */

	for(int i=envc;i>=0;i--) {
		write_data(&aux, &savedenv[i], sizeof(char *));
	}

	for(int i=argc;i>=0;i--) {
		write_data(&aux, &savedargv[i], sizeof(char *));
	}
	
	write_data(&aux, &argc, sizeof(long));
	arch_thread_usermode_jump(base == 0 ? header.entry : interp_entry + base, aux);

out_close:
	sys_close(fd);
	return err;
}

