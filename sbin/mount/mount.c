/*-
 * Copyright (c) 1980, 1989, 1993, 1994
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
 * @(#) Copyright (c) 1980, 1989, 1993, 1994 The Regents of the University of California.  All rights reserved.
 * @(#)mount.c	8.25 (Berkeley) 5/8/95
 * $FreeBSD: src/sbin/mount/mount.c,v 1.39.2.3 2001/08/01 08:26:23 obrien Exp $
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/mountctl.h>
#define DKTYPENAMES
#include <sys/dtype.h>
#include <sys/diskslice.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <mntopts.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libutil.h>

#include "extern.h"
#include "pathnames.h"

/* `meta' options */
#define MOUNT_META_OPTION_FSTAB		"fstab"	
#define MOUNT_META_OPTION_CURRENT	"current"

int debug, fstab_style, verbose;

static char  *catopt(char *, const char *);
static struct statfs
             *getmntpt(const char *);
static int    hasopt(const char *, const char *);
static int    ismounted(struct fstab *, struct statfs *, int);
static int    isremountable(const char *);
static void   mangle(char *, int *, const char **);
static char  *update_options(char *, char *, int);
static int    mountfs(const char *, const char *, const char *,
		      int, const char *, const char *);
static void   remopt(char *, const char *);
static void   printdefvals(const struct statfs *);
static void   prmount(struct statfs *);
static void   putfsent(const struct statfs *);
static void   usage(void);
static char  *flags2opts(int);
static char  *xstrdup(const char *str);
static void   checkdisklabel(const char *devpath, const char **vfstypep);

/*
 * List of VFS types that can be remounted without becoming mounted on top
 * of each other.
 * XXX Is this list correct?
 */
static const char *remountable_fs_names[] = {
	"ufs", "ffs", "ext2fs", "hammer", "hammer2",
	NULL
};

static void
restart_mountd(void)
{
	struct pidfh *pfh;
	pid_t mountdpid;

	pfh = pidfile_open(_PATH_MOUNTDPID, 0600, &mountdpid);
	if (pfh != NULL) {
		/* Mountd is not running. */
		pidfile_remove(pfh);
		return;
	}
	if (errno != EEXIST) {
		/* Cannot open pidfile for some reason. */
		return;
	}
	/* We have mountd(8) PID in mountdpid varible, let's signal it. */
	if (kill(mountdpid, SIGHUP) == -1)
		err(1, "signal mountd");
}

int
main(int argc, char **argv)
{
	const char *mntfromname, **vfslist, *vfstype;
	struct fstab *fs;
	struct statfs *mntbuf;
	int all, ch, i, init_flags, mntsize, rval, have_fstab;
	char *options;

	all = init_flags = 0;
	options = NULL;
	vfslist = NULL;
	vfstype = "ufs";
	while ((ch = getopt(argc, argv, "adF:fo:prwt:uv")) != -1) {
		switch (ch) {
		case 'a':
			all = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'F':
			setfstab(optarg);
			break;
		case 'f':
			init_flags |= MNT_FORCE;
			break;
		case 'o':
			if (*optarg)
				options = catopt(options, optarg);
			break;
		case 'p':
			fstab_style = 1;
			verbose = 1;
			break;
		case 'r':
			options = catopt(options, "ro");
			break;
		case 't':
			if (vfslist != NULL)
				errx(1, "only one -t option may be specified");
			vfslist = makevfslist(optarg);
			vfstype = optarg;
			break;
		case 'u':
			init_flags |= MNT_UPDATE;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			options = catopt(options, "noro");
			break;
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

#define	BADTYPE(type)							\
	(strcmp(type, FSTAB_RO) &&					\
	    strcmp(type, FSTAB_RW) && strcmp(type, FSTAB_RQ))

	rval = 0;
	switch (argc) {
	case 0:
		if ((mntsize = getmntinfo(&mntbuf, MNT_NOWAIT)) == 0)
			err(1, "getmntinfo");
		if (all) {
			while ((fs = getfsent()) != NULL) {
				if (BADTYPE(fs->fs_type))
					continue;
				if (checkvfsname(fs->fs_vfstype, vfslist))
					continue;
				if (hasopt(fs->fs_mntops, "noauto"))
					continue;
				if (!(init_flags & MNT_UPDATE) &&
				    ismounted(fs, mntbuf, mntsize))
					continue;
				options = update_options(options,
				    fs->fs_mntops, mntbuf->f_flags);
				if (mountfs(fs->fs_vfstype, fs->fs_spec,
				    fs->fs_file, init_flags, options,
				    fs->fs_mntops))
					rval = 1;
			}
		} else if (fstab_style) {
			for (i = 0; i < mntsize; i++) {
				if (checkvfsname(mntbuf[i].f_fstypename, vfslist))
					continue;
				putfsent(&mntbuf[i]);
			}
		} else {
			for (i = 0; i < mntsize; i++) {
				if (checkvfsname(mntbuf[i].f_fstypename,
				    vfslist))
					continue;
				prmount(&mntbuf[i]);
			}
		}
		exit(rval);
	case 1:
		if (vfslist != NULL)
			usage();

		rmslashes(*argv, *argv);

		if (init_flags & MNT_UPDATE) {
			mntfromname = NULL;
			have_fstab = 0;
			if ((mntbuf = getmntpt(*argv)) == NULL)
				errx(1, "not currently mounted %s", *argv);
			/*
			 * Only get the mntflags from fstab if both mntpoint
			 * and mntspec are identical. Also handle the special
			 * case where just '/' is mounted and 'spec' is not
			 * identical with the one from fstab ('/dev' is missing
			 * in the spec-string at boot-time).
			 */
			if ((fs = getfsfile(mntbuf->f_mntonname)) != NULL) {
				if (strcmp(fs->fs_spec,
				    mntbuf->f_mntfromname) == 0 &&
				    strcmp(fs->fs_file,
				    mntbuf->f_mntonname) == 0) {
					have_fstab = 1;
					mntfromname = mntbuf->f_mntfromname;
				} else if (argv[0][0] == '/' &&
				    argv[0][1] == '\0') {
					fs = getfsfile("/");
					have_fstab = 1;
					mntfromname = fs->fs_spec;
				}
			}
			if (have_fstab) {
				options = update_options(options, fs->fs_mntops,
				    mntbuf->f_flags);
			} else {
				mntfromname = mntbuf->f_mntfromname;
				options = update_options(options, NULL,
				    mntbuf->f_flags);
			}
			rval = mountfs(mntbuf->f_fstypename, mntfromname,
			    mntbuf->f_mntonname, init_flags, options, 0);
			break;
		}
		if ((fs = getfsfile(*argv)) == NULL &&
		    (fs = getfsspec(*argv)) == NULL)
			errx(1, "%s: unknown special file or file system",
			    *argv);
		if (BADTYPE(fs->fs_type))
			errx(1, "%s has unknown file system type",
			    *argv);
		rval = mountfs(fs->fs_vfstype, fs->fs_spec, fs->fs_file,
		    init_flags, options, fs->fs_mntops);
		break;
	case 2:
		/*
		 * If -t flag has not been specified, the path cannot be
		 * found.
		 *
		 * If the spec is not a file and contains a ':' then assume
		 * NFS.
		 *
		 * If the spec is a cdev attempt to extract the fstype from
		 * the label.
		 *
		 * When all else fails ufs is assumed.
		 */
		if (vfslist == NULL) {
			if (strpbrk(argv[0], ":") != NULL &&
			    access(argv[0], 0) == -1) {
				vfstype = "nfs";
			} else {
				checkdisklabel(argv[0], &vfstype);
			}
		}

		rval = mountfs(vfstype, getdevpath(argv[0], 0), argv[1],
			       init_flags, options, NULL);
		break;
	default:
		usage();
		/* NOTREACHED */
	}

	/*
	 * If the mount was successfully, and done by root, tell mountd the
	 * good news.
	 */
	if (rval == 0 && getuid() == 0)
		restart_mountd();

	exit(rval);
}

static int
ismounted(struct fstab *fs, struct statfs *mntbuf, int mntsize)
{
	int i;

	if (fs->fs_file[0] == '/' && fs->fs_file[1] == '\0')
		/* the root file system can always be remounted */
		return (0);

	for (i = mntsize - 1; i >= 0; --i)
		if (strcmp(fs->fs_file, mntbuf[i].f_mntonname) == 0 &&
		    (!isremountable(fs->fs_vfstype) ||
		     strcmp(fs->fs_spec, mntbuf[i].f_mntfromname) == 0))
			return (1);
	return (0);
}

static int
isremountable(const char *vfsname)
{
	const char **cp;

	for (cp = remountable_fs_names; *cp; cp++)
		if (strcmp(*cp, vfsname) == 0)
			return (1);
	return (0);
}

static int
hasopt(const char *mntopts, const char *option)
{
	int negative, found;
	char *opt, *optbuf;

	if (option[0] == 'n' && option[1] == 'o') {
		negative = 1;
		option += 2;
	} else
		negative = 0;
	optbuf = xstrdup(mntopts);
	found = 0;
	for (opt = optbuf; (opt = strtok(opt, ",")) != NULL; opt = NULL) {
		if (opt[0] == 'n' && opt[1] == 'o') {
			if (!strcasecmp(opt + 2, option))
				found = negative;
		} else if (!strcasecmp(opt, option))
			found = !negative;
	}
	free(optbuf);
	return (found);
}

static int
mountfs(const char *vfstype, const char *spec, const char *name, int flags,
        const char *options, const char *mntopts)
{
	/* List of directories containing mount_xxx subcommands. */
	static const char *edirs[] = {
		_PATH_SBIN,
		_PATH_USRSBIN,
		NULL
	};
	const char *argv[100], **edir;
	struct statfs sf;
	pid_t pid;
	int argc, i, status;
	char *optbuf;
	char *argv0;
	char execname[MAXPATHLEN + 1];
	char mntpath[MAXPATHLEN];

#if __GNUC__
	(void)&optbuf;
	(void)&name;
#endif

	/* resolve the mountpoint with realpath(3) */
	checkpath(name, mntpath);
	name = mntpath;

	if (mntopts == NULL)
		mntopts = "";
	if (options == NULL) {
		if (*mntopts == '\0') {
			options = "rw";
		} else {
			options = mntopts;
			mntopts = "";
		}
	}
	optbuf = catopt(xstrdup(mntopts), options);

	if (strcmp(name, "/") == 0)
		flags |= MNT_UPDATE;
	if (flags & MNT_FORCE)
		optbuf = catopt(optbuf, "force");
	if (flags & MNT_RDONLY)
		optbuf = catopt(optbuf, "ro");
	/*
	 * XXX
	 * The mount_mfs (newfs) command uses -o to select the
	 * optimization mode.  We don't pass the default "-o rw"
	 * for that reason.
	 */
	if (flags & MNT_UPDATE)
		optbuf = catopt(optbuf, "update");

	asprintf(&argv0, "mount_%s", vfstype);

	argc = 0;
	argv[argc++] = argv0;
	mangle(optbuf, &argc, argv);
	argv[argc++] = spec;
	argv[argc++] = name;
	argv[argc] = NULL;

	if (debug) {
		printf("exec: mount_%s", vfstype);
		for (i = 1; i < argc; i++)
			printf(" %s", argv[i]);
		printf("\n");
		return (0);
	}

	switch (pid = fork()) {
	case -1:				/* Error. */
		warn("fork");
		free(optbuf);
		return (1);
	case 0:					/* Child. */
		/* Go find an executable. */
		for (edir = edirs; *edir; edir++) {
			snprintf(execname, sizeof(execname),
				 "%s/mount_%s", *edir, vfstype);
			execv(execname, __DECONST(char * const *, argv));
		}
		if (errno == ENOENT) {
			int len = 0;
			char *cp;
			for (edir = edirs; *edir; edir++)
				len += strlen(*edir) + 2;	/* ", " */
			if ((cp = malloc(len)) == NULL)
				errx(1, "malloc failed");
			cp[0] = '\0';
			for (edir = edirs; *edir; edir++) {
				strcat(cp, *edir);
				if (edir[1] != NULL)
					strcat(cp, ", ");
			}
			warn("exec mount_%s not found in %s", vfstype, cp);
		}
		exit(1);
		/* NOTREACHED */
	default:				/* Parent. */
		free(optbuf);

		if (waitpid(pid, &status, 0) < 0) {
			warn("waitpid");
			return (1);
		}

		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) != 0)
				return (WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			warnx("%s: %s", name, sys_siglist[WTERMSIG(status)]);
			return (1);
		}

		if (verbose) {
			if (statfs(name, &sf) < 0) {
				warn("statfs %s", name);
				return (1);
			}
			if (fstab_style)
				putfsent(&sf);
			else
				prmount(&sf);
		}
		break;
	}

	return (0);
}

static void
prmount(struct statfs *sfp)
{
	struct passwd *pw;
	char *buf;
	int error;
	int len;

	error = 0;
	len = 256;

	if ((buf = malloc(len)) == NULL)
		errx(1, "malloc failed");

	printf("%s on %s (%s", sfp->f_mntfromname, sfp->f_mntonname,
	    sfp->f_fstypename);

	/* Get a string buffer with all the used flags names */
	error = mountctl(sfp->f_mntonname, MOUNTCTL_MOUNTFLAGS,
			 -1, NULL, 0, buf, len);

	if (sfp->f_owner) {
		printf(", mounted by ");
		if ((pw = getpwuid(sfp->f_owner)) != NULL)
			printf("%s", pw->pw_name);
		else
			printf("%d", sfp->f_owner);
	}

	if (error != -1 && strlen(buf))
		printf(", %s", buf);

	if (verbose) {
		if (sfp->f_syncwrites != 0 || sfp->f_asyncwrites != 0)
			printf(", writes: sync %ld async %ld",
			    sfp->f_syncwrites, sfp->f_asyncwrites);
		if (sfp->f_syncreads != 0 || sfp->f_asyncreads != 0)
			printf(", reads: sync %ld async %ld",
			    sfp->f_syncreads, sfp->f_asyncreads);
	}
	printf(")\n");
}

static struct statfs *
getmntpt(const char *name)
{
	struct statfs *mntbuf;
	int i, mntsize;

	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	for (i = mntsize - 1; i >= 0; i--) {
		if (strcmp(mntbuf[i].f_mntfromname, name) == 0 ||
		    strcmp(mntbuf[i].f_mntonname, name) == 0)
			return (&mntbuf[i]);
	}
	return (NULL);
}

static char *
catopt(char *s0, const char *s1)
{
	size_t i;
	char *cp;

	if (s1 == NULL || *s1 == '\0')
		return s0;

	if (s0 && *s0) {
		i = strlen(s0) + strlen(s1) + 1 + 1;
		if ((cp = malloc(i)) == NULL)
			errx(1, "malloc failed");
		snprintf(cp, i, "%s,%s", s0, s1);
	} else
		cp = xstrdup(s1);

	if (s0)
		free(s0);
	return (cp);
}

static void
mangle(char *options, int *argcp, const char **argv)
{
	char *p, *s;
	int argc;

	argc = *argcp;
	for (s = options; (p = strsep(&s, ",")) != NULL;)
		if (*p != '\0') {
			if (*p == '-') {
				argv[argc++] = p;
				p = strchr(p, '=');
				if (p) {
					*p = '\0';
					argv[argc++] = p+1;
				}
			} else if (strcmp(p, "rw") != 0) {
				argv[argc++] = "-o";
				argv[argc++] = p;
			}
		}

	*argcp = argc;
}


static char *
update_options(char *opts, char *fstab, int curflags)
{
	char *o, *p;
	char *cur;
	char *expopt, *newopt, *tmpopt;

	if (opts == NULL)
		return xstrdup("");

	/* remove meta options from list */
	remopt(fstab, MOUNT_META_OPTION_FSTAB);
	remopt(fstab, MOUNT_META_OPTION_CURRENT);
	cur = flags2opts(curflags);

	/*
	 * Expand all meta-options passed to us first.
	 */
	expopt = NULL;
	for (p = opts; (o = strsep(&p, ",")) != NULL;) {
		if (strcmp(MOUNT_META_OPTION_FSTAB, o) == 0)
			expopt = catopt(expopt, fstab);
		else if (strcmp(MOUNT_META_OPTION_CURRENT, o) == 0)
			expopt = catopt(expopt, cur);
		else
			expopt = catopt(expopt, o);
	}
	free(cur);
	free(opts);

	/*
	 * Remove previous contradictory arguments. Given option "foo" we
	 * remove all the "nofoo" options. Given "nofoo" we remove "nonofoo"
	 * and "foo" - so we can deal with possible options like "notice".
	 */
	newopt = NULL;
	for (p = expopt; (o = strsep(&p, ",")) != NULL;) {
		if ((tmpopt = malloc( strlen(o) + 2 + 1 )) == NULL)
			errx(1, "malloc failed");
	
		strcpy(tmpopt, "no");
		strcat(tmpopt, o);
		remopt(newopt, tmpopt);
		free(tmpopt);

		if (strncmp("no", o, 2) == 0)
			remopt(newopt, o+2);

		newopt = catopt(newopt, o);
	}
	free(expopt);

	return newopt;
}

static void
remopt(char *string, const char *opt)
{
	char *o, *p, *r;

	if (string == NULL || *string == '\0' || opt == NULL || *opt == '\0')
		return;

	r = string;

	for (p = string; (o = strsep(&p, ",")) != NULL;) {
		if (strcmp(opt, o) != 0) {
			if (*r == ',' && *o != '\0')
				r++;
			while ((*r++ = *o++) != '\0')
			    ;
			*--r = ',';
		}
	}
	*r = '\0';
}

static void
usage(void)
{

	fprintf(stderr, "%s\n%s\n%s\n",
"usage: mount [-adfpruvw] [-F fstab] [-o options] [-t type]",
"       mount [-dfpruvw] {special | node}",
"       mount [-dfpruvw] [-o options] [-t type] special node");
	exit(1);
}

/*
 * Prints the default values for dump frequency and pass number of fsck.
 * This happens when mount(8) is called with -p and there is no fstab file
 * or there is but the values aren't specified.
 */
static void
printdefvals(const struct statfs *ent)
{
        if (strcmp(ent->f_fstypename, "ufs") == 0) {
                if (strcmp(ent->f_mntonname, "/") == 0)
                        printf("\t1 1\n");
                else
                        printf("\t2 2\n");
        } else {
                printf("\t0 0\n");
	}
}

static void
putfsent(const struct statfs *ent)
{
	struct stat sb;
	struct fstab *fst;
	char *opts;
  
	opts = flags2opts(ent->f_flags);
	printf("%s\t%s\t%s %s", ent->f_mntfromname, ent->f_mntonname,
	    ent->f_fstypename, opts);
	free(opts);

	/*
	 * If fstab file is missing don't call getfsspec() or getfsfile()
	 * at all, because the same warning will be printed twice for every
	 * mounted filesystem.
	 */
	if (stat(_PATH_FSTAB, &sb) != -1) {
		if ((fst = getfsspec(ent->f_mntfromname)))
			printf("\t%u %u\n", fst->fs_freq, fst->fs_passno);
		else if ((fst = getfsfile(ent->f_mntonname)))
			printf("\t%u %u\n", fst->fs_freq, fst->fs_passno);
		else
			printdefvals(ent);
	} else {
		printdefvals(ent);
	}
}


static char *
flags2opts(int flags)
{
	char *res;

	res = NULL;

	res = catopt(res, (flags & MNT_RDONLY) ? "ro" : "rw");

	if (flags & MNT_SYNCHRONOUS)	res = catopt(res, "sync");
	if (flags & MNT_NOEXEC)		res = catopt(res, "noexec");
	if (flags & MNT_NOSUID)		res = catopt(res, "nosuid");
	if (flags & MNT_NODEV)		res = catopt(res, "nodev");
	if (flags & MNT_UNION)		res = catopt(res, "union");
	if (flags & MNT_ASYNC)		res = catopt(res, "async");
	if (flags & MNT_NOATIME)	res = catopt(res, "noatime");
	if (flags & MNT_NOCLUSTERR)	res = catopt(res, "noclusterr");
	if (flags & MNT_NOCLUSTERW)	res = catopt(res, "noclusterw");
	if (flags & MNT_NOSYMFOLLOW)	res = catopt(res, "nosymfollow");
	if (flags & MNT_SUIDDIR)	res = catopt(res, "suiddir");
	if (flags & MNT_IGNORE)		res = catopt(res, "ignore");

	return res;
}

static char*
xstrdup(const char *str)
{
	char* ret = strdup(str);
	if(ret == NULL) {
		errx(1, "strdup failed (could not allocate memory)");
	}
	return ret;
}

/*
 * Hack 'cd9660' for the default fstype on cd media if no disklabel
 * found.
 */
static int
iscdmedia(const char *path, int fd __unused)
{
	int n;

	if (strrchr(path, '/'))
		path = strrchr(path, '/') + 1;
	if (sscanf(path, "acd%d", &n) == 1)
		return 1;
	if (sscanf(path, "cd%d", &n) == 1)
		return 1;
	return 0;
}

/*
 * If the device path is a cdev attempt to access the disklabel to determine
 * the filesystem type.  Adjust *vfstypep if we can figure it out
 * definitively.
 *
 * Ignore any portion of the device path after an '@' or ':'.
 */
static void
checkdisklabel(const char *devpath, const char **vfstypep)
{
	struct stat st;
	struct partinfo info;
	char *path = strdup(devpath);
	int fd;

	if (strchr(path, '@'))
		*strchr(path, '@') = 0;
	if (strchr(path, ':'))
		*strchr(path, ':') = 0;

	if (stat(path, &st) < 0)
		goto done;
	if (!S_ISCHR(st.st_mode))
		goto done;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		goto done;
	if (ioctl(fd, DIOCGPART, &info) == 0) {
		if (info.fstype >= 0 && info.fstype < (int)FSMAXTYPES) {
			if (fstype_to_vfsname[info.fstype]) {
				*vfstypep = fstype_to_vfsname[info.fstype];
			} else if (iscdmedia(path, fd)) {
				*vfstypep = "cd9660";
			} else {
				fprintf(stderr,
					"mount: warning: fstype in disklabel "
					"not set to anything I understand\n");
				fprintf(stderr,
					"attempting to mount with -t ufs\n");
			}
		}
	}
	close(fd);
done:
	free(path);
}
