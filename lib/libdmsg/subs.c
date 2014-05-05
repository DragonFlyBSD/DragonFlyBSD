/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "dmsg_local.h"

/*
 * Allocation wrappers give us shims for possible future use
 */
void *
dmsg_alloc(size_t bytes)
{
	void *ptr;

	ptr = malloc(bytes);
	assert(ptr);
	bzero(ptr, bytes);
	return (ptr);
}

void
dmsg_free(void *ptr)
{
	free(ptr);
}

const char *
dmsg_uuid_to_str(uuid_t *uuid, char **strp)
{
	uint32_t status;
	if (*strp) {
		free(*strp);
		*strp = NULL;
	}
	uuid_to_string(uuid, strp, &status);
	return (*strp);
}

const char *
dmsg_peer_type_to_str(uint8_t type)
{
	switch(type) {
	case DMSG_PEER_NONE:
		return("NONE");
	case DMSG_PEER_CLUSTER:
		return("CLUSTER");
	case DMSG_PEER_BLOCK:
		return("BLOCK");
	case DMSG_PEER_HAMMER2:
		return("HAMMER2");
	default:
		return("?PEERTYPE?");
	}
}

int
dmsg_connect(const char *hostname)
{
	struct sockaddr_in lsin;
	struct hostent *hen;
	int fd;
	int opt;

	/*
	 * Acquire socket and set options
	 */
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "cmd_debug: socket(): %s\n",
			strerror(errno));
		return -1;
	}
	opt = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof opt);

	/*
	 * Connect to the target
	 */
	bzero(&lsin, sizeof(lsin));
	lsin.sin_family = AF_INET;
	lsin.sin_addr.s_addr = 0;
	lsin.sin_port = htons(DMSG_LISTEN_PORT);

	if (hostname) {
		hen = gethostbyname2(hostname, AF_INET);
		if (hen == NULL) {
			if (inet_pton(AF_INET, hostname, &lsin.sin_addr) != 1) {
				fprintf(stderr,
					"Cannot resolve %s\n", hostname);
				return -1;
			}
		} else {
			bcopy(hen->h_addr, &lsin.sin_addr, hen->h_length);
		}
	}
	if (connect(fd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
		close(fd);
		fprintf(stderr, "debug: connect failed: %s\n",
			strerror(errno));
		return -1;
	}
	return (fd);
}
