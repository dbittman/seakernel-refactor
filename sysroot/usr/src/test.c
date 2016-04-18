#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
int main()
{
	int x = open("/dev/com0", O_RDWR);
	FILE *f = fopen("/dev/com0", "w");
	while(1) {
		fprintf(f, "Test! %d\n", x);
		fprintf(stderr, "Test! %d\n", x);
		fflush(f);

		sleep(1);
	}
}

