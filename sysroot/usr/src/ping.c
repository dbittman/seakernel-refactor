#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <stddef.h>
#include <net/if.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <stdbool.h>
#include <math.h>

struct addrinfo *rai;
int sock;
unsigned sent = 0, received = 0, dups = 0;
int interval = 1000000;

uint32_t randomid = 0;

double mean = 0.f, stddev_r = 0.f, min = INFINITY, max = 0.f;

struct ping {
	uint32_t rand;
	uint32_t seq;
	struct timespec time;
};

struct req {
	uint32_t seq;
	bool outstanding;
	time_t time;
};

#define REQLEN 1024

static struct req reqs[REQLEN];

void usage(void)
{
	fprintf(stderr, "usage: ping [-h] target\n");
	fprintf(stderr, "Run a repeated ping against target.\n");
	exit(0);
}

void send_packet(void)
{
	struct sockaddr_in6 *sa = (void *)rai->ai_addr;
	char buf[1024];
	struct icmp6_hdr *header = (struct icmp6_hdr *)buf;
	header->icmp6_type = 128;
	header->icmp6_code = 0;
	header->icmp6_data32[0] = 0;
	struct ping *ping = (struct ping *)(buf + sizeof(*header));
	ping->rand = randomid;
	ping->seq = ++sent;
	reqs[ping->seq % REQLEN].seq = ping->seq;
	reqs[ping->seq % REQLEN].time = time(NULL);
	reqs[ping->seq % REQLEN].outstanding = true;
	clock_gettime(CLOCK_MONOTONIC, &ping->time);

	if(sendto(sock, buf, sizeof(*header) + sizeof(*ping), MSG_DONTWAIT, (struct sockaddr *)sa, sizeof(*sa)) < 0) {
		perror("sendto");
	}
}

void elapsed(struct timespec *start, struct timespec *stop)
{
	if ((stop->tv_nsec - start->tv_nsec) < 0) {
	    stop->tv_sec = stop->tv_sec - start->tv_sec - 1;
	    stop->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
	} else {
	    stop->tv_sec = stop->tv_sec - start->tv_sec;
	    stop->tv_nsec = stop->tv_nsec - start->tv_nsec;
	}
}

void gettime(const char **str, double *us)
{
	*str = "us";
	if(*us >= 100) {
		*us /= 1000;
		*str = "ms";
	}
	if(*us >= 100) {
		*us /= 1000;
		*str = "s";
	}
}

void process_packet(struct sockaddr_in6 *src, int r, struct ping *ping, struct timespec *stop, bool dup)
{
	double us;
	const char *dur;
	elapsed(&ping->time, stop);
	us = (double)stop->tv_nsec / 1000 + (double)stop->tv_sec * 1000000ul;

	if(!dup) {
		double pm = mean;
		mean += (us - mean) / (received - dups);
		stddev_r += (us - mean) * (us - pm);
		if(min > us)
			min = us;
		if(max < us)
			max = us;
	}

	gettime(&dur, &us);
	char buf[128];
	printf("%d bytes from %s (%s): seq=%d, time=%.1f%s%s\n", r, rai->ai_canonname, inet_ntop(AF_INET6, &src->sin6_addr, buf, 128), ping->seq, us, dur, dup ? " (DUP!)" : "");
}

void recv_packet(void)
{
	char buf[1024];
	struct sockaddr_in6 src;
	socklen_t srclen = sizeof(src);
	struct icmp6_hdr *header = (struct icmp6_hdr *)buf;
	struct ping *ping = (struct ping *)(buf + sizeof(*header));
	int r;
	struct timespec stop;

	while((r = recvfrom(sock, buf, 1024, 0, (struct sockaddr *)&src, &srclen)) <= 0
			|| header->icmp6_type != 129 || ping->rand != randomid);
	clock_gettime(CLOCK_MONOTONIC, &stop);
	received++;
	uint32_t seq = ping->seq;
	bool dup = false;
	if(reqs[seq % REQLEN].seq == seq) {
		if(reqs[seq % REQLEN].outstanding)
			reqs[seq % REQLEN].outstanding = false;
		else
			dups++, dup = true;
	}


	process_packet(&src, r, ping, &stop, dup);
}

void handler(int s)
{
	switch(s) {
		case SIGALRM:
			send_packet();
			break;
		default:
			exit(0);
	}
}

void print_stats(void)
{
	printf("--- %s ping statistics ---\n", rai->ai_canonname);
	printf("%d packets tx, %d packets rx, %.1f%% packet loss\n", sent, received, 100 * (1.f - ((float)sent / received)));
	const char *med, *std, *mad, *mid;
	gettime(&med, &mean);
	double stddev = sqrt(stddev_r / (received - dups));
	gettime(&std, &stddev);
	gettime(&mad, &min);
	gettime(&mid, &max);
	printf("rtt min/avg/max/mdev = %.3f%s / %.3f%s / %.3f%s / %.3f%s\n", min, mid, mean, med, max, mad, stddev, std);
}

int main(int argc, char **argv)
{
	int c;
	while((c = getopt(argc, argv, "h")) != -1) {
		switch(c) {
		case 'h':
			usage();
			break;
		}
	}

	if(optind >= argc)
		usage();
	char *target = argv[optind];

	/* init random id */
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	srandom((ts.tv_nsec << 8) + ts.tv_sec);
	randomid = random();

	struct addrinfo hints = {
		.ai_family = AF_INET6,
		.ai_flags = AI_CANONNAME | AI_V4MAPPED,
	};

	int s;
	if((s=getaddrinfo(target, NULL, &hints, &rai)) < 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(1);
	}

	struct sockaddr_in6 *sa = (void *)rai->ai_addr;
	char buf[128];

	if((sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)) == -1) {
		perror("socket");
		exit(1);
	}

	int csoff = offsetof(struct icmp6_hdr, icmp6_cksum);
	if(setsockopt(sock, SOL_RAW, IPV6_CHECKSUM, &csoff, sizeof(csoff)) == -1) {
		perror("setsockopt.checksum");
		exit(1);
	}
	
	/* handle alarms */
	signal(SIGALRM, handler);
	signal(SIGINT, handler);
	struct itimerval itv = {
		.it_interval = { .tv_sec = interval / 1000000, .tv_usec = interval % 1000000 },
		.it_value = { .tv_sec = interval / 1000000, .tv_usec = interval % 1000000 },
	};


	printf("PING %s: %s (%s)\n", target, rai->ai_canonname, inet_ntop(AF_INET6, &sa->sin6_addr, buf, 128));
	send_packet();
	atexit(print_stats);
	setitimer(ITIMER_REAL, &itv, NULL);
	while(true)
		recv_packet();
}

