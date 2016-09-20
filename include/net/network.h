#pragma once

#include <fs/socket.h>

int net_network_send(struct socket *, const struct sockaddr *dest, const void *trheader, size_t thlen, const void *msg, size_t mlen, int prot, int checksum_offset);
