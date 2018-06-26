/*
 * lsvfs - list loaded VFSes
 * Garrett A. Wollman, September 1994
 * This file is in the public domain.
 *
 * $FreeBSD: src/usr.bin/lsvfs/lsvfs.c,v 1.13.2.1 2001/07/30 09:59:16 dd Exp $
 */

#include <sys/param.h>
#include <sys/mount.h>

#include <err.h>
#include <stdio.h>
#include <string.h>

#define	FMT	"%-32.32s 0x%08x %5d  %s\n"
#define	HDRFMT	"%-32.32s %10s %5.5s  %s\n"
#define	DASHES	"-------------------------------- "	\
		"---------- -----  ---------------\n"

static struct flaglist {
	int		flag;
	const char	str[32]; /* must be longer than the longest one. */
} fl[] = {
	{ .flag = VFCF_STATIC, .str = "static", },
	{ .flag = VFCF_NETWORK, .str = "network", },
	{ .flag = VFCF_READONLY, .str = "read-only", },
	{ .flag = VFCF_SYNTHETIC, .str = "synthetic", },
	{ .flag = VFCF_LOOPBACK, .str = "loopback", },
	{ .flag = VFCF_UNICODE, .str = "unicode", },
};

static const char *fmt_flags(int);

int
main(int argc, char **argv)
{
	int rv = 0;
	struct vfsconf vfc;
	struct ovfsconf *ovfcp;
	argc--, argv++;

	setvfsent(1);

	printf(HDRFMT, "Filesystem", "Num", "Refs", "Flags");
	fputs(DASHES, stdout);

	if (argc) {
		for(; argc; argc--, argv++) {
			if (getvfsbyname(*argv, &vfc) == 0) {
				printf(FMT, vfc.vfc_name, vfc.vfc_typenum,
				    vfc.vfc_refcount,
				    fmt_flags(vfc.vfc_flags));
			} else {
				warnx("VFS %s unknown or not loaded", *argv);
				rv = 1;
			}
		}
	} else {
		while ((ovfcp = getvfsent()) != NULL) {
			if (getvfsbyname(ovfcp->vfc_name, &vfc) == 0) {
				printf(FMT, vfc.vfc_name, vfc.vfc_typenum,
				    vfc.vfc_refcount,
				    fmt_flags(vfc.vfc_flags));
			} else {
				warnx("VFS %s unknown or not loaded", *argv);
				rv = 1;
			}
		}
	}

	endvfsent();
	return rv;
}

static const char *
fmt_flags(int flags)
{
	static char buf[sizeof(struct flaglist) * sizeof(fl)];
	size_t i;

	buf[0] = '\0';
	for (i = 0; i < nitems(fl); i++)
		if (flags & fl[i].flag) {
			strlcat(buf, fl[i].str, sizeof(buf));
			strlcat(buf, ", ", sizeof(buf));
		}
	if (buf[0] != '\0')
		buf[strlen(buf) - 2] = '\0';
	return (buf);
}
