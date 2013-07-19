/*-
 * Copyright (c) 2002 Juli Mallett.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * @(#)uname.c	8.2 (Berkeley) 5/4/95
 * $FreeBSD: src/usr.bin/uname/uname.c,v 1.4.6.2 2002/10/17 07:47:29 jmallett Exp $
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/varsym.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define	MFLAG	0x0001
#define	NFLAG	0x0002
#define	PFLAG	0x0004
#define	RFLAG	0x0008
#define	SFLAG	0x0010
#define	VFLAG	0x0020
#define	IFLAG   0x0040
#define	GFLAG   0x0080
#define	GFLAG2  0x0100

typedef void (*get_t)(void);
get_t get_ident, get_machine, get_hostname, get_arch;
get_t get_release, get_sysname, get_version, get_pkgabi;
  
void native_ident(void);
void native_machine(void);
void native_hostname(void);
void native_arch(void);
void native_release(void);
void native_sysname(void);
void native_version(void);
void native_pkgabi(void);
void print_uname(void);
void setup_get(void);
void usage(void);

static char *ident, *machine, *hostname, *arch;
static char *release, *sysname, *version, *pkgabi;
static int space;
static u_int flags;

int
main(int argc, char *argv[])
{
	int ch;

	setup_get();

	while ((ch = getopt(argc, argv, "aimnprsvP")) != -1) {
		switch(ch) {
		case 'a':
			flags |= (MFLAG | NFLAG | RFLAG | SFLAG | VFLAG);
			break;
		case 'i':
			flags |= IFLAG;
			break;
		case 'm':
			flags |= MFLAG;
			break;
		case 'n':
			flags |= NFLAG;
			break;
		case 'p':
			flags |= PFLAG;
			break;
		case 'r':
			flags |= RFLAG;
			break;
		case 's':
			flags |= SFLAG;
			break;
		case 'v':
			flags |= VFLAG;
			break;
		case 'P':
			if (flags & GFLAG)	/* don't adjust odd numbers */
				flags |= GFLAG2;
			flags |= GFLAG;
			break;
		case '?':
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	if (!flags)
		flags |= SFLAG;

	print_uname();
	exit(0);
}

/*
 * Overrides.
 *
 * UNAME_x env variables have the highest priority
 * UNAME_x varsyms have the next highest priority
 * values retrieved from sysctls have the lowest priority
 */
static
void
CHECK_ENV(const char *envname, get_t *getp, get_t nativep, char **varp)
{
	char buf[1024];

	if ((*varp = getenv(envname)) == NULL) {
		if (varsym_get(VARSYM_ALL_MASK, envname,
			       buf, sizeof(buf)) < 0) {
			*getp = nativep;
			return;
		}
		*varp = strdup(buf);
	}
	*getp = NULL;
}

void
setup_get(void)
{
	CHECK_ENV("UNAME_s", &get_sysname, native_sysname, &sysname);
	CHECK_ENV("UNAME_n", &get_hostname, native_hostname, &hostname);
	CHECK_ENV("UNAME_r", &get_release, native_release, &release);
	CHECK_ENV("UNAME_v", &get_version, native_version, &version);
	CHECK_ENV("UNAME_m", &get_machine, native_machine, &machine);
	CHECK_ENV("UNAME_p", &get_arch, native_arch, &arch);
	CHECK_ENV("UNAME_i", &get_ident, native_ident, &ident);
	CHECK_ENV("UNAME_G", &get_pkgabi, native_pkgabi, &pkgabi);
}

#define	PRINT_FLAG(flags,flag,var)		\
	if ((flags & flag) == flag) {		\
		if (space)			\
			printf(" ");		\
		else				\
			space++;		\
		if (get_##var != NULL)		\
			(*get_##var)();		\
		printf("%s", var);		\
	}

void
print_uname(void)
{
	PRINT_FLAG(flags, SFLAG, sysname);
	PRINT_FLAG(flags, NFLAG, hostname);
	PRINT_FLAG(flags, RFLAG, release);
	PRINT_FLAG(flags, VFLAG, version);
	PRINT_FLAG(flags, MFLAG, machine);
	PRINT_FLAG(flags, PFLAG, arch);
	PRINT_FLAG(flags, IFLAG, ident);
	PRINT_FLAG(flags, GFLAG, pkgabi);
	printf("\n");
}

#define	NATIVE_SYSCTL2_GET(var,mib0,mib1)	\
void						\
native_##var(void)				\
{						\
	int mib[] = { (mib0), (mib1) };		\
	size_t len;				\
	static char buf[1024];			\
	char **varp = &(var);			\
						\
	len = sizeof buf;			\
	if (sysctl(mib, sizeof mib / sizeof mib[0],	\
	   &buf, &len, NULL, 0) == -1)		\
		err(1, "sysctl");

#define	NATIVE_SYSCTLNAME_GET(var,name)		\
void						\
native_##var(void)				\
{						\
	size_t len;				\
	static char buf[1024];			\
	char **varp = &(var);			\
						\
	len = sizeof buf;			\
	if (sysctlbyname(name, &buf, &len, NULL,\
	    0) == -1)				\
		err(1, "sysctlbyname");

#define	NATIVE_SET				\
	*varp = buf;				\
	return;					\
}	struct __hack

#define	NATIVE_BUFFER	(buf)
#define	NATIVE_LENGTH	(len)

NATIVE_SYSCTL2_GET(sysname, CTL_KERN, KERN_OSTYPE) {
} NATIVE_SET;

NATIVE_SYSCTL2_GET(hostname, CTL_KERN, KERN_HOSTNAME) {
} NATIVE_SET;

NATIVE_SYSCTL2_GET(release, CTL_KERN, KERN_OSRELEASE) {
} NATIVE_SET;

NATIVE_SYSCTL2_GET(version, CTL_KERN, KERN_VERSION) {
	size_t n;
	char *p;

	p = NATIVE_BUFFER;
	n = NATIVE_LENGTH;
	for (; n--; ++p)
		if (*p == '\n' || *p == '\t')
			*p = ' ';
} NATIVE_SET;

NATIVE_SYSCTL2_GET(machine, CTL_HW, HW_MACHINE) {
} NATIVE_SET;

NATIVE_SYSCTL2_GET(arch, CTL_HW, HW_MACHINE_ARCH) {
} NATIVE_SET;

NATIVE_SYSCTLNAME_GET(ident, "kern.ident") {
} NATIVE_SET;

void						\
native_pkgabi(void)				\
{
	char osrel[64];
	char mach[64];
	size_t len;
	double d;

	len = sizeof(osrel);
	if (sysctlbyname("kern.osrelease", osrel, &len, NULL, 0) == -1)
		err(1, "sysctlbyname");
	len = sizeof(mach);
	if (sysctlbyname("hw.machine", mach, &len, NULL, 0) == -1)
		err(1, "sysctlbyname");

	/*
	 * Current convention is to adjust odd release numbers to even.
	 */
	d = strtod(osrel, NULL);
	if ((flags & GFLAG2) == 0) {
		if ((int)(d * 10) & 1)
			d = d + 0.1;	/* force to nearest even release */
	}

	/*
	 * pkgng expects the ABI in a different form
	 */
	if (strcmp(mach, "x86_64") == 0)
		snprintf(mach, sizeof(mach), "x86:64");
	else if (strcmp(mach, "i386") == 0)
		snprintf(mach, sizeof(mach), "x86:32");

	asprintf(&pkgabi, "dragonfly:%3.1f:%s", d, mach);
}

void
usage(void)
{
	fprintf(stderr, "usage: uname [-aimnprsv]\n");
	exit(1);
}
