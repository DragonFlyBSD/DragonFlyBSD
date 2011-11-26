/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/test/syslink/dummyfs.c,v 1.1 2007/05/28 05:28:12 dillon Exp $
 */

#include <sys/types.h>
#include <sys/syslink.h>
#include <sys/syslink_msg.h>
#include <sys/syslink_vfs.h>
#include <vfs/userfs/userfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static void usage(void);

int VerboseOpt;

int
main(int ac, char **av)
{
	struct userfs_mount_info info;
	const char *mountpt;
	int ch;

	while ((ch = getopt(ac, av, "v")) != -1) {
		switch(ch) {
		case 'v':
			++VerboseOpt;
			break;
		default:
			usage();
		}
	}
	ac -= optind;
	av += optind;
	if (ac == 0)
		usage();
	mountpt = av[0];

	bzero(&info, sizeof(info));
	info.cfd = -1;
	if (mount("userfs", mountpt, 0, &info) < 0) {
		fprintf(stderr, "Unable to mount: %s\n", strerror(errno));
	}
	printf("mount successful %d\n", info.cfd);
	return(0);
}

static
void
usage(void)
{
	fprintf(stderr, "dummyfs [-v] mountpt\n");
	exit(1);
}

