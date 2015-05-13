/*
 * Copyright (c) 1993
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
 * @(#) Copyright (c) 1993 The Regents of the University of California.  All rights reserved.
 * @(#)from: sysctl.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/sbin/sysctl/sysctl.c,v 1.25.2.11 2003/05/01 22:48:08 trhodes Exp $
 */

#ifdef __i386__
#include <sys/diskslice.h>	/* used for bootdev parsing */
#include <sys/reboot.h>		/* used for bootdev parsing */
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/sensors.h>
#include <sys/param.h>

#include <machine/inttypes.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int	aflag, bflag, dflag, eflag, Nflag, nflag, oflag, xflag;

static int	oidfmt(int *, size_t, char *, u_int *);
static void	parse(const char *);
static int	show_var(int *, size_t);
static int	sysctl_all(int *, size_t);
static void	set_T_dev_t(const char *, void **, size_t *);
static int	set_IK(const char *, int *);

static void
usage(void)
{

	fprintf(stderr, "%s\n%s\n",
	    "usage: sysctl [-bdeNnox] variable[=value] ...",
	    "       sysctl [-bdeNnox] -a");
	exit(1);
}

int
main(int argc, char **argv)
{
	int ch;
	setbuf(stdout,0);
	setbuf(stderr,0);

	while ((ch = getopt(argc, argv, "AabdeNnowxX")) != -1) {
		switch (ch) {
		case 'A':
			/* compatibility */
			aflag = oflag = 1;
			break;
		case 'a':
			aflag = 1;
			break;
		case 'b':
			bflag = 1;
			break;
		case 'd':
			dflag = 1;
			break;
		case 'e':
			eflag = 1;
			break;
		case 'N':
			Nflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'o':
			oflag = 1;
			break;
		case 'w':
			/* compatibility */
			/* ignored */
			break;
		case 'X':
			/* compatibility */
			aflag = xflag = 1;
			break;
		case 'x':
			xflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (Nflag && nflag)
		usage();
	if (aflag && argc == 0)
		exit(sysctl_all(0, 0));
	if (argc == 0)
		usage();
	while (argc-- > 0)
		parse(*argv++);
	exit(0);
}

/*
 * Parse a name into a MIB entry.
 * Lookup and print out the MIB entry if it exists.
 * Set a new value if requested.
 */
static void
parse(const char *string)
{
	size_t len;
	int i, j;
	void *newval = NULL;
	int intval;
	unsigned int uintval;
	long longval;
	unsigned long ulongval;
	size_t newsize = 0;
	quad_t quadval;
	u_quad_t uquadval;
	int mib[CTL_MAXNAME];
	char *cp, fmt[BUFSIZ];
	const char *name;
	char *name_allocated = NULL;
	u_int kind;

	if ((cp = strchr(string, '=')) != NULL) {
		if ((name_allocated = malloc(cp - string + 1)) == NULL)
			err(1, "malloc failed");
		strlcpy(name_allocated, string, cp - string + 1);
		name = name_allocated;
		
		while (isspace(*++cp))
			;

		newval = cp;
		newsize = strlen(cp);
	} else {
		name = string;
	}

	len = CTL_MAXNAME;
	if (sysctlnametomib(name, mib, &len) < 0) {
		if (errno == ENOENT) {
			errx(1, "unknown oid '%s'", name);
		} else {
			err(1, "sysctlnametomib(\"%s\")", name);
		}
	}

	if (oidfmt(mib, len, fmt, &kind))
		err(1, "couldn't find format of oid '%s'", name);

	if (newval == NULL) {
		if ((kind & CTLTYPE) == CTLTYPE_NODE) {
			sysctl_all(mib, len);
		} else {
			i = show_var(mib, len);
			if (!i && !bflag)
				putchar('\n');
		}
	} else {
		if ((kind & CTLTYPE) == CTLTYPE_NODE)
			errx(1, "oid '%s' isn't a leaf node", name);

		if (!(kind&CTLFLAG_WR))
			errx(1, "oid '%s' is read only", name);
	
		switch (kind & CTLTYPE) {
			case CTLTYPE_INT:
				if (!(strcmp(fmt, "IK") == 0)) {
					if (!set_IK(newval, &intval))
						errx(1, "invalid value '%s'",
						    (char *)newval);
				} else
					intval = (int) strtol(newval, NULL, 0);
				newval = &intval;
				newsize = sizeof(intval);
				break;
			case CTLTYPE_UINT:
				uintval = (int) strtoul(newval, NULL, 0);
				newval = &uintval;
				newsize = sizeof uintval;
				break;
			case CTLTYPE_LONG:
				longval = strtol(newval, NULL, 0);
				newval = &longval;
				newsize = sizeof longval;
				break;
			case CTLTYPE_ULONG:
				ulongval = strtoul(newval, NULL, 0);
				newval = &ulongval;
				newsize = sizeof ulongval;
				break;
			case CTLTYPE_STRING:
				break;
			case CTLTYPE_QUAD:
				quadval = strtoq(newval, NULL, 0);
				newval = &quadval;
				newsize = sizeof(quadval);
				break;
			case CTLTYPE_UQUAD:
				uquadval = strtouq(newval, NULL, 0);
				newval = &uquadval;
				newsize = sizeof(uquadval);
				break;
			case CTLTYPE_OPAQUE:
				if (strcmp(fmt, "T,dev_t") == 0 ||
				    strcmp(fmt, "T,udev_t") == 0
				) {
					set_T_dev_t((char*)newval, &newval,
						    &newsize);
					break;
				}
				/* FALLTHROUGH */
			default:
				errx(1, "oid '%s' is type %d,"
					" cannot set that", name,
					kind & CTLTYPE);
		}

		i = show_var(mib, len);
		if (sysctl(mib, len, 0, 0, newval, newsize) == -1) {
			if (!i && !bflag)
				putchar('\n');
			switch (errno) {
			case EOPNOTSUPP:
				errx(1, "%s: value is not available", 
					string);
			case ENOTDIR:
				errx(1, "%s: specification is incomplete", 
					string);
			case ENOMEM:
				errx(1, "%s: type is unknown to this program", 
					string);
			default:
				warn("%s", string);
				return;
			}
		}
		if (!bflag)
			printf(" -> ");
		i = nflag;
		nflag = 1;
		j = show_var(mib, len);
		if (!j && !bflag)
			putchar('\n');
		nflag = i;
	}

	if (name_allocated != NULL)
		free(name_allocated);
}

/* These functions will dump out various interesting structures. */

static int
S_clockinfo(int l2, void *p)
{
	struct clockinfo *ci = (struct clockinfo*)p;
	if (l2 != sizeof(*ci))
		err(1, "S_clockinfo %d != %zu", l2, sizeof(*ci));
	printf("{ hz = %d, tick = %d, tickadj = %d, profhz = %d, stathz = %d }",
		ci->hz, ci->tick, ci->tickadj, ci->profhz, ci->stathz);
	return (0);
}

static int
S_loadavg(int l2, void *p)
{
	struct loadavg *tv = (struct loadavg*)p;

	if (l2 != sizeof(*tv))
		err(1, "S_loadavg %d != %zu", l2, sizeof(*tv));

	printf("{ %.2f %.2f %.2f }",
		(double)tv->ldavg[0]/(double)tv->fscale,
		(double)tv->ldavg[1]/(double)tv->fscale,
		(double)tv->ldavg[2]/(double)tv->fscale);
	return (0);
}

static int
S_timespec(int l2, void *p)
{
	struct timespec *ts = (struct timespec*)p;
	time_t tv_sec;
	char *p1, *p2;

	if (l2 != sizeof(*ts))
		err(1, "S_timespec %d != %zu", l2, sizeof(*ts));
	printf("{ sec = %ld, nsec = %ld } ",
		ts->tv_sec, ts->tv_nsec);
	tv_sec = ts->tv_sec;
	p1 = strdup(ctime(&tv_sec));
	for (p2=p1; *p2 ; p2++)
		if (*p2 == '\n')
			*p2 = '\0';
	fputs(p1, stdout);
	return (0);
}

static int
S_timeval(int l2, void *p)
{
	struct timeval *tv = (struct timeval*)p;
	time_t tv_sec;
	char *p1, *p2;

	if (l2 != sizeof(*tv))
		err(1, "S_timeval %d != %zu", l2, sizeof(*tv));
	printf("{ sec = %ld, usec = %ld } ",
		tv->tv_sec, tv->tv_usec);
	tv_sec = tv->tv_sec;
	p1 = strdup(ctime(&tv_sec));
	for (p2=p1; *p2 ; p2++)
		if (*p2 == '\n')
			*p2 = '\0';
	fputs(p1, stdout);
	return (0);
}

static int
S_sensor(int l2, void *p)
{
	struct sensor *s = (struct sensor *)p;

	if (l2 != sizeof(*s)) {
		warnx("S_sensor %d != %zu", l2, sizeof(*s));
		return (1);
	}

	if (s->flags & SENSOR_FINVALID) {
		/*
		 * XXX: with this flag, the node should be entirely ignored,
		 * but as the magic-based sysctl(8) is not too flexible, we
		 * simply have to print out that the sensor is invalid.
		 */
		printf("invalid");
		return (0);
	}

	if (s->flags & SENSOR_FUNKNOWN)
		printf("unknown");
	else {
		switch (s->type) {
		case SENSOR_TEMP:
			printf("%.2f degC",
			    (s->value - 273150000) / 1000000.0);
			break;
		case SENSOR_FANRPM:
			printf("%jd RPM", (intmax_t)s->value);
			break;
		case SENSOR_VOLTS_DC:
			printf("%.2f VDC", s->value / 1000000.0);
			break;
		case SENSOR_AMPS:
			printf("%.2f A", s->value / 1000000.0);
			break;
		case SENSOR_WATTHOUR:
			printf("%.2f Wh", s->value / 1000000.0);
			break;
		case SENSOR_AMPHOUR:
			printf("%.2f Ah", s->value / 1000000.0);
			break;
		case SENSOR_INDICATOR:
			printf("%s", s->value ? "On" : "Off");
			break;
		case SENSOR_ECC:
		case SENSOR_INTEGER:
			printf("%jd", (intmax_t)s->value);
			break;
		case SENSOR_PERCENT:
			printf("%.2f%%", s->value / 1000.0);
			break;
		case SENSOR_LUX:
			printf("%.2f lx", s->value / 1000000.0);
			break;
		case SENSOR_DRIVE:
		{
			const char *name;

			switch (s->value) {
			case SENSOR_DRIVE_EMPTY:
				name = "empty";
				break;
			case SENSOR_DRIVE_READY:
				name = "ready";
				break;
			case SENSOR_DRIVE_POWERUP:
				name = "powering up";
				break;
			case SENSOR_DRIVE_ONLINE:
				name = "online";
				break;
			case SENSOR_DRIVE_IDLE:
				name = "idle";
				break;
			case SENSOR_DRIVE_ACTIVE:
				name = "active";
				break;
			case SENSOR_DRIVE_REBUILD:
				name = "rebuilding";
				break;
			case SENSOR_DRIVE_POWERDOWN:
				name = "powering down";
				break;
			case SENSOR_DRIVE_FAIL:
				name = "failed";
				break;
			case SENSOR_DRIVE_PFAIL:
				name = "degraded";
				break;
			default:
				name = "unknown";
				break;
			}
			printf("%s", name);
			break;
		}
		case SENSOR_TIMEDELTA:
			printf("%.6f secs", s->value / 1000000000.0);
			break;
		default:
			printf("unknown");
		}
	}

	if (s->desc[0] != '\0')
		printf(" (%s)", s->desc);

	switch (s->status) {
	case SENSOR_S_UNSPEC:
		break;
	case SENSOR_S_OK:
		printf(", OK");
		break;
	case SENSOR_S_WARN:
		printf(", WARNING");
		break;
	case SENSOR_S_CRIT:
		printf(", CRITICAL");
		break;
	case SENSOR_S_UNKNOWN:
		printf(", UNKNOWN");
		break;
	}

	if (s->tv.tv_sec) {
		time_t t = s->tv.tv_sec;
		char ct[26];

		ctime_r(&t, ct);
		ct[19] = '\0';
		printf(", %s.%03ld", ct, s->tv.tv_usec / 1000);
	}

	return (0);
}

static int
T_dev_t(int l2, void *p)
{
	dev_t *d = (dev_t *)p;
	if (l2 != sizeof(*d))
		err(1, "T_dev_T %d != %zu", l2, sizeof(*d));
	if ((int)(*d) != -1) {
		if (minor(*d) > 255 || minor(*d) < 0)
			printf("{ major = %d, minor = 0x%x }",
				major(*d), minor(*d));
		else
			printf("{ major = %d, minor = %d }",
				major(*d), minor(*d));
	}
	return (0);
}

static void
set_T_dev_t(const char *path, void **val, size_t *size)
{
	static struct stat statb;

	if (strcmp(path, "none") && strcmp(path, "off")) {
		int rc = stat (path, &statb);
		if (rc) {
			err(1, "cannot stat %s", path);
		}

		if (!S_ISCHR(statb.st_mode)) {
			errx(1, "must specify a device special file.");
		}
	} else {
		statb.st_rdev = NODEV;
	}
	*val = (char*) &statb.st_rdev;
	*size = sizeof statb.st_rdev;
}

static int
set_IK(const char *str, int *val)
{
	float temp;
	int len, kelv;
	const char *p;
	char *endptr;

	if ((len = strlen(str)) == 0)
		return (0);
	p = &str[len - 1];
	if (*p == 'C' || *p == 'F') {
		temp = strtof(str, &endptr);
		if (endptr == str || endptr != p)
			return 0;
		if (*p == 'F')
			temp = (temp - 32) * 5 / 9;
		kelv = temp * 10 + 2732;
	} else {
		/*
		 * I would like to just use, 0 but it would make numbers
		 * like '023' which were interpreted as decimal before
		 * suddenly interpreted as octal.
		 */
		if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
			kelv = (int)strtol(str, &endptr, 0);
		else
			kelv = (int)strtol(str, &endptr, 10);
		if (endptr == str || *endptr != '\0')
			return 0;
	}
	*val = kelv;
	return 1;
}

/*
 * These functions uses a presently undocumented interface to the kernel
 * to walk the tree and get the type so it can print the value.
 * This interface is under work and consideration, and should probably
 * be killed with a big axe by the first person who can find the time.
 * (be aware though, that the proper interface isn't as obvious as it
 * may seem, there are various conflicting requirements.
 */

static int
oidfmt(int *oid, size_t len, char *fmt, u_int *kind)
{
	int qoid[CTL_MAXNAME+2];
	u_char buf[BUFSIZ];
	int i;
	size_t j;

	qoid[0] = 0;
	qoid[1] = 4;
	memcpy(qoid + 2, oid, len * sizeof(int));

	j = sizeof(buf);
	i = sysctl(qoid, len + 2, buf, &j, 0, 0);
	if (i)
		err(1, "sysctl fmt %d %zu %d", i, j, errno);

	if (kind)
		*kind = *(u_int *)buf;

	if (fmt)
		strcpy(fmt, (char *)(buf + sizeof(u_int)));
	return 0;
}

#ifdef __i386__
/*
 * Code to map a bootdev major number into a suitable device name.
 * Major numbers are mapped into names as in boot2.c
 */
struct _foo {
	int majdev;
	const char *name;
} maj2name[] = {
	{ 30,	"ad" },
	{ 0,	"wd" },
	{ 1,	"wfd" },
	{ 2,	"fd" },
	{ 4,	"da" },
	{ -1,	NULL }	/* terminator */
};

static int
machdep_bootdev(u_long value)
{
	int majdev, unit, slice, part;
	struct _foo *p;

	if ((value & B_MAGICMASK) != B_DEVMAGIC) {
		printf("invalid (0x%08lx)", value);
		return 0;
	}
	majdev = B_TYPE(value);
	unit = B_UNIT(value);
	slice = B_SLICE(value);
	part = B_PARTITION(value);
	if (majdev == 2) {	/* floppy, as known to the boot block... */
		printf("/dev/fd%d", unit);
		return 0;
	}
	for (p = maj2name; p->name != NULL && p->majdev != majdev ; p++) ;
	if (p->name != NULL) {	/* found */
		if (slice == WHOLE_DISK_SLICE)
			printf("/dev/%s%d%c", p->name, unit, part);
		else
			printf("/dev/%s%ds%d%c",
			    p->name, unit, slice - BASE_SLICE + 1, part + 'a');
	} else
		printf("unknown (major %d unit %d slice %d part %d)",
			majdev, unit, slice, part);
	return 0;
}
#endif

/*
 * This formats and outputs the value of one variable
 *
 * Returns zero if anything was actually output.
 * Returns one if didn't know what to do with this.
 * Return minus one if we had errors.
 */

static int
show_var(int *oid, size_t nlen)
{
	u_char buf[BUFSIZ], *val = NULL, *p, *nul;
	char name[BUFSIZ], *fmt;
	const char *sep, *spacer;
	int qoid[CTL_MAXNAME+2];
	int i;
	size_t j, len;
	u_int kind;
	int (*func)(int, void *);
	int error = 0;

	qoid[0] = 0;
	memcpy(qoid + 2, oid, nlen * sizeof(int));

	qoid[1] = 1;
	j = sizeof(name);
	i = sysctl(qoid, nlen + 2, name, &j, 0, 0);
	if (i || !j)
		err(1, "sysctl name %d %zu %d", i, j, errno);

	if (Nflag) {
		printf("%s", name);
		return (0);
	}

	if (eflag)
		sep = "=";
	else
		sep = ": ";

	if (dflag) {	/* just print description */
		qoid[1] = 5;
		j = sizeof(buf);
		i = sysctl(qoid, nlen + 2, buf, &j, 0, 0);
		if (!nflag)
			printf("%s%s", name, sep);
		printf("%s", buf);
		return(0);
	}
	/* find an estimate of how much we need for this var */
	j = 0;
	i = sysctl(oid, nlen, 0, &j, 0, 0);
	j += j; /* we want to be sure :-) */

	val = malloc(j + 1);
	if (val == NULL)
		return (1);

	len = j;
	i = sysctl(oid, nlen, val, &len, 0, 0);
	if (i || !len) {
		error = 1;
		goto done;
	}

	if (bflag) {
		fwrite(val, 1, len, stdout);
		goto done;
	}

	val[len] = '\0';
	fmt = buf;
	oidfmt(oid, nlen, fmt, &kind);
	p = val;
	switch (*fmt) {
	case 'A':
		if (!nflag)
			printf("%s%s", name, sep);
		nul = memchr(p, '\0', len);
		fwrite(p, nul == NULL ? (int)len : nul - p, 1, stdout);
		return (0);
		
	case 'I':
		if (!nflag)
			printf("%s%s", name, sep);
		fmt++;
		spacer = "";
		while (len >= sizeof(int)) {
			if(*fmt == 'U')
				printf("%s%u", spacer, *(unsigned int *)p);
			else if (*fmt == 'K' && *(int *)p >= 0)
				printf("%s%.1fC", spacer, (*(int *)p - 2732) / 10.0);
			else
				printf("%s%d", spacer, *(int *)p);
			spacer = " ";
			len -= sizeof(int);
			p += sizeof(int);
		}
		goto done;

	case 'L':
		if (!nflag)
			printf("%s%s", name, sep);
		fmt++;
#ifdef __i386__
		if (!strcmp(name, "machdep.guessed_bootdev"))
			return machdep_bootdev(*(unsigned long *)p);
#endif
		spacer = "";
		while (len >= sizeof(long)) {
			if(*fmt == 'U')
				printf("%s%lu", spacer, *(unsigned long *)p);
			else
				printf("%s%ld", spacer, *(long *)p);
			spacer = " ";
			len -= sizeof(long);
			p += sizeof(long);
		}
		goto done;

	case 'P':
		if (!nflag)
			printf("%s%s", name, sep);
		printf("%p", *(void **)p);
		goto done;

	case 'Q':
		if (!nflag)
			printf("%s%s", name, sep);
		fmt++;
		spacer = "";
		while (len >= sizeof(quad_t)) {
			if(*fmt == 'U') {
				printf("%s%ju",
				       spacer, (uintmax_t)*(u_quad_t *)p);
			} else {
				printf("%s%jd",
				       spacer, (intmax_t)*(quad_t *)p);
			}
			spacer = " ";
			len -= sizeof(int64_t);
			p += sizeof(int64_t);
		}
		goto done;

	case 'T':
	case 'S':
		if (!oflag && !xflag) {
			i = 0;
			if (strcmp(fmt, "S,clockinfo") == 0)
				func = S_clockinfo;
			else if (strcmp(fmt, "S,timespec") == 0)
				func = S_timespec;
			else if (strcmp(fmt, "S,timeval") == 0)
				func = S_timeval;
			else if (strcmp(fmt, "S,loadavg") == 0)
				func = S_loadavg;
			else if (strcmp(fmt, "S,sensor") == 0)
				func = S_sensor;
			else if (strcmp(fmt, "T,dev_t") == 0)
				func = T_dev_t;
			else if (strcmp(fmt, "T,udev_t") == 0)
				func = T_dev_t;
			else
				func = NULL;
			if (func) {
				if (!nflag)
					printf("%s%s", name, sep);
				error = (*func)(len, p);
				goto done;
			}
		}
		/* FALL THROUGH */
	default:
		if (!oflag && !xflag) {
			error = 1;
			goto done;
		}
		if (!nflag)
			printf("%s%s", name, sep);
		printf("Format:%s Length:%zu Dump:0x", fmt, len);
		while (len-- && (xflag || p < val + 16))
			printf("%02x", *p++);
		if (!xflag && len > 16)
			printf("...");
		goto done;
	}

done:
	if (val != NULL)
		free(val);
	return (error);
}

static int
sysctl_all(int *oid, size_t len)
{
	int name1[22], name2[22];
	int retval;
	size_t i, l1, l2;

	name1[0] = 0;
	name1[1] = 2;
	l1 = 2;
	if (len) {
		memcpy(name1+2, oid, len * sizeof(int));
		l1 += len;
	} else {
		name1[2] = 1;
		l1++;
	}
	for (;;) {
		l2 = sizeof(name2);
		retval = sysctl(name1, l1, name2, &l2, 0, 0);
		if (retval < 0) {
			if (errno == ENOENT)
				return 0;
			else
				err(1, "sysctl(getnext) %d %zu", retval, l2);
		}

		l2 /= sizeof(int);

		if (l2 < len)
			return 0;

		for (i = 0; i < len; i++)
			if (name2[i] != oid[i])
				return 0;

		retval = show_var(name2, l2);
		if (retval == 0 && !bflag)
			putchar('\n');

		memcpy(name1+2, name2, l2 * sizeof(int));
		l1 = 2 + l2;
	}
}
