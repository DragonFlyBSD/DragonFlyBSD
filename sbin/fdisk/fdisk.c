/*
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * Any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 *
 * Ported to 386bsd by Julian Elischer  Thu Oct 15 20:26:46 PDT 1992
 *
 * 14-Dec-89  Robert Baron (rvb) at Carnegie-Mellon University
 *	Copyright (c) 1989	Robert. V. Baron
 *	Created.
 *
 * $FreeBSD: /repoman/r/ncvs/src/sbin/i386/fdisk/fdisk.c,v 1.36.2.14 2004/01/30 14:40:47 harti Exp $
 */

#include <sys/param.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <err.h>
#include <fstab.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bus/cam/scsi/scsi_daio.h>

#define Decimal(str, ans, tmp) \
	if (decimal(str, &tmp, ans)) ans = tmp

#define MAX_SEC_SIZE		4096	/* maximum section size supported */
#define MIN_SEC_SIZE		512	/* sector size to start sensing at */
#define MAX_SECTORS_PER_TRACK	0x3f	/* maximum number of sectors per track */
#define MIN_SECTORS_PER_TRACK	0x1	/* minimum number of sectors per track */
#define MAX_HEADS		0xff	/* maximum number of heads */

#define ACTIVE		0x80
#define DOSSECT(s,c)	((s & MAX_SECTORS_PER_TRACK) | ((c >> 2) & 0xc0))
#define DOSCYL(c)	(c & 0xff)
#define MAXCYL		1023

static const char *disk = NULL;
static const char *disks[] = {		/* List of disks to try open. */
	"/dev/da0", "/dev/ad0", "/dev/vkd0", "/dev/vbd0", NULL,
};

struct mboot {
	unsigned char padding[2];	/* force the longs to be long aligned */
	unsigned char *bootinst;	/* boot code */
	off_t bootinst_size;
	struct dos_partition parts[NDOSPART];
};
static struct mboot mboot;

static int fd;			/* file descriptor of the given disk */
static int secsize = 0;		/* the sensed sector size */
static int partition = -1;
static int current_line_number;
static int geom_processed = 0;
static int part_processed = 0;
static int active_processed = 0;
static int dos_cyls, dos_heads, dos_sectors, dos_cylsecs;
static int cyls, sectors, heads, cylsecs;
static int64_t disksecs;

#define LBUF 100
static char lbuf[LBUF];

#define MAX_ARGS	10
typedef struct cmd {
	char cmd;
	int n_args;
	struct arg {
		char argtype;
		long long arg_val;
	} args[MAX_ARGS];
} CMD;

static int B_flag = 0;		/* replace boot code */
static int C_flag = 0;		/* use wrapped values for CHS */
static int E_flag = 0;		/* Erase through TRIM */
static int I_flag = 0;		/* use entire disk for DragonFly */
static int a_flag = 0;		/* set active partition */
static char *b_flag = NULL;	/* path to boot code */
static char *f_flag = NULL;	/* Read config info from file */
static int i_flag = 0;		/* replace partition data */
static int u_flag = 0;		/* update partition data */
static int p_flag = 0;		/* operate on a disk image file */
static int s_flag = 0;		/* Print a summary and exit */
static int t_flag = 0;		/* test only */
static int v_flag = 0;		/* Be verbose */
static int x_flag = 0;		/* Expand-to-fit device */

static void print_s0(int which);
static void print_part(int i);
static void init_sector0(unsigned long start);
static void init_boot(void);
static void change_part(int i);
static void print_params(void);
static void change_active(int which);
static void change_code(void);
static void get_params_to_use(void);
static void dos(struct dos_partition *partp);
static int open_disk(void);
static void erase_partition(int i);
static ssize_t read_disk(off_t sector, void *buf);
static ssize_t write_disk(off_t sector, void *buf);
static int get_params(void);
static int read_s0(void);
static int write_s0(void);
static int ok(const char *str);
static int decimal(const char *str, int *num, int deflt);
static const char *get_type(int type);
static int read_config(char *config_file);
static void reset_boot(void);
static int sanitize_partition(struct dos_partition *);
static void usage(void);
static int expand_table(void);

int
main(int argc, char *argv[])
{
	int	c, i;

	while ((c = getopt(argc, argv, "BCEIab:f:ip:stuvx1234")) != -1) {
		switch (c) {
		case 'B':
			B_flag = 1;
			break;
		case 'C':
			C_flag = 1;
			break;
		case 'E':
			E_flag = 1;
			break;
		case 'I':
			I_flag = 1;
			break;
		case 'a':
			a_flag = 1;
			break;
		case 'b':
			b_flag = optarg;
			break;
		case 'f':
			f_flag = optarg;
			break;
		case 'i':
			i_flag = 1;
			break;
		case 'p':
			disk = optarg;
			p_flag = 1;
			break;
		case 's':
			s_flag = 1;
			break;
		case 't':
			t_flag = 1;
			break;
		case 'u':
			u_flag = 1;
			break;
		case 'v':
			v_flag = 1;
			break;
		case 'x':
			++x_flag;
			break;
		case '1':
		case '2':
		case '3':
		case '4':
			partition = c - '0';
			break;
		default:
			usage();
		}
	}

	if (f_flag || i_flag)
		u_flag = 1;
	if (t_flag)
		v_flag = 1;

	argc -= optind;
	argv += optind;

	if (argc > 0) {
		disk = getdevpath(argv[0], 0);
		if (disk == NULL)
			err(1, "cannot get the path of device %s", argv[0]);
		if (open_disk() < 0)
			err(1, "cannot open disk %s", disk);
	} else if (disk == NULL) {
		int rv = 0;

		for (i = 0; disks[i]; i++) {
			disk = disks[i];
			rv = open_disk();
			if (rv != -2)
				break;
		}
		if (rv < 0)
			err(1, "cannot open any disk");
	} else {
		if (open_disk() < 0)
			err(1, "cannot open disk %s", disk);
	}

	/*
	 * (abu)use mboot.bootinst to probe for the sector size
	 */
	if ((mboot.bootinst = malloc(MAX_SEC_SIZE)) == NULL)
		err(1, "cannot allocate buffer to determine disk sector size");
	if (read_disk(0, mboot.bootinst) == -1) {
		free(mboot.bootinst);
		errx(1, "could not read MBR");
	}
	free(mboot.bootinst);
	mboot.bootinst = NULL;

	if (s_flag) {
		struct dos_partition *partp;

		printf("%s: %d cyl %d hd %d sec\n",
		       disk, dos_cyls, dos_heads, dos_sectors);
		if (read_s0()) {
			if (errno != 0)
				err(1, "read_s0");
			else
				exit(1); /* warning already reported */
		}
		printf("Part  %11s %11s Type Flags\n", "Start", "Size");
		for (i = 0; i < NDOSPART; i++) {
			partp = ((struct dos_partition *) &mboot.parts) + i;
			if (partp->dp_start == 0 && partp->dp_size == 0)
				continue;
			printf("%4d: %11lu %11lu 0x%02x 0x%02x\n",
			       i + 1, (u_long) partp->dp_start,
			       (u_long) partp->dp_size, partp->dp_typ,
			       partp->dp_flag);
		}
		exit(0);
	}

	printf("******* Working on device %s *******\n", disk);

	if (I_flag) {
		struct dos_partition *partp;

		read_s0();
		reset_boot();
		partp = (struct dos_partition *) (&mboot.parts[0]);
		partp->dp_typ = DOSPTYP_DFLYBSD;
		partp->dp_flag = ACTIVE;
		partp->dp_start = dos_sectors;
		if (disksecs - dos_sectors > 0xFFFFFFFFU) {
			printf("Warning: Ending logical block > 2TB, "
			       "using max value\n");
			partp->dp_size = 0xFFFFFFFFU;
		} else {
			partp->dp_size =
			    rounddown(disksecs, dos_cylsecs) - dos_sectors;
		}
		dos(partp);
		if (v_flag)
			print_s0(-1);

		if (E_flag) {
			/* Trim now if we're using the entire device */
			erase_partition(0);
		}

		if (!t_flag) {
			if (write_s0() == -1)
				err(1, "can't write fdisk partition table");
		}

		exit(0);
	}

	if (f_flag) {
		if (read_s0() || i_flag)
			reset_boot();

		if (!read_config(f_flag))
			exit(1);

		if (x_flag)
			x_flag = expand_table();
		if (v_flag)
			print_s0(-1);
		if (!t_flag) {
			if (write_s0() == -1)
				err(1, "can't write fdisk partition table");
		}

		exit(0);
	}

	if (u_flag)
		get_params_to_use();
	else
		print_params();

	if (read_s0())
		init_sector0(dos_sectors);

	printf("Media sector size is %d\n", secsize);
	printf("Warning: BIOS sector numbering starts with sector 1\n");
	printf("Information from DOS bootblock is:\n");
	if (partition == -1) {
		for (i = 1; i <= NDOSPART; i++)
			change_part(i);
	} else {
		change_part(partition);
	}

	if (u_flag || a_flag)
		change_active(partition);
	if (B_flag)
		change_code();
	if (x_flag)
		x_flag = expand_table();

	if (x_flag || u_flag || a_flag || B_flag) {
		if (!t_flag) {
			printf("\nWe haven't changed the "
			       "partition table yet.  "
			       "This is your last chance.\n");
		}
		print_s0(-1);
		if (!t_flag) {
			if (ok("Should we write new partition table?")) {
				if (E_flag && u_flag) {
					/*
					 * Trim now because we've
					 * committed to updating the
					 * partition.
					 */
					if (partition == -1) {
						for (i = 0; i < NDOSPART; i++)
							erase_partition(i);
					} else {
						erase_partition(partition);
					}
				}
				if (write_s0() == -1)
					err(1, "can't write fdisk partition "
					       "table");
			}
		} else {
			printf("\n-t flag specified -- "
			       "partition table not written.\n");
		}
	}

	exit(0);
}

static void
usage(void)
{
	fprintf(stderr, "%s%s%s",
		"usage: fdisk [-BCEIaistu] [-b bootcode] [-p diskimage]\n",
		"             [-1234] [disk]\n",
		"       fdisk -f configfile [-itv] [disk]\n");
        exit(1);
}

static int
expand_table(void)
{
	struct dos_partition *part;
	struct dos_partition *best;
	int i;

	printf("\n");

	best = NULL;
	for (i = 0; i < NDOSPART; ++i) {
		part = ((struct dos_partition *) &mboot.parts) + i;
		if (part->dp_start == 0 && part->dp_size == 0)
			continue;
		if (best == NULL || best->dp_start < part->dp_start)
			best = part;
	}
	if (best) {
		if (best->dp_typ == 0xEE || best->dp_typ == 0xEF) {
			printf("Cannot use fdisk to resize a GPT label\n");
			printf("use 'gpt expand <device>' instead\n");
			best = NULL;
			return 0;
		}
		if (disksecs - best->dp_start > 0xFFFFFFFFU) {
			if (best->dp_size == 0xFFFFFFFFU) {
				printf("Last slice already using max value, "
				       "no changes required\n");
				best = NULL;
			}
		} else if (best->dp_size == rounddown(disksecs, dos_cylsecs) -
						      best->dp_start) {
			printf("Last slice already properly sized, "
			       "no changes required\n");
			best = NULL;
		}
	}

	if (best) {
		printf("Changing size of last slice %u -> ", best->dp_start);
		if (disksecs - best->dp_start > 0xFFFFFFFFU) {
			printf("max-value\n");
			best->dp_size = 0xFFFFFFFFU;
		} else {
			best->dp_size = rounddown(disksecs, dos_cylsecs) -
						  best->dp_start;
			printf("%u\n", best->dp_size);
		}
		dos(best);
		return 1;
	}

	return 0;
}

static void
print_s0(int which)
{
	int	i;

	print_params();
	printf("Information from DOS bootblock is:\n");
	if (which == -1) {
		for (i = 1; i <= NDOSPART; i++)
			printf("%d: ", i), print_part(i);
	} else {
		print_part(which);
	}
}

static struct dos_partition mtpart = { 0 };

static void
print_part(int i)
{
	struct dos_partition *partp;
	uint64_t part_mb;

	partp = ((struct dos_partition *) &mboot.parts) + i - 1;

	if (!bcmp(partp, &mtpart, sizeof (struct dos_partition))) {
		printf("<UNUSED>\n");
		return;
	}

	part_mb = (partp->dp_size * secsize) >> 20;
	printf("sysid %d,(%s)\n", partp->dp_typ, get_type(partp->dp_typ));
	printf("    start %lu, size %lu (%jd Meg), flag %x%s\n",
	       (u_long)partp->dp_start, (u_long)partp->dp_size,
	       (intmax_t)part_mb, partp->dp_flag,
	       partp->dp_flag == ACTIVE ? " (active)" : "");
	printf("\tbeg: cyl %d/ head %d/ sector %d;\n"
	       "\tend: cyl %d/ head %d/ sector %d\n",
	       DPCYL(partp->dp_scyl, partp->dp_ssect),
	       partp->dp_shd, DPSECT(partp->dp_ssect),
	       DPCYL(partp->dp_ecyl, partp->dp_esect),
	       partp->dp_ehd, DPSECT(partp->dp_esect));
}


static void
init_boot(void)
{
	const char *fname;
	int boot_fd, n;
	struct stat sb;

	fname = b_flag ? b_flag : "/boot/mbr";
	if ((boot_fd = open(fname, O_RDONLY)) == -1 ||
	    fstat(boot_fd, &sb) == -1)
		err(1, "%s", fname);

	mboot.bootinst_size = sb.st_size;
	if (mboot.bootinst_size % secsize != 0) {
		close(boot_fd);
		errx(1, "%s: length must be a multiple of sector size", fname);
	}
	free(mboot.bootinst);
	if ((mboot.bootinst = malloc(mboot.bootinst_size)) == NULL) {
		close(boot_fd);
		errx(1, "%s: unable to allocate read buffer", fname);
	}

	n = read(boot_fd, mboot.bootinst, mboot.bootinst_size);
	close(boot_fd);
	if (n == -1)
		err(1, "%s", fname);
	if (n != mboot.bootinst_size)
		errx(1, "%s: short read", fname);
}


static void
init_sector0(unsigned long start)
{
	struct dos_partition *partp;

	init_boot();

	partp = (struct dos_partition *)(&mboot.parts[3]);
	partp->dp_typ = DOSPTYP_DFLYBSD;
	partp->dp_flag = ACTIVE;
	start = roundup(start, dos_sectors);
	if (start == 0)
		start = dos_sectors;
	partp->dp_start = start;
	if (disksecs - start > 0xFFFFFFFFU) {
		printf("Warning: Ending logical block > 2TB, "
		       "using max value\n");
		partp->dp_size = 0xFFFFFFFFU;
	} else {
		partp->dp_size = rounddown(disksecs, dos_cylsecs) - start;
	}

	dos(partp);
}

static void
change_part(int i)
{
	struct dos_partition *partp;
	int tmp, tsec, tcyl, thd;

	partp = ((struct dos_partition *) &mboot.parts) + i - 1;

	printf("The data for partition %d is:\n", i);
	print_part(i);

	if (!u_flag || !ok("Do you want to change it?"))
		return;

	if (i_flag) {
		bzero((char *)partp, sizeof (struct dos_partition));
		if (i == 4) {
			init_sector0(1);
			printf("\nThe static data for the DOS partition 4 "
			       "has been reinitialized to:\n");
			print_part(i);
		}
	}

	do {
		Decimal("sysid (108=DragonFly)", partp->dp_typ, tmp);
		Decimal("start", partp->dp_start, tmp);
		Decimal("size", partp->dp_size, tmp);
		if (!sanitize_partition(partp)) {
			warnx("ERROR: failed to adjust; setting sysid to 0");
			partp->dp_typ = 0;
		}

		if (ok("Explicitly specify beg/end address ?")) {
			tcyl = DPCYL(partp->dp_scyl,partp->dp_ssect);
			thd = partp->dp_shd;
			tsec = DPSECT(partp->dp_ssect);
			Decimal("beginning cylinder", tcyl, tmp);
			Decimal("beginning head", thd, tmp);
			Decimal("beginning sector", tsec, tmp);
			if (tcyl > MAXCYL && C_flag == 0) {
				printf("Warning: starting cylinder wraps, "
				       "using all 1's\n");
				partp->dp_scyl = -1;
				partp->dp_ssect = -1;
				partp->dp_shd = -1;
			} else {
				partp->dp_scyl = DOSCYL(tcyl);
				partp->dp_ssect = DOSSECT(tsec,tcyl);
				partp->dp_shd = thd;
			}

			tcyl = DPCYL(partp->dp_ecyl,partp->dp_esect);
			thd = partp->dp_ehd;
			tsec = DPSECT(partp->dp_esect);
			Decimal("ending cylinder", tcyl, tmp);
			Decimal("ending head", thd, tmp);
			Decimal("ending sector", tsec, tmp);
			if (tcyl > MAXCYL && C_flag == 0) {
				printf("Warning: ending cylinder wraps, "
				       "using all 1's\n");
				partp->dp_ecyl = -1;
				partp->dp_esect = -1;
				partp->dp_ehd = -1;
			} else {
				partp->dp_ecyl = DOSCYL(tcyl);
				partp->dp_esect = DOSSECT(tsec,tcyl);
				partp->dp_ehd = thd;
			}
		} else {
			dos(partp);
		}

		print_part(i);
	} while (!ok("Are we happy with this entry?"));
}

static void
print_params(void)
{
	printf("parameters extracted from device are:\n");
	printf("cylinders=%d heads=%d sectors/track=%d (%d blks/cyl)\n\n",
	       cyls, heads, sectors, cylsecs);
	if (dos_sectors > MAX_SECTORS_PER_TRACK ||
	    dos_cyls > MAXCYL || dos_heads > MAX_HEADS)
		printf("Figures below won't work with BIOS for "
		       "partitions not in cyl 1\n");
	printf("parameters to be used for BIOS calculations are:\n");
	printf("cylinders=%d heads=%d sectors/track=%d (%d blks/cyl)\n\n",
	       dos_cyls, dos_heads, dos_sectors, dos_cylsecs);
}

static void
change_active(int which)
{
	struct dos_partition *partp = &mboot.parts[0];
	int active, i, new, tmp;

	active = -1;
	for (i = 0; i < NDOSPART; i++) {
		if ((partp[i].dp_flag & ACTIVE) == 0)
			continue;
		printf("Partition %d is marked active\n", i + 1);
		if (active == -1)
			active = i + 1;
	}
	if (a_flag && which != -1)
		active = which;
	else if (active == -1)
		active = 1;

	if (!ok("Do you want to change the active partition?"))
		return;

setactive:
	do {
		new = active;
		Decimal("active partition", new, tmp);
		if (new < 1 || new > NDOSPART) {
			printf("Active partition number must be in range 1-4."
			       "  Try again.\n");
			goto setactive;
		}
		active = new;
	} while (!ok("Are you happy with this choice"));
	for (i = 0; i < NDOSPART; i++)
		partp[i].dp_flag = 0;
	if (active > 0 && active <= NDOSPART)
		partp[active-1].dp_flag = ACTIVE;
}

static void
change_code(void)
{
	if (ok("Do you want to change the boot code?"))
		init_boot();
}

static void
get_params_to_use(void)
{
	int	tmp;

	print_params();
	if (ok("Do you want to change our idea of what BIOS thinks ?")) {
		do {
			Decimal("BIOS's idea of #cylinders", dos_cyls, tmp);
			Decimal("BIOS's idea of #heads", dos_heads, tmp);
			Decimal("BIOS's idea of #sectors", dos_sectors, tmp);
			dos_cylsecs = dos_heads * dos_sectors;
			print_params();
		} while(!ok("Are you happy with this choice"));
	}
}


/***********************************************\
* Change real numbers into strange dos numbers	*
\***********************************************/
static void
dos(struct dos_partition *partp)
{
	int cy, sec;
	uint32_t end;

	if (partp->dp_typ == 0 && partp->dp_start == 0 && partp->dp_size == 0) {
		memcpy(partp, &mtpart, sizeof(*partp));
		return;
	}

	/* Start c/h/s. */
	cy = partp->dp_start / dos_cylsecs;
	sec = partp->dp_start % dos_sectors + 1;
	if (cy > MAXCYL && C_flag == 0) {
		printf("Warning: starting cylinder wraps, using all 1's\n");
		partp->dp_shd = -1;
		partp->dp_scyl = -1;
		partp->dp_ssect = -1;
	} else {
		partp->dp_shd = partp->dp_start % dos_cylsecs / dos_sectors;
		partp->dp_scyl = DOSCYL(cy);
		partp->dp_ssect = DOSSECT(sec, cy);
	}

	/* End c/h/s. */
	end = partp->dp_start + partp->dp_size - 1;
	cy = end / dos_cylsecs;
	sec = end % dos_sectors + 1;
	if (cy > MAXCYL && C_flag == 0) {
		printf("Warning: ending cylinder wraps, using all 1's\n");
		partp->dp_ehd = -1;
		partp->dp_ecyl = -1;
		partp->dp_esect = -1;
	} else {
		partp->dp_ehd = end % dos_cylsecs / dos_sectors;
		partp->dp_ecyl = DOSCYL(cy);
		partp->dp_esect = DOSSECT(sec, cy);
	}
}

static void
erase_partition(int i)
{
	struct dos_partition *partp;
	off_t ioarg[2];

	partp = ((struct dos_partition *) &mboot.parts) + i;
	printf("Erase sectors: start=%u size=%u\n",
	       partp->dp_start, partp->dp_size);

	ioarg[0] = partp->dp_start * secsize;
	ioarg[1] = partp->dp_size * secsize;

	if (ioctl(fd, DAIOCTRIM, ioarg) < 0) {
		printf("Trim error %s\n", strerror(errno));
		printf("Continuing\n");
	} else {
		printf("Trim completed ok\n");
	}
}

	/* Getting device status */

static int
open_disk(void)
{
	struct stat st;

	if (stat(disk, &st) == -1) {
		if (errno == ENOENT)
			return -2;
		warnx("can't get file status of %s", disk);
		return -1;
	}
	if (!(st.st_mode & S_IFCHR) && p_flag == 0)
		warnx("device %s is not character special", disk);
	fd = open(disk, (x_flag || a_flag || I_flag || B_flag || u_flag) ?
			O_RDWR : O_RDONLY);
	if (fd == -1) {
		if (errno == ENXIO)
			return -2;
		warnx("can't open device %s", disk);
		return -1;
	}
	if (get_params() == -1) {
		warnx("can't get disk parameters on %s", disk);
		return -1;
	}
	return fd;
}

static ssize_t
read_disk(off_t sector, void *buf)
{
	int size;

	lseek(fd, sector * 512, 0);
	if (secsize == 0) {
		for (secsize = MIN_SEC_SIZE;
		     secsize <= MAX_SEC_SIZE;
		     secsize *= 2)
		{
			/* try the read */
			errno = 0;
			size = read(fd, buf, secsize);
			if (size == secsize)
				return secsize; /* it worked so return */
		}
	} else {
		size = read(fd, buf, secsize);
		if (size == secsize)
			return secsize;
	}

	warn("failed to read sector %ju", (uintmax_t)sector);
	return -1;
}

static ssize_t
write_disk(off_t sector, void *buf)
{
	lseek(fd, sector * 512, 0);
	/* write out in the size that the read_disk found worked */
	return write(fd, buf, secsize);
}

static int
get_params(void)
{
	struct partinfo partinfo;	/* disk parameters */
	struct stat st;

	/*
	* NOTE: When faking up the CHS for a file image (e.g. for USB),
	*       we must use max values.  If we do not then an overflowed
	*       cylinder count will, by convention, set the CHS fields to
	*       all 1's.  The heads and sectors in the CHS fields will then
	*       exceed the basic geometry which can cause BIOSes to brick.
	*/
	if (ioctl(fd, DIOCGPART, &partinfo) == -1) {
		if (p_flag && fstat(fd, &st) == 0 && st.st_size) {
			sectors = MAX_SECTORS_PER_TRACK;
			heads = MAX_HEADS;
			cylsecs = heads * sectors;
			cyls = st.st_size / 512 / cylsecs;
		} else {
			warnx("can't get disk parameters on %s; "
			      "supplying dummy ones", disk);
			heads = 1;
			cylsecs = heads * sectors;
		}
	} else {
		heads = partinfo.d_nheads;
		sectors = partinfo.d_secpertrack;
		cylsecs = heads * sectors;
		secsize = partinfo.media_blksize;
		cyls = partinfo.media_blocks / (heads * sectors);
		if ((u_int)cyls != partinfo.d_ncylinders) {
			printf("Warning: disk %s provided wrong ncylinders %u; "
			       "recalculating to %d\n",
			       disk, partinfo.d_ncylinders, cyls);
		}
	}

	dos_cyls = cyls;
	dos_heads = heads;
	dos_sectors = sectors;
	dos_cylsecs = cylsecs;
	disksecs = (int64_t)cyls * heads * sectors;

	return (0);
}

static int
read_s0(void)
{
	mboot.bootinst_size = secsize;
	if (mboot.bootinst != NULL)
		free(mboot.bootinst);
	if ((mboot.bootinst = malloc(mboot.bootinst_size)) == NULL) {
		warnx("unable to allocate buffer to read fdisk "
		      "partition table");
		return -1;
	}
	if (read_disk(0, mboot.bootinst) == -1) {
		warnx("can't read fdisk partition table");
		return -1;
	}
	if (*(uint16_t *)&mboot.bootinst[DOSMAGICOFF] != DOSMAGIC) {
		warnx("invalid fdisk partition table found");
		/* So should we initialize things */
		return -1;
	}
	memcpy(mboot.parts, &mboot.bootinst[DOSPARTOFF], sizeof(mboot.parts));
	return 0;
}

static int
write_s0(void)
{
#ifdef NOT_NOW
	int	flag = 1;
#endif
	int	sector;

	memcpy(&mboot.bootinst[DOSPARTOFF], mboot.parts, sizeof(mboot.parts));
	/*
	 * write enable label sector before write (if necessary),
	 * disable after writing.
	 * needed if the disklabel protected area also protects
	 * sector 0. (e.g. empty disk)
	 */
#ifdef NOT_NOW
	if (ioctl(fd, DIOCWLABEL, &flag) < 0)
		warn("ioctl DIOCWLABEL");
#endif
	for(sector = 0; sector < mboot.bootinst_size / secsize; sector++)
		if (write_disk(sector,
			       &mboot.bootinst[sector * secsize]) == -1) {
			warn("can't write fdisk partition table");
#ifdef NOT_NOW
			flag = 0;
			ioctl(fd, DIOCWLABEL, &flag);
#endif
			return -1;
		}
#ifdef NOT_NOW
	flag = 0;
	ioctl(fd, DIOCWLABEL, &flag);
#endif
	return(0);
}

static int
ok(const char *str)
{
	printf("%s [n] ", str);
	fflush(stdout);
	if (fgets(lbuf, LBUF, stdin) == NULL)
		exit(1);
	lbuf[strlen(lbuf)-1] = 0;

	if (strcmp(lbuf, "yes") == 0 || strcmp(lbuf, "YES") == 0 ||
	    strcmp(lbuf, "y") == 0 || strcmp(lbuf, "Y") == 0)
		return 1;
	else
		return 0;
}

static int
decimal(const char *str, int *num, int deflt)
{
	int acc = 0, c;
	char *cp;

	while (1) {
		printf("Supply a decimal value for \"%s\" [%d] ", str, deflt);
		fflush(stdout);
		if (fgets(lbuf, LBUF, stdin) == NULL)
			exit(1);
		lbuf[strlen(lbuf)-1] = 0;

		if (!*lbuf)
			return 0;

		cp = lbuf;
		while ((c = *cp) && (c == ' ' || c == '\t'))
			cp++;
		if (!c)
			return 0;
		while ((c = *cp++)) {
			if (c <= '9' && c >= '0')
				acc = acc * 10 + c - '0';
			else
				break;
		}
		if (c == ' ' || c == '\t')
			while ((c = *cp) && (c == ' ' || c == '\t'))
				cp++;
		if (!c) {
			*num = acc;
			return 1;
		} else {
			printf("%s is an invalid decimal number.  Try again.\n",
			       lbuf);
		}
	}
}

static const char *
get_type(int type)
{
	int	numentries = NELEM(dos_ptypes);
	int	counter = 0;
	const struct dos_ptype *ptr = dos_ptypes;

	while (counter < numentries) {
		if (ptr->type == type)
			return (ptr->name);
		ptr++;
		counter++;
	}

	return("unknown");
}

static void
parse_config_line(char *line, CMD *command)
{
	char	*cp, *end;

	cp = line;
	/* dirty trick used to insure one exit point for this function */
	while (1) {
		memset(command, 0, sizeof(*command));

		while (isspace(*cp))
			++cp;
		if (*cp == '\0' || *cp == '#')
			break;

		command->cmd = *cp++;

		/* Parse args */
		while (1) {
			while (isspace(*cp))
				++cp;
			if (*cp == '#')
				break;		/* found comment */
			if (isalpha(*cp))
				command->args[command->n_args].argtype = *cp++;
			if (!isdigit(*cp))
				break;		/* assume end of line */

			end = NULL;
			command->args[command->n_args].arg_val =
				strtoll(cp, &end, 0);
			if (cp == end)
				break;		/* couldn't parse number */
			cp = end;
			command->n_args++;
		}
		break;
	}
}

static int
process_geometry(CMD *command)
{
	int status = 1, i;

	while (1) {
		geom_processed = 1;
		if (part_processed) {
			warnx("ERROR line %d: the geometry specification line "
			      "must occur before all partition specifications",
			      current_line_number);
			status = 0;
			break;
		}
		if (command->n_args != 3) {
			warnx("ERROR line %d: incorrect number of "
			      "geometry args", current_line_number);
			status = 0;
			break;
		}
		dos_cyls = -1;
		dos_heads = -1;
		dos_sectors = -1;
		for (i = 0; i < 3; ++i) {
			switch (command->args[i].argtype) {
			case 'c':
				dos_cyls = command->args[i].arg_val;
				break;
			case 'h':
				dos_heads = command->args[i].arg_val;
				break;
			case 's':
				dos_sectors = command->args[i].arg_val;
				break;
			default:
				warnx("ERROR line %d: unknown geometry "
				      "arg type: '%c' (0x%02x)",
				      current_line_number,
				      command->args[i].argtype,
				      command->args[i].argtype);
				status = 0;
				break;
			}
		}
		if (status == 0)
			break;

		dos_cylsecs = dos_heads * dos_sectors;

		/*
		 * Do sanity checks on parameter values
		 */
		if (dos_cyls < 0) {
			warnx("ERROR line %d: number of cylinders "
			      "not specified", current_line_number);
			status = 0;
		}
		if (dos_cyls == 0 || dos_cyls > 1024) {
			warnx("WARNING line %d: number of cylinders (%d) "
			      "may be out-of-range\n"
			      "(must be within 1-1024 for normal BIOS "
			      "operation, unless the entire disk "
			      "is dedicated to DragonFly)",
			      current_line_number, dos_cyls);
		}
		if (dos_heads < 0) {
			warnx("ERROR line %d: number of heads not specified",
			      current_line_number);
			status = 0;
		} else if (dos_heads < 1 || dos_heads > 256) {
			warnx("ERROR line %d: number of heads must be within "
			      "(1-256)", current_line_number);
			status = 0;
		}
		if (dos_sectors < 0) {
			warnx("ERROR line %d: number of sectors not specified",
			      current_line_number);
			status = 0;
		} else if (dos_sectors < MIN_SECTORS_PER_TRACK ||
			   dos_sectors > MAX_SECTORS_PER_TRACK) {
			warnx("ERROR line %d: number of sectors must be within "
			      "(1-63)", current_line_number);
			status = 0;
		}

		break;
	}

	return (status);
}

static int
process_partition(CMD *command)
{
	int			status = 0, part;
	uint32_t		prev_head_boundary, prev_cyl_boundary;
	uint32_t		adj_size, max_end;
	struct dos_partition	*partp;

	while (1) {
		part_processed = 1;
		if (command->n_args != 4) {
			warnx("ERROR line %d: incorrect number of "
			      "partition args", current_line_number);
			break;
		}

		part = command->args[0].arg_val;
		if (part < 1 || part > NDOSPART) {
			warnx("ERROR line %d: invalid partition number %d",
			      current_line_number, part);
			break;
		}

		partp = ((struct dos_partition *) &mboot.parts) + part - 1;
		bzero((char *)partp, sizeof (struct dos_partition));
		partp->dp_typ = command->args[1].arg_val;
		partp->dp_start = command->args[2].arg_val;
		partp->dp_size = command->args[3].arg_val;
		max_end = partp->dp_start + partp->dp_size;

		if (partp->dp_typ == 0) {
			/*
			 * Get out, the partition is marked as unused.
			 * Ensure that it's unused.
			 */
			bzero((char *)partp, sizeof (struct dos_partition));
			status = 1;
			break;
		}

		/*
		 * Adjust start upwards, if necessary, to fall on an head
		 * boundary.
		 */
		if (partp->dp_start % dos_sectors != 0) {
			prev_head_boundary =
				rounddown(partp->dp_start, dos_sectors);
			if (max_end < (uint32_t)dos_sectors ||
			    prev_head_boundary > max_end - dos_sectors) {
				/*
				 * Can't go past end of partition
				 */
				warnx("ERROR line %d: unable to adjust start "
				      "of partition %d to fall on "
				      "a head boundary",
				      current_line_number, part);
				break;
			}
			warnx("WARNING: adjusting start offset of "
			      "partition %d from %u to %u, "
			      "to fall on a head boundary",
			      part, (u_int)partp->dp_start,
			      (u_int)(prev_head_boundary + dos_sectors));
			partp->dp_start = prev_head_boundary + dos_sectors;
		}

		/*
		 * Adjust size downwards, if necessary, to fall on a cylinder
		 * boundary.
		 */
		prev_cyl_boundary = rounddown(partp->dp_start + partp->dp_size,
					      dos_cylsecs);
		if (prev_cyl_boundary > partp->dp_start) {
			adj_size = prev_cyl_boundary - partp->dp_start;
		} else {
			warnx("ERROR: could not adjust partition to start on a "
			      "head boundary and end on a cylinder boundary.");
			return (0);
		}
		if (adj_size != partp->dp_size) {
			warnx("WARNING: adjusting size of partition %d from "
			      "%u to %u to end on a cylinder boundary",
			      part, (u_int)partp->dp_size, (u_int)adj_size);
			partp->dp_size = adj_size;
		}
		if (partp->dp_size == 0) {
			warnx("ERROR line %d: size of partition %d is zero",
			      current_line_number, part);
			break;
		}

		dos(partp);
		status = 1;
		break;
	}

	return (status);
}

static int
process_active(CMD *command)
{
	int			status = 0, part, i;
	struct dos_partition	*partp;

	while (1) {
		active_processed = 1;
		if (command->n_args != 1) {
			warnx("ERROR line %d: incorrect number of active args",
			      current_line_number);
			status = 0;
			break;
		}
		part = command->args[0].arg_val;
		if (part < 1 || part > NDOSPART) {
			warnx("ERROR line %d: invalid partition number %d",
			      current_line_number, part);
			break;
		}
		/*
		 * Reset active partition
		 */
		partp = ((struct dos_partition *) &mboot.parts);
		for (i = 0; i < NDOSPART; i++)
			partp[i].dp_flag = 0;
		partp[part-1].dp_flag = ACTIVE;

		status = 1;
		break;
	}

	return (status);
}

static int
process_line(char *line)
{
	CMD	command;
	int	status = 1;

	while (1) {
		parse_config_line(line, &command);
		switch (command.cmd) {
		case 0:
			/* Comment or blank line */
			break;
		case 'g':
			/* Set geometry */
			status = process_geometry(&command);
			break;
		case 'p':
			status = process_partition(&command);
			break;
		case 'a':
			status = process_active(&command);
			break;
		default:
			status = 0;
			break;
		}
		break;
	}

	return (status);
}

static int
read_config(char *config_file)
{
	FILE	*fp = NULL;
	int	status = 1;
	char	buf[1010];

	/* dirty trick used to insure one exit point for this function */
	while (1) {
		if (strcmp(config_file, "-") != 0) {
			/* We're not reading from stdin */
			if ((fp = fopen(config_file, "r")) == NULL) {
				status = 0;
				break;
			}
		} else {
			fp = stdin;
		}

		current_line_number = 0;
		while (!feof(fp)) {
			if (fgets(buf, sizeof(buf), fp) == NULL)
				break;
			++current_line_number;
			status = process_line(buf);
			if (status == 0)
				break;
		}

		break;
	}

	if (fp) {
		/*
		 * It doesn't matter if we're reading from stdin, as we've
		 * reached EOF.
		 */
		fclose(fp);
	}

	return (status);
}

static void
reset_boot(void)
{
	int i;
	struct dos_partition *partp;

	init_boot();

	for (i = 0; i < NDOSPART; ++i) {
		partp = ((struct dos_partition *) &mboot.parts) + i;
		bzero((char *)partp, sizeof(struct dos_partition));
	}
}

static int
sanitize_partition(struct dos_partition *partp)
{
	uint32_t prev_head_boundary, prev_cyl_boundary;
	uint32_t max_end, size, start;

	start = partp->dp_start;
	size = partp->dp_size;
	max_end = start + size;

	/* Only allow a zero size if the partition is being marked unused. */
	if (size == 0) {
		if (start == 0 && partp->dp_typ == 0)
			return (1);
		warnx("ERROR: size of partition is zero");
		return (0);
	}

	/* Return if no adjustment is necessary. */
	if (start % dos_sectors == 0 && (start + size) % dos_sectors == 0)
		return (1);

	if (start == 0) {
		warnx("WARNING: partition overlaps with partition table");
		if (ok("Correct this automatically?"))
			start = dos_sectors;
	}
	if (start % dos_sectors != 0)
		warnx("WARNING: partition does not start on a head boundary");
	if ((start  +size) % dos_sectors != 0)
		warnx("WARNING: partition does not end on a cylinder boundary");
	warnx("WARNING: this may confuse the BIOS or some operating systems");

	if (!ok("Correct this automatically?"))
		return (1);

	/*
	 * Adjust start upwards, if necessary, to fall on an head boundary.
	 */
	if (start % dos_sectors != 0) {
		prev_head_boundary = rounddown(start, dos_sectors);
		if (max_end < (uint32_t)dos_sectors ||
		    prev_head_boundary >= max_end - dos_sectors) {
			/* Can't go past end of partition */
			warnx("ERROR: unable to adjust start of partition "
			      "to fall on a head boundary");
			return (0);
		}
		start = prev_head_boundary + dos_sectors;
	}

	/*
	 * Adjust size downwards, if necessary, to fall on a cylinder boundary.
	 */
	prev_cyl_boundary = rounddown(start + size, dos_cylsecs);
	if (prev_cyl_boundary > start) {
		size = prev_cyl_boundary - start;
	} else {
		warnx("ERROR: could not adjust partition to start on a "
		      "head boundary and end on a cylinder boundary.");
		return (0);
	}

	/* Finally, commit any changes to partp and return. */
	if (start != partp->dp_start) {
		warnx("WARNING: adjusting start offset of partition to %u",
	              (u_int)start);
		partp->dp_start = start;
	}
	if (size != partp->dp_size) {
		warnx("WARNING: adjusting size of partition to %u",
		      (u_int)size);
		partp->dp_size = size;
	}

	return (1);
}
