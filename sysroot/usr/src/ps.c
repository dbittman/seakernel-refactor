#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <pwd.h>
bool all = false;

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
	int uid;
	if(read_int(pid, "uid", &uid) == -1)
		return;

	struct passwd *pw;
	pw = getpwuid(uid);

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

