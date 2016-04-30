#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <pwd.h>
#include <stdint.h>
bool all = false;

static bool give_up;
#define HASH_SIZE (1 << 16)
static int seen[HASH_SIZE];

static bool saw_this_pid(int pid)
{
	pid++;
	if(give_up)
		return false;

	uint32_t idx = ((uint32_t)pid * (uint32_t)2654435761u);
	for(uint32_t i=0;i<HASH_SIZE;i++) {
		if(seen[(idx + i) % HASH_SIZE] == 0)
			return false;
		else if(seen[(idx + i) % HASH_SIZE] == pid)
			return true;
	}
	return false;
}

static void mark_seen(int pid)
{
	pid++;
	if(give_up)
		return;

	uint32_t idx = ((uint32_t)pid * (uint32_t)2654435761u);
	for(int i=0;i<HASH_SIZE;i++) {
		if(seen[(idx + i) % HASH_SIZE] == 0) {
			seen[(idx + i) % HASH_SIZE] = pid;
			return;
		}
	}
	give_up = true;
}

static int read_int(int pid, const char *field, int *res)
{
	char path[256];
	sprintf(path, "/proc/%d/%s", pid, field);

	FILE *f = fopen(path, "r");
	if(!f)
		return -1;
	if(fscanf(f, "%d", res) != 1)
		return -1;
	fclose(f);
	return 0;
}

static void print_proc(int pid)
{
	if(saw_this_pid(pid))
		return;
	int uid;
	if(read_int(pid, "uid", &uid) == -1)
		return;

	struct passwd *pw;
	pw = getpwuid(uid);

	mark_seen(pid);

	printf("%4d %s\n", pid, pw ? pw->pw_name : "???");
}

static void print_procs(void)
{
	DIR *dir = opendir("/proc");
	struct dirent *de;
	printf(" PID USER\n");
	while((de = readdir(dir))) {
		int pid, end;
		int r = sscanf(de->d_name, "%d%n", &pid, &end);
		if(r == 1 && end == strlen(de->d_name)) {
			print_proc(pid);
		}
	}
}

int main(int argc, char **argv)
{
	int c;
	while((c = getopt(argc, argv, "a")) != EOF) {
		switch(c) {
			case 'a':
				all = true;
		}
	}
	
	print_procs();

	return 0;
}

