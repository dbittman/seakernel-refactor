#include <sys.h>
#include <string.h>
struct utsname
{
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

int sys_uname(struct utsname *buf)
{
	strncpy(buf->sysname, "SeaOS", 64);
	strncpy(buf->nodename, "something", 64);
	strncpy(buf->release, "0.4", 64);
	strncpy(buf->version, "SMP #1", 64);
	strncpy(buf->machine, "x86_64", 64); //TODO

	return 0;
}

