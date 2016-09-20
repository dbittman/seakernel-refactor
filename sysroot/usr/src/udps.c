
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#define MAXBUF 65536

int main()
{
   int sock;
   int status;
   struct sockaddr_in6 sin6;
   int sin6len;
   char buffer[MAXBUF];

   sock = socket(PF_INET6, SOCK_DGRAM,0);

   sin6len = sizeof(struct sockaddr_in6);
   printf(":: %d %d %d %d\n", sin6len, sizeof(sa_family_t), sizeof(struct in6_addr), sizeof(in_port_t));

   memset(&sin6, 0, sin6len);

   /* just use the first address returned in the structure */

   sin6.sin6_port = htons(1234);
   sin6.sin6_family = AF_INET6;
   sin6.sin6_addr = in6addr_any;

   struct ifreq ifr;
   memset(&ifr, 0, sizeof(ifr));
   snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "nic0");
   if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr)) < 0) {
	   perror(":(");
   }


   status = bind(sock, (struct sockaddr *)&sin6, sin6len);
   if(-1 == status)
     perror("bind"), exit(1);

   status = getsockname(sock, (struct sockaddr *)&sin6, &sin6len);

   printf("%d\n", ntohs(sin6.sin6_port));

   while(1) {
   	   status = recvfrom(sock, buffer, MAXBUF, 0, 
               (struct sockaddr *)&sin6, &sin6len);
   	   printf("buffer : %s\n", buffer);
   	   const char *res = "This is a response!\n";
   	   sendto(sock, res, strlen(res), 0, (struct sockaddr *)&sin6, sin6len);
   }
   shutdown(sock, 2);
   close(sock);
   return 0;
}

