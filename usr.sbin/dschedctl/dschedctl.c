/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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

#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/syslimits.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/dsched.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int dev_fd;


static void
usage(void)
{
	fprintf(stderr,
		"Usage: dschedctl <commands>\n"
		"Valid commands are:\n"
		" -l [-d <disk>]\n"
		"\t Lists all disks and their policies, or just for <disk>\n"
		" -p\n"
		"\t Lists all available I/O scheduling policies\n"
		" -s <policy> -d <disk>\n"
		"\t Switches the policy of the disk specified with -d to <policy>\n"
		"\n"
		"Valid options and its arguments are:\n"
		" -d <disk>\n"
		"\t Specifies the disk to be used (for -l and -s)\n"
		);

	exit(1);
}


static int
dsched_ioctl(unsigned long cmd, struct dsched_ioctl *pdioc)
{
	if (ioctl(dev_fd, cmd, pdioc) == -1)
		err(1, "ioctl");

	return 0;
}


int main(int argc, char *argv[])
{
	struct dsched_ioctl	dioc;
	char	*disk_name = NULL;
	char	*policy = NULL;
	int	dflag = 0, lflag = 0, pflag = 0, sflag = 0;
	int	ch, error;

	while ((ch = getopt(argc, argv, "d:lps:")) != -1) {
		switch (ch) {
		case 'd':
			dflag = 1;
			disk_name = optarg;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'p':
			pflag = 1;
			break;
		case 's':
			sflag = 1;
			policy = optarg;
			break;
		case 'h':
		case '?':
		default:
			usage();
			/* NOT REACHED */
		}
	}

	argc -= optind;
	argv += optind;

	/*
	 * Check arguments:
	 * - need to use at least one mode
	 * - can not use -s without -d
	 */
	if (!(lflag || pflag || sflag) ||
	    (sflag && (!dflag))) {
		usage();
		/* NOT REACHED */
	}

	dev_fd = open("/dev/dsched", O_RDWR);
	if (dev_fd == -1)
		err(1, "open(/dev/dsched)");

	if (lflag) {
		if (dflag) {
			strncpy(dioc.dev_name, disk_name, DSCHED_NAME_LENGTH);
			error = dsched_ioctl(DSCHED_LIST_DISK, &dioc);
			if (!error) {
				printf("%s\t=>\t%s\n", disk_name, dioc.pol_name);
			}
		} else {
			dioc.num_elem = 0;
			while(ioctl(dev_fd, DSCHED_LIST_DISKS, &dioc) != -1) {
				++dioc.num_elem;
				printf("%s\t=>\t%s\n", dioc.dev_name, dioc.pol_name);
			}
		}
	}

	if (pflag) {
		dioc.num_elem = 0;
		while(ioctl(dev_fd, DSCHED_LIST_POLICIES, &dioc) != -1) {
			++dioc.num_elem;
			printf("\t>\t%s\n", dioc.pol_name);
		}
	}

	if (sflag) {
		strncpy(dioc.dev_name, disk_name, DSCHED_NAME_LENGTH);
		strncpy(dioc.pol_name, policy, DSCHED_NAME_LENGTH);
		error = dsched_ioctl(DSCHED_SET_DEVICE_POLICY, &dioc);
		if (!error) {
			printf("Switched scheduler policy of %s successfully to %s\n",
			    disk_name, policy);
		}
	}

	close(dev_fd);

	return 0;
}
