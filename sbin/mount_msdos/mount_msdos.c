/*	$NetBSD: mount_msdos.c,v 1.18 1997/09/16 12:24:18 lukem Exp $	*/

/*
 * Copyright (c) 1994 Christopher G. Demetriou
 * All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sbin/mount_msdos/mount_msdos.c,v 1.19.2.1 2000/07/20 10:35:13 kris Exp $
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/iconv.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <vfs/msdosfs/msdosfsmount.h>

#include <ctype.h>
#include <err.h>
#include <grp.h>
#include <locale.h>
#include <mntopts.h>
#include <pwd.h>
#include <stdio.h>
/* must be after stdio to declare fparseln */
#include <libutil.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

/*
 * XXX - no way to specify "foo=<bar>"-type options; that's what we'd
 * want for "-u", "-g", "-m", "-L", and "-D".
 */
static struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_FORCE,
	MOPT_SYNC,
	MOPT_UPDATE,
#ifdef MSDOSFSMNT_GEMDOSFS
	{ "gemdosfs", 0, MSDOSFSMNT_GEMDOSFS, 1 },
#endif
	{ "shortnames", 0, MSDOSFSMNT_SHORTNAME, 1 },
	{ "longnames", 0, MSDOSFSMNT_LONGNAME, 1 },
	{ "nowin95", 0, MSDOSFSMNT_NOWIN95, 1 },
	MOPT_NULL
};

static gid_t	a_gid(char *);
static uid_t	a_uid(char *);
static mode_t	a_mask(char *);
static void	usage(void) __dead2;
int set_charset(struct msdosfs_args*, const char*, const char*);

int
main(int argc, char **argv)
{
	struct msdosfs_args args;
	struct stat sb;
	int c, error, mntflags, set_gid, set_uid, set_mask;
	char *dev, *dir, mntpath[MAXPATHLEN], *csp;
	const char *quirk = NULL;
        char *cs_local = NULL;
        char *cs_dos = NULL;
	struct vfsconf vfc;
	mntflags = set_gid = set_uid = set_mask = 0;
	memset(&args, '\0', sizeof(args));
	args.magic = MSDOSFS_ARGSMAGIC;

	while ((c = getopt(argc, argv, "sl9u:g:m:o:L:D:")) != -1) {
		switch (c) {
#ifdef MSDOSFSMNT_GEMDOSFS
		case 'G':
			args.flags |= MSDOSFSMNT_GEMDOSFS;
			break;
#endif
		case 's':
			args.flags |= MSDOSFSMNT_SHORTNAME;
			break;
		case 'l':
			args.flags |= MSDOSFSMNT_LONGNAME;
			break;
		case '9':
			args.flags |= MSDOSFSMNT_NOWIN95;
			break;
		case 'u':
			args.uid = a_uid(optarg);
			set_uid = 1;
			break;
		case 'g':
			args.gid = a_gid(optarg);
			set_gid = 1;
			break;
		case 'm':
			args.mask = a_mask(optarg);
			set_mask = 1;
			break;
		case 'L':
                        if (setlocale(LC_CTYPE, optarg) == NULL)
                                err(EX_CONFIG, "%s", optarg);
                        csp = strchr(optarg,'.');
                        if (!csp)
                                err(EX_CONFIG, "%s", optarg);
			quirk = kiconv_quirkcs(csp + 1, KICONV_VENDOR_MICSFT);
			cs_local = strdup(quirk);
			args.flags |= MSDOSFSMNT_KICONV;
			break;
		case 'D':
			csp = optarg;
			cs_dos = strdup(optarg);
			args.flags |= MSDOSFSMNT_KICONV;
			break;
		case 'o':
			getmntopts(optarg, mopts, &mntflags, &args.flags);
			break;
		case '?':
		default:
			usage();
			break;
		}
	}

	if (optind + 2 != argc)
		usage();

	dev = argv[optind];
	dir = argv[optind + 1];

	/*
	 * Resolve the mountpoint with realpath(3) and remove unnecessary
	 * slashes from the devicename if there are any.
	 */
	checkpath(dir, mntpath);
	rmslashes(dev, dev);

	args.fspec = dev;
	args.export.ex_root = -2;	/* unchecked anyway on DOS fs */

        if (cs_local != NULL) {
                if (set_charset(&args, cs_local, cs_dos) == -1)
                        err(EX_OSERR, "msdos_iconv");
        } else if (cs_dos != NULL) {
                if (set_charset(&args, "ISO8859-1", cs_dos) == -1)
                        err(EX_OSERR, "msdos_iconv");
        }

	if (mntflags & MNT_RDONLY)
		args.export.ex_flags = MNT_EXRDONLY;
	else
		args.export.ex_flags = 0;
	if (!set_gid || !set_uid || !set_mask) {
		if (stat(mntpath, &sb) == -1)
			err(EX_OSERR, "stat %s", mntpath);

		if (!set_uid)
			args.uid = sb.st_uid;
		if (!set_gid)
			args.gid = sb.st_gid;
		if (!set_mask)
			args.mask = sb.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
	}

	error = getvfsbyname("msdos", &vfc);
	if (error && vfsisloadable("msdos")) {
		if (vfsload("msdos"))
			err(EX_OSERR, "vfsload(msdos)");
		endvfsent();	/* clear cache */
		error = getvfsbyname("msdos", &vfc);
	}
	if (error)
		errx(EX_OSERR, "msdos filesystem is not available");

	if (mount(vfc.vfc_name, mntpath, mntflags, &args) < 0)
		err(EX_OSERR, "%s", dev);

	exit (0);
}

static gid_t
a_gid(char *s)
{
	struct group *gr;
	char *gname;
	gid_t gid;

	if ((gr = getgrnam(s)) != NULL)
		gid = gr->gr_gid;
	else {
		for (gname = s; *s && isdigit(*s); ++s);
		if (!*s)
			gid = atoi(gname);
		else
			errx(EX_NOUSER, "unknown group id: %s", gname);
	}
	return (gid);
}

static uid_t
a_uid(char *s)
{
	struct passwd *pw;
	char *uname;
	uid_t uid;

	if ((pw = getpwnam(s)) != NULL)
		uid = pw->pw_uid;
	else {
		for (uname = s; *s && isdigit(*s); ++s);
		if (!*s)
			uid = atoi(uname);
		else
			errx(EX_NOUSER, "unknown user id: %s", uname);
	}
	return (uid);
}

static mode_t
a_mask(char *s)
{
	int done, rv;
	char *ep;

	done = 0;
	rv = -1;
	if (*s >= '0' && *s <= '7') {
		done = 1;
		rv = strtol(optarg, &ep, 8);
	}
	if (!done || rv < 0 || *ep)
		errx(EX_USAGE, "invalid file mode: %s", s);
	return (rv);
}

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n", 
	    "usage: mount_msdos [-9ls] [-D DOS_codepage] [-g gid] [-L locale]",
	    "                   [-m mask] [-o options] [-u uid] special node");
	exit(EX_USAGE);
}

int
set_charset(struct msdosfs_args *args, const char *cs_local, const char *cs_dos)
{
        int error;
        if (modfind("msdos_iconv") < 0) {
                if (kldload("msdos_iconv") < 0 || modfind("msdos_iconv") < 0)
		{
                        warnx("cannot find or load \"msdos_iconv\" kernel module");
                        return (-1);
                }
	}
	snprintf(args->cs_local, ICONV_CSNMAXLEN, "%s", cs_local);
        error = kiconv_add_xlat16_cspairs(ENCODING_UNICODE, cs_local);
        if (error)
                return (-1);
        if (!cs_dos)
		cs_dos = strdup("CP437");
	snprintf(args->cs_dos, ICONV_CSNMAXLEN, "%s", cs_dos);
	error = kiconv_add_xlat16_cspairs(cs_dos, cs_local);
	if (error)
		return (-1);
        return (0);
}
