/*
 * Copyright (c) 1993 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 * from: Utah $Hdr: vnconfig.c 1.1 93/12/15$
 *
 * @(#)vnconfig.c	8.1 (Berkeley) 12/15/93
 * $FreeBSD: src/usr.sbin/vnconfig/vnconfig.c,v 1.13.2.7 2003/06/02 09:10:27 maxim Exp $
 * $DragonFly: src/usr.sbin/vnconfig/vnconfig.c,v 1.15 2008/07/27 22:36:01 thomas Exp $
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/linker.h>
#include <sys/mount.h>
#include <sys/module.h>
#include <sys/stat.h>
#include <sys/vnioctl.h>
#include <vfs/ufs/ufsmount.h>

#define LINESIZE	1024
#define ZBUFSIZE	32768

struct vndisk {
	char	*dev;
	char	*file;
	char	*autolabel;
	int	flags;
	int64_t	size;
	char	*oarg;
} *vndisks;

#define VN_CONFIG	0x01
#define VN_UNCONFIG	0x02
#define VN_ENABLE	0x04
#define VN_DISABLE	0x08
#define	VN_SWAP		0x10
#define VN_MOUNTRO	0x20
#define VN_MOUNTRW	0x40
#define VN_IGNORE	0x80
#define VN_SET		0x100
#define VN_RESET	0x200
#define VN_TRUNCATE	0x400
#define VN_ZERO		0x800

int nvndisks;

int all = 0;
int verbose = 0;
int global = 0;
int listopt = 0;
u_long setopt = 0;
u_long resetopt = 0;
const char *configfile;

int config(struct vndisk *);
void getoptions(struct vndisk *, const char *);
int getinfo(const char *vname);
char *rawdevice(const char *);
void readconfig(int);
static void usage(void);
static int64_t getsize(const char *arg);
static void do_autolabel(const char *dev, const char *label);
int what_opt(const char *, u_long *);

int
main(int argc, char *argv[])
{
	int i, rv;
	int flags = 0;
	int64_t size = 0;
	char *autolabel = NULL;
	char *s;

	configfile = _PATH_VNTAB;
	while ((i = getopt(argc, argv, "acdef:glr:s:S:TZL:uv")) != -1)
		switch (i) {

		/* all -- use config file */
		case 'a':
			all++;
			break;

		/* configure */
		case 'c':
			flags |= VN_CONFIG;
			flags &= ~VN_UNCONFIG;
			break;

		/* disable */
		case 'd':
			flags |= VN_DISABLE;
			flags &= ~VN_ENABLE;
			break;

		/* enable */
		case 'e':
			flags |= (VN_ENABLE|VN_CONFIG);
			flags &= ~(VN_DISABLE|VN_UNCONFIG);
			break;

		/* alternate config file */
		case 'f':
			configfile = optarg;
			break;

		/* fiddle global options */
		case 'g':
			global = 1 - global;
			break;

		/* reset options */
		case 'r':
			for (s = strtok(optarg, ","); s; s = strtok(NULL, ",")) {
				if (what_opt(s, &resetopt))
					errx(1, "invalid options '%s'", s);
			}
			flags |= VN_RESET;
			break;

		case 'l':
			listopt = 1;
			break;

		/* set options */
		case 's':
			for (s = strtok(optarg, ","); s; s = strtok(NULL, ",")) {
				if (what_opt(s, &setopt))
					errx(1, "invalid options '%s'", s);
			}
			flags |= VN_SET;
			break;

		/* unconfigure */
		case 'u':
			flags |= (VN_DISABLE|VN_UNCONFIG);
			flags &= ~(VN_ENABLE|VN_CONFIG);
			break;

		/* verbose */
		case 'v':
			verbose++;
			break;

		case 'S':
			size = getsize(optarg);
			flags |= VN_CONFIG;
			flags &= ~VN_UNCONFIG;
			break;

		case 'T':
			flags |= VN_TRUNCATE;
			break;

		case 'Z':
			flags |= VN_ZERO;
			break;

		case 'L':
			autolabel = optarg;
			break;

		default:
			usage();
		}

	if (modfind("vn") < 0)
		if (kldload("vn") < 0 || modfind("vn") < 0)
			warnx( "cannot find or load \"vn\" kernel module");

	rv = 0;
	if (listopt) {
		if(argc > optind)
			while(argc > optind) 
				rv += getinfo( argv[optind++]);
		else {
			rv = getinfo( NULL );
		}
		exit(rv);
	}

	if (flags == 0)
		flags = VN_CONFIG;
	if (all) {
		readconfig(flags);
	} else {
		vndisks = calloc(sizeof(struct vndisk), 1);
		if (argc < optind + 1)
			usage();
		vndisks[0].dev = argv[optind++];
		vndisks[0].file = argv[optind++];	/* may be NULL */
		vndisks[0].flags = flags;
		vndisks[0].size = size;
		vndisks[0].autolabel = autolabel;
		if (optind < argc)
			getoptions(&vndisks[0], argv[optind]);
		nvndisks = 1;
	}
	rv = 0;
	for (i = 0; i < nvndisks; i++)
		rv += config(&vndisks[i]);
	exit(rv);
}

int
what_opt(const char *str, u_long *p)
{
	if (!strcmp(str,"reserve")) { *p |= VN_RESERVE; return 0; }
	if (!strcmp(str,"labels")) { return 0; }	/* deprecated */
	if (!strcmp(str,"follow")) { *p |= VN_FOLLOW; return 0; }
	if (!strcmp(str,"debug")) { *p |= VN_DEBUG; return 0; }
	if (!strcmp(str,"io")) { *p |= VN_IO; return 0; }
	if (!strcmp(str,"all")) { *p |= ~0; return 0; }
	if (!strcmp(str,"none")) { *p |= 0; return 0; }
	return 1;
}

/*
 *
 * GETINFO
 *
 *	Print vnode disk information to stdout for the device at
 *	path 'vname', or all existing 'vn' devices if none is given. 
 *	Any 'vn' devices must exist under /dev in order to be queried.
 *
 *	Todo: correctly use vm_secsize for swap-backed vn's ..
 */

int
getinfo( const char *vname )
{
	int i = 0, vd, printlim = 0;
	char vnpath[PATH_MAX];
	const char *tmp;

	struct vn_user vnu;
	struct stat sb;

	if (vname == NULL) {
		printlim = 1024;
	} else {
		tmp = vname;
		while (*tmp != 0) {
			if(isdigit(*tmp)){
				i = atoi(tmp);
				printlim = i + 1;
				break;
			}
			tmp++;
		}
		if (*tmp == '\0')
			errx(1, "unknown vn device: %s", vname);
	}

	snprintf(vnpath, sizeof(vnpath), "/dev/vn%d", i);

	vd = open(vnpath, O_RDONLY);
	if (vd < 0)
		err(1, "open: %s", vnpath);

	for (; i<printlim; i++) {

		bzero(&vnpath, sizeof(vnpath));
		bzero(&sb, sizeof(struct stat));
		bzero(&vnu, sizeof(struct vn_user));

		vnu.vnu_unit = i;

		snprintf(vnpath, sizeof(vnpath), "/dev/vn%d", vnu.vnu_unit);

		if(stat(vnpath, &sb) < 0) {
			break;
		}
		else {
        		if (ioctl(vd, VNIOCGET, &vnu) == -1) {
				warn("vn%d: ioctl", i);
					continue;
        		}

			fprintf(stdout, "vn%d: ", vnu.vnu_unit);

			if (vnu.vnu_file[0] == 0)
				fprintf(stdout, "not in use\n");
			else if ((strcmp(vnu.vnu_file, _VN_USER_SWAP)) == 0)
				fprintf(stdout,
					"consuming %jd VM pages\n",
					(intmax_t)vnu.vnu_size);
			else
				fprintf(stdout, 
					"covering %s on %s, inode %ju\n",
					vnu.vnu_file,
					devname(vnu.vnu_dev, S_IFBLK), 
					(uintmax_t)vnu.vnu_ino);
		}
	}
	close(vd);
	return 0;
}

int
config(struct vndisk *vnp)
{
	char *dev, *file, *rdev, *oarg;
	FILE *f;
	struct vn_ioctl vnio;
	int flags, pgsize, rv, status;
	u_long l;
	
	pgsize = getpagesize();

	status = rv = 0;

	/*
	 * Prepend "/dev/" to the specified device name, if necessary.
	 * Operate on vnp->dev because it is used later.
	 */
	if (vnp->dev[0] != '/' && vnp->dev[0] != '.')
		asprintf(&vnp->dev, "%s%s", _PATH_DEV, vnp->dev);
	dev = vnp->dev;
	file = vnp->file;
	flags = vnp->flags;
	oarg = vnp->oarg;

	if (flags & VN_IGNORE)
		return(0);

	/*
	 * When a regular file has been specified, do any requested setup
	 * of the file.  Truncation (also creates the file if necessary),
	 * sizing, and zeroing.
	 */

	if (file && vnp->size != 0 && (flags & VN_CONFIG)) {
		int  fd;
		struct stat st;

		if (flags & VN_TRUNCATE)
			fd = open(file, O_RDWR|O_CREAT|O_TRUNC, 0600);
		else
			fd = open(file, O_RDWR);
		if (fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
			if (st.st_size < vnp->size * pgsize)
				ftruncate(fd, vnp->size * pgsize);
			if (vnp->size != 0)
				st.st_size = vnp->size * pgsize;

			if (flags & VN_ZERO) {
				char *buf = malloc(ZBUFSIZE);
				bzero(buf, ZBUFSIZE);
				while (st.st_size > 0) {
					int n = (st.st_size > ZBUFSIZE) ?
					    ZBUFSIZE : (int)st.st_size;
					if (write(fd, buf, n) != n) {
						ftruncate(fd, 0);
						printf("Unable to ZERO file %s\n", file);
						return(0);
					}
					st.st_size -= (off_t)n;
				}
			}
			close(fd);
		} else {
			printf("Unable to open file %s\n", file);
			return(0);
		}
	} else if (file == NULL && vnp->size == 0 && (flags & VN_CONFIG)) {
		warnx("specify regular filename or swap size");
		return (0);
	}

	rdev = rawdevice(dev);
	f = fopen(rdev, "rw");
	if (f == NULL) {
		warn("%s", dev);
		return(1);
	}
	if (!strcmp(rdev, "/dev/vn")) {
		printf("%s\n", fdevname(fileno(f)));
		rv = asprintf(&dev, "%s%s", _PATH_DEV, fdevname(fileno(f)));
		if (rv < 0)
			dev = fdevname(fileno(f));
	}

	vnio.vn_file = file;
	vnio.vn_size = vnp->size;	/* non-zero only if swap backed */

	/*
	 * Disable the device
	 */
	if (flags & VN_DISABLE) {
		if (flags & (VN_MOUNTRO|VN_MOUNTRW)) {
			rv = unmount(oarg, 0);
			if (rv) {
				status--;
				if (errno == EBUSY)
					flags &= ~VN_UNCONFIG;
				if ((flags & VN_UNCONFIG) == 0)
					warn("umount");
			} else if (verbose)
				printf("%s: unmounted\n", dev);
		}
	}
	/*
	 * Clear (un-configure) the device
	 */
	if (flags & VN_UNCONFIG) {
		rv = ioctl(fileno(f), VNIOCDETACH, &vnio);
		if (rv) {
			if (errno == ENODEV) {
				if (verbose)
					printf("%s: not configured\n", dev);
				rv = 0;
			} else {
				status--;
				warn("VNIOCDETACH");
			}
		} else if (verbose)
			printf("%s: cleared\n", dev);
	}
	/*
	 * Set specified options
	 */
	if (flags & VN_SET) {
		l = setopt;
		if (global)
			rv = ioctl(fileno(f), VNIOCGSET, &l);
		else
			rv = ioctl(fileno(f), VNIOCUSET, &l);
		if (rv) {
			status--;
			warn("VNIO[GU]SET");
		} else if (verbose)
			printf("%s: flags now=%08lx\n",dev,l);
	}
	/*
	 * Reset specified options
	 */
	if (flags & VN_RESET) {
		l = resetopt;
		if (global)
			rv = ioctl(fileno(f), VNIOCGCLEAR, &l);
		else
			rv = ioctl(fileno(f), VNIOCUCLEAR, &l);
		if (rv) {
			status--;
			warn("VNIO[GU]CLEAR");
		} else if (verbose)
			printf("%s: flags now=%08lx\n",dev,l);
	}
	/*
	 * Configure the device
	 */
	if (flags & VN_CONFIG) {
		rv = ioctl(fileno(f), VNIOCATTACH, &vnio);
		if (rv) {
			status--;
			warn("VNIOCATTACH");
			flags &= ~VN_ENABLE;
		} else {
			if (verbose) {
				printf("%s: %s, ", dev, file);
				if (vnp->size != 0) {
				    printf("%jd bytes mapped\n", (intmax_t)vnio.vn_size);
				} else {
				    printf("complete file mapped\n");
				}
			}
			/*
			 * autolabel
			 */
			if (vnp->autolabel) {
				do_autolabel(vnp->dev, vnp->autolabel);
			}
		}
	}
	/*
	 * Set an option
	 */
	if (flags & VN_SET) {
		l = setopt;
		if (global)
			rv = ioctl(fileno(f), VNIOCGSET, &l);
		else
			rv = ioctl(fileno(f), VNIOCUSET, &l);
		if (rv) {
			status--;
			warn("VNIO[GU]SET");
		} else if (verbose)
			printf("%s: flags now=%08lx\n",dev,l);
	}
	/*
	 * Reset an option
	 */
	if (flags & VN_RESET) {
		l = resetopt;
		if (global)
			rv = ioctl(fileno(f), VNIOCGCLEAR, &l);
		else
			rv = ioctl(fileno(f), VNIOCUCLEAR, &l);
		if (rv) {
			status--;
			warn("VNIO[GU]CLEAR");
		} else if (verbose)
			printf("%s: flags now=%08lx\n",dev,l);
	}

	/*
	 * Close the device now, as we may want to mount it.
	 */
	fclose(f);

	/*
	 * Enable special functions on the device
	 */
	if (flags & VN_ENABLE) {
		if (flags & VN_SWAP) {
			rv = swapon(dev);
			if (rv) {
				status--;
				warn("swapon");
			}
			else if (verbose)
				printf("%s: swapping enabled\n", dev);
		}
		if (flags & (VN_MOUNTRO|VN_MOUNTRW)) {
			struct ufs_args args;
			int mflags;

			args.fspec = dev;
			mflags = (flags & VN_MOUNTRO) ? MNT_RDONLY : 0;
			rv = mount("ufs", oarg, mflags, &args);
			if (rv) {
				status--;
				warn("mount");
			}
			else if (verbose)
				printf("%s: mounted on %s\n", dev, oarg);
		}
	}
/* done: */
	fflush(stdout);
	return(status < 0);
}

#define EOL(c)		((c) == '\0' || (c) == '\n')
#define WHITE(c)	((c) == ' ' || (c) == '\t' || (c) == '\n')

void
readconfig(int flags)
{
	char buf[LINESIZE];
	FILE *f;
	char *cp, *sp;
	int ix;
	int ax;

	f = fopen(configfile, "r");
	if (f == NULL)
		err(1, "%s", configfile);
	ix = 0;		/* number of elements */
	ax = 0;		/* allocated elements */
	while (fgets(buf, LINESIZE, f) != NULL) {
		cp = buf;
		if (*cp == '#')
			continue;
		while (!EOL(*cp) && WHITE(*cp))
			cp++;
		if (EOL(*cp))
			continue;
		sp = cp;
		while (!EOL(*cp) && !WHITE(*cp))
			cp++;
		if (EOL(*cp))
			continue;
		*cp++ = '\0';

		if (ix == ax) {
			ax = ax + 16;
			vndisks = realloc(vndisks, ax * sizeof(struct vndisk));
			bzero(&vndisks[ix], (ax - ix) * sizeof(struct vndisk));
		}
		vndisks[ix].dev = malloc(cp - sp);
		strcpy(vndisks[ix].dev, sp);
		while (!EOL(*cp) && WHITE(*cp))
			cp++;
		if (EOL(*cp))
			continue;
		sp = cp;
		while (!EOL(*cp) && !WHITE(*cp))
			cp++;
		*cp++ = '\0';

		if (*sp == '%' && strtol(sp + 1, NULL, 0) > 0) {
			vndisks[ix].size = getsize(sp + 1);
		} else {
			vndisks[ix].file = malloc(cp - sp);
			strcpy(vndisks[ix].file, sp);
		}

		while (!EOL(*cp) && WHITE(*cp))
			cp++;
		vndisks[ix].flags = flags;
		if (!EOL(*cp)) {
			sp = cp;
			while (!EOL(*cp) && !WHITE(*cp))
				cp++;
			*cp++ = '\0';
			getoptions(&vndisks[ix], sp);
		}
		nvndisks++;
		ix++;
	}
}

void
getoptions(struct vndisk *vnp, const char *fstr)
{
	int flags = 0;
	const char *oarg = NULL;

	if (strcmp(fstr, "swap") == 0)
		flags |= VN_SWAP;
	else if (strncmp(fstr, "mount=", 6) == 0) {
		flags |= VN_MOUNTRW;
		oarg = &fstr[6];
	} else if (strncmp(fstr, "mountrw=", 8) == 0) {
		flags |= VN_MOUNTRW;
		oarg = &fstr[8];
	} else if (strncmp(fstr, "mountro=", 8) == 0) {
		flags |= VN_MOUNTRO;
		oarg = &fstr[8];
	} else if (strcmp(fstr, "ignore") == 0)
		flags |= VN_IGNORE;
	vnp->flags |= flags;
	if (oarg) {
		vnp->oarg = malloc(strlen(oarg) + 1);
		strcpy(vnp->oarg, oarg);
	} else
		vnp->oarg = NULL;
}

char *
rawdevice(const char *dev)
{
	char *rawbuf, *dp, *ep;
	struct stat sb;
	int len;

	len = strlen(dev);
	rawbuf = malloc(len + 2);
	strcpy(rawbuf, dev);
	if (stat(rawbuf, &sb) != 0 || !S_ISCHR(sb.st_mode)) {
		dp = strrchr(rawbuf, '/');
		if (dp) {
			for (ep = &rawbuf[len]; ep > dp; --ep)
				*(ep+1) = *ep;
			*++ep = 'r';
		}
	}
	return (rawbuf);
}

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n%s\n",
		"usage: vnconfig [-cdeguvTZ] [-s options] [-r options]",
		"                [-S value] special_file [regular_file] [feature]",
		"       vnconfig -a [-cdeguv] [-s options] [-r options] [-f config_file]",
		"       vnconfig -l [special_file ...]");
	exit(1);
}

static int64_t
getsize(const char *arg)
{
	char *ptr;
	int pgsize = getpagesize();
	int64_t size = strtoq(arg, &ptr, 0);

	switch(tolower(*ptr)) {
	case 't':
		/*
		 * GULP!  Terabytes.  It's actually possible to create
		 * a 7.9 TB VN device, though newfs can't handle any single
		 * filesystem larger then 1 TB.
		 */
		size *= 1024;
		/* fall through */
	case 'g':
		size *= 1024;
		/* fall through */
	default:
	case 'm':
		size *= 1024;
		/* fall through */
	case 'k':
		size *= 1024;
		/* fall through */
	case 'c':
		break;
	}
	size = (size + pgsize - 1) / pgsize;
	return(size);
}

/*
 * DO_AUTOLABEL
 *
 *	Automatically label the device.  This will wipe any preexisting
 *	label.
 */

static void
do_autolabel(const char *dev __unused, const char *label __unused)
{
	/* XXX not yet implemented */
	fprintf(stderr, "autolabel not yet implemented, sorry\n");
	exit(1);
}

