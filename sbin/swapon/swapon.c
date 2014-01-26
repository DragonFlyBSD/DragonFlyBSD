/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) Copyright (c) 1980, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)swapon.c	8.1 (Berkeley) 6/5/93
 * $FreeBSD: src/sbin/swapon/swapon.c,v 1.8.2.2 2001/07/30 10:30:11 dd Exp $
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/diskslice.h>
#include <sys/ioctl_compat.h>
#include <vm/vm_param.h>

#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libutil.h>

static void usage(void);
static int swap_on_off(char *name, int doingall, int trim, int ask);
static void swaplist(int lflag, int sflag, int hflag);

enum { SWAPON, SWAPOFF, SWAPCTL } orig_prog, which_prog = SWAPCTL;

int
main(int argc, char **argv)
{
	struct fstab *fsp;
	char *ptr;
	int ret;
	int ch;
	int doall, sflag, lflag, hflag, qflag, eflag, iflag;

	if ((ptr = strrchr(argv[0], '/')) == NULL)
		ptr = argv[0];
	if (strstr(ptr, "swapon"))
		which_prog = SWAPON;
	else if (strstr(ptr, "swapoff"))
		which_prog = SWAPOFF;
	orig_prog = which_prog;

	sflag = lflag = hflag = qflag = doall = eflag = iflag = 0;
	while ((ch = getopt(argc, argv, "AadeghiklmqsU")) != -1) {
		switch((char)ch) {
		case 'A':
			if (which_prog == SWAPCTL) {
				doall = 1;
				which_prog = SWAPON;
			} else {
				usage();
			}
			break;
		case 'a':
			if (which_prog == SWAPON || which_prog == SWAPOFF)
				doall = 1;
			else
				which_prog = SWAPON;
			break;
		case 'd':
			if (which_prog == SWAPCTL)
				which_prog = SWAPOFF;
			else
				usage();
			break;
		case 'e':
			eflag = 1;
			break;
		case 'g':
			hflag = 'G';
			break;
		case 'h':
			hflag = 'H';
			break;
		case 'i':
			iflag = 1;
			break;
		case 'k':
			hflag = 'K';
			break;
		case 'l':
			lflag = 1;
			break;
		case 'm':
			hflag = 'M';
			break;
		case 'q':
			if (which_prog == SWAPON || which_prog == SWAPOFF)
				qflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 'U':
			if (which_prog == SWAPCTL) {
				doall = 1;
				which_prog = SWAPOFF;
			} else {
				usage();
			}
			break;
		case '?':
		default:
			usage();
		}
	}
	argv += optind;

	ret = 0;
	if (which_prog == SWAPON || which_prog == SWAPOFF) {
		if (doall) {
			while ((fsp = getfsent()) != NULL) {
				if (strcmp(fsp->fs_type, FSTAB_SW))
					continue;
				if (strstr(fsp->fs_mntops, "noauto"))
					continue;
				if (swap_on_off(fsp->fs_spec, 1, eflag, iflag)) {
					ret = 1;
				} else {
					if (!qflag) {
						printf("%s: %sing %s as swap device\n",
						    getprogname(),
						    which_prog == SWAPOFF ? "remov" : "add",
						    fsp->fs_spec);
					}
				}
			}
		} else if (*argv == NULL) {
			usage();
		}
		for (; *argv; ++argv) {
			if (swap_on_off(getdevpath(*argv, 0), 0, eflag, iflag)) {
				ret = 1;
			} else if (orig_prog == SWAPCTL) {
				printf("%s: %sing %s as swap device\n",
				    getprogname(),
				    which_prog == SWAPOFF ? "remov" : "add",
				    *argv);
			}
		}
	} else {
		if (lflag || sflag)
			swaplist(lflag, sflag, hflag);
		else
			usage();
	}
	exit(ret);
}

/*
 * TRIM the device
 */
static
void
trim_volume(char * name)
{	
	struct partinfo pinfo;
	int fd,i,n;
	size_t bytes = 0,ksize;
	char *xswbuf;
	struct xswdev *xsw;


	/*
	* Determine if this device is already being used by swap without 
	* calling swapon(). 
	*/
	if ((sysctlbyname("vm.swap_info_array", NULL, &bytes, NULL, 0) < 0) ||
	    bytes == 0) {
		err(1, "sysctlbyname()");
	}

	xswbuf = malloc(bytes);
	if ((sysctlbyname("vm.swap_info_array", xswbuf, &bytes, NULL, 0) < 0) ||
	    bytes == 0) {
			free(xswbuf);
			err(1, "sysctlbyname()");
	}

	ksize = ((struct xswdev *)xswbuf)->xsw_size;
	n = (int)(bytes / ksize);
	for (i = 0; i < n; ++i) {
		xsw = (void *)((char *)xswbuf + i * ksize);

		if (xsw->xsw_dev == NODEV )
			continue;
		if(!strcmp(devname(xsw->xsw_dev, S_IFCHR),
		    name + strlen("/dev/"))) {
			warnx("%s: device already a swap device", name);
			exit(1);
		}
	}
	
	/*
	 * Get the size and offset of this parititon/device
	 */
	fd = open(name, O_RDWR);
	if (fd < 0)
		err(1, "Unable to open %s R+W", name);
	if (ioctl(fd, DIOCGPART, &pinfo) < 0) {
		printf("Cannot trim regular file\n");
		usage ();
	}
	off_t ioarg[2];
	
	/*Trim the Device*/	
	ioarg[0] = pinfo.media_offset;
	ioarg[1] = pinfo.media_size;
	printf("Trimming Device:%s, sectors (%llu -%llu)\n",name,
	     (unsigned long long)ioarg[0]/512,
	     (unsigned long long)ioarg[1]/512);
	if (ioctl(fd, IOCTLTRIM, ioarg) < 0) {
		printf("Device trim failed\n");
		usage ();
	}
	close(fd);
}

static int
swap_on_off(char *name, int doingall, int trim, int ask)
{

	if (ask && which_prog == SWAPON) {
		printf("Do you really want to use device %s as a swap device ?\n", name);
		printf("You might loose data. [Y/N]");

		int c = fgetc(stdin);
		printf("\n");
		if (c != 'y' && c != 'Y')
			return(1);

	}
	if (which_prog == SWAPON && trim){
		char sysctl_name[64];
		int trim_enabled = 0;
		size_t olen = sizeof(trim_enabled);
		char *dev_name = strdup(name);
		dev_name = strtok(dev_name + strlen("/dev/da"),"s");
		sprintf(sysctl_name, "kern.cam.da.%s.trim_enabled", dev_name);
		sysctlbyname(sysctl_name, &trim_enabled, &olen, NULL, 0);
		if(errno == ENOENT) {
			printf("Device:%s does not support the TRIM command\n",
			    name);
			usage();
		}
		if(!trim_enabled) {
			printf("Erase device option selected, but sysctl (%s) "
			    "is not enabled\n",sysctl_name);
			usage();
		}

		trim_volume(name);

	}
	if ((which_prog == SWAPOFF ? swapoff(name) : swapon(name)) == -1) {
		switch(errno) {
		case EBUSY:
			if (!doingall)
				warnx("%s: device already in use", name);
			break;
		case EINVAL:
			if (which_prog == SWAPON)
				warnx("%s: NSWAPDEV limit reached", name);
			else if (!doingall)
				warn("%s", name);
			break;
		default:
			warn("%s", name);
			break;
		}
		return(1);
	}
	return(0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s ", getprogname());
	switch (orig_prog) {
	case SWAPON:
	case SWAPOFF:
		fprintf(stderr, "-aeiq | file ...\n");
		break;
	case SWAPCTL:
		fprintf(stderr, "[-AeghiklmsU] [-a file ... | -d file ...]\n");
		break;
	}
	exit(1);
}

static void
sizetobuf(char *buf, size_t bufsize, int hflag, long long val, int hlen,
    long blocksize)
{
	if (hflag == 'H') {
		char tmp[16];

		humanize_number(tmp, 5, (int64_t)val, "", HN_AUTOSCALE,
		    HN_B | HN_NOSPACE | HN_DECIMAL);
		snprintf(buf, bufsize, "%*s", hlen, tmp);
	} else {
		snprintf(buf, bufsize, "%*lld", hlen, val / blocksize);
	}
}

static void
swaplist(int lflag, int sflag, int hflag)
{
	size_t ksize, bytes = 0;
	char *xswbuf;
	struct xswdev *xsw;
	int hlen, pagesize;
	int i, n;
	long blocksize;
	long long total, used, tmp_total, tmp_used;
	char buf[32];

	pagesize = getpagesize();
	switch(hflag) {
	case 'G':
		blocksize = 1024 * 1024 * 1024;
		strlcpy(buf, "1GB-blocks", sizeof(buf));
		hlen = 10;
		break;
	case 'H':
		blocksize = -1;
		strlcpy(buf, "Bytes", sizeof(buf));
		hlen = 10;
		break;
	case 'K':
		blocksize = 1024;
		strlcpy(buf, "1kB-blocks", sizeof(buf));
		hlen = 10;
		break;
	case 'M':
		blocksize = 1024 * 1024;
		strlcpy(buf, "1MB-blocks", sizeof(buf));
		hlen = 10;
		break;
	default:
		getbsize(&hlen, &blocksize);
		snprintf(buf, sizeof(buf), "%ld-blocks", blocksize);
		break;
	}

	if (sysctlbyname("vm.swap_info_array", NULL, &bytes, NULL, 0) < 0)
		err(1, "sysctlbyname()");
	if (bytes == 0)
		err(1, "sysctlbyname()");

	xswbuf = malloc(bytes);
	if (sysctlbyname("vm.swap_info_array", xswbuf, &bytes, NULL, 0) < 0) {
		free(xswbuf);
		err(1, "sysctlbyname()");
	}
	if (bytes == 0) {
		free(xswbuf);
		err(1, "sysctlbyname()");
	}

	/*
	 * Calculate size of xsw entry returned by kernel (it can be larger
	 * than the one we have if there is a version mismatch).
	 */
	ksize = ((struct xswdev *)xswbuf)->xsw_size;
	n = (int)(bytes / ksize);

	if (lflag) {
		printf("%-13s %*s %*s\n",
		    "Device:",
		    hlen, buf,
		    hlen, "Used:");
	}

	total = used = tmp_total = tmp_used = 0;
	for (i = 0; i < n; ++i) {
		xsw = (void *)((char *)xswbuf + i * ksize);

		if (xsw->xsw_nblks == 0)
			continue;

		tmp_total = (long long)xsw->xsw_nblks * pagesize;
		tmp_used = (long long)xsw->xsw_used * pagesize;
		total += tmp_total;
		used += tmp_used;
		if (lflag) {
			sizetobuf(buf, sizeof(buf), hflag, tmp_total, hlen,
			    blocksize);
			if (xsw->xsw_dev == NODEV) {
				printf("%-13s %s ", "[NFS swap]", buf);
			} else {
				printf("/dev/%-8s %s ",
				    devname(xsw->xsw_dev, S_IFCHR), buf);
			}

			sizetobuf(buf, sizeof(buf), hflag, tmp_used, hlen,
			    blocksize);
			printf("%s\n", buf);
		}
	}

	if (sflag) {
		sizetobuf(buf, sizeof(buf), hflag, total, hlen, blocksize);
		printf("Total:        %s ", buf);
		sizetobuf(buf, sizeof(buf), hflag, used, hlen, blocksize);
		printf("%s\n", buf);
	}
}
