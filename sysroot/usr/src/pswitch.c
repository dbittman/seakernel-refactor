#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string.h>

void *rxbuf, *txbuf, *ctlbuf;

int main(int argc, char **argv)
{
	int rfd = open("/dev/lo", O_RDWR);
	rxbuf = mmap(NULL, 0x1000 * 32, PROT_READ, MAP_PRIVATE, rfd, 0);
	txbuf = mmap(NULL, 0x1000 * 32, PROT_READ | PROT_WRITE, MAP_PRIVATE, rfd, 0x1000);
	ctlbuf = mmap(NULL, 0x1000 * 32, PROT_READ | PROT_WRITE, MAP_PRIVATE, rfd, 0x2000);

	strcpy(txbuf, "Hello, World!");
	size_t pos = 1;
	ioctl(rfd, 1, &pos);

	size_t rp = 0;
	while(rp == 0) {
		ioctl(rfd, 2, &rp);
		printf(":: %ld\n", rp);
		printf("-> %s\n", (char *)rxbuf);
	}
}

