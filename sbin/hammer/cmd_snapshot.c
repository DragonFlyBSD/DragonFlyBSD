/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/hammer/cmd_snapshot.c,v 1.7 2008/07/10 18:47:22 mneumann Exp $
 */

#include "hammer.h"
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define DEFAULT_SNAPSHOT_NAME "snap-%Y%m%d-%H%M"

static void snapshot_usage(int exit_code);
static void snapshot_add(int fd, const char *fsym, const char *tsym,
		const char *label, hammer_tid_t tid);
static void snapshot_ls(const char *path);
static void snapshot_del(int fsfd, hammer_tid_t tid);
static char *dirpart(const char *path);

/*
 * hammer snap <path> [<note>]
 *
 * Path may be a directory, softlink, or non-existent (a softlink will be
 * created).
 */
void
hammer_cmd_snap(char **av, int ac, int tostdout, int fsbase)
{
	struct hammer_ioc_synctid synctid;
	struct hammer_ioc_version version;
	char *dirpath;
	char *fsym;
	char *tsym;
	struct stat st;
	char note[64];
	int fsfd;

	if (ac == 0 || ac > 2) {
		snapshot_usage(1);
		/* not reached */
		exit(1);
	}

	if (ac == 2)
		snprintf(note, sizeof(note), "%s", av[1]);
	else
		note[0] = 0;

	/*
	 * Figure out the softlink path and directory path
	 */
	if (stat(av[0], &st) < 0) {
		dirpath = dirpart(av[0]);
		tsym = av[0];
	} else if (S_ISLNK(st.st_mode)) {
		dirpath = dirpart(av[0]);
		tsym = av[0];
	} else if (S_ISDIR(st.st_mode)) {
		time_t t = time(NULL);
		struct tm *tp;
		char extbuf[64];

		tp = localtime(&t);
		strftime(extbuf, sizeof(extbuf), DEFAULT_SNAPSHOT_NAME, tp);

		dirpath = strdup(av[0]);
		asprintf(&tsym, "%s/%s", dirpath, extbuf);
	} else {
		err(2, "hammer snap: File %s exists and is not a softlink\n",
		    av[0]);
		/* not reached */
	}

	/*
	 * Get a handle on some directory in the filesystem for the
	 * ioctl (so it is stored in the correct PFS).
	 */
	fsfd = open(dirpath, O_RDONLY);
	if (fsfd < 0) {
		err(2, "hammer snap: Cannot open directory %s\n", dirpath);
		/* not reached */
	}

	/*
	 * Must be at least version 3 to use this command.
	 */
        bzero(&version, sizeof(version));

        if (ioctl(fsfd, HAMMERIOC_GET_VERSION, &version) < 0) {
		err(2, "Unable to create snapshot");
		/* not reached */
	} else if (version.cur_version < 3) {
		errx(2, "Unable to create snapshot: This directive requires "
			"you to upgrade\n"
			"the filesystem to version 3.  "
			"Use 'hammer snapshot' for legacy operation.");
		/* not reached */
	}

	/*
	 * Synctid to get a transaction id for the snapshot.
	 */
	bzero(&synctid, sizeof(synctid));
	synctid.op = HAMMER_SYNCTID_SYNC2;
	if (ioctl(fsfd, HAMMERIOC_SYNCTID, &synctid) < 0) {
		err(2, "hammer snap: Synctid %s failed",
		    dirpath);
	}
	if (tostdout) {
		if (strcmp(dirpath, ".") == 0 || strcmp(dirpath, "..") == 0) {
			printf("%s/@@0x%016jx\n",
				dirpath, (uintmax_t)synctid.tid);
		} else {
			printf("%s@@0x%016jx\n",
				dirpath, (uintmax_t)synctid.tid);
		}
		fsym = NULL;
		tsym = NULL;
	}

	/*
	 * Contents of the symlink being created.
	 */
	if (fsbase) {
		struct statfs buf;

		if (statfs(dirpath, &buf) < 0) {
			err(2, "hammer snap: Cannot determine mount for %s",
			    dirpath);
		}
		asprintf(&fsym, "%s/@@0x%016jx",
			 buf.f_mntonname, (uintmax_t)synctid.tid);
	} else if (strcmp(dirpath, ".") == 0 || strcmp(dirpath, "..") == 0) {
		asprintf(&fsym, "%s/@@0x%016jx",
			 dirpath, (uintmax_t)synctid.tid);
	} else {
		asprintf(&fsym, "%s@@0x%016jx",
			 dirpath, (uintmax_t)synctid.tid);
	}

	/*
	 * Create the snapshot.
	 */
	snapshot_add(fsfd, fsym, tsym, note, synctid.tid);
	free(dirpath);
}

/*
 * hammer snapls [<path> ...]
 *
 * If no arguments are specified snapshots for the PFS containing the
 * current directory are listed.
 */
void
hammer_cmd_snapls(char **av, int ac)
{
	int i;

	for (i = 0; i < ac; ++i)
		snapshot_ls(av[i]);
	if (ac == 0)
		snapshot_ls(".");
}

/*
 * hammer snaprm <path> ...
 * hammer snaprm <transid> ...
 * hammer snaprm <filesystem> <transid> ...
 */
void
hammer_cmd_snaprm(char **av, int ac)
{
	struct stat st;
	char linkbuf[1024];
	intmax_t tid;
	int fsfd = -1;
	int i;
	int delete;
	enum snaprm_mode { none_m, path_m, tid_m } mode = none_m;
	char *dirpath;
	char *ptr, *ptr2;

	if (ac == 0) {
		snapshot_usage(1);
		/* not reached */
	}

	for (i = 0; i < ac; ++i) {
		if (lstat(av[i], &st) < 0) {
			tid = strtoull(av[i], &ptr, 16);
			if (*ptr) {
				err(2, "hammer snaprm: not a file or tid: %s",
				    av[i]);
				/* not reached */
			}
			if (mode == path_m) {
				snapshot_usage(1);
				/* not reached */
			}
			mode = tid_m;
			if (fsfd < 0)
				fsfd = open(".", O_RDONLY);
			snapshot_del(fsfd, tid);
		} else if (S_ISDIR(st.st_mode)) {
			if (i != 0 || ac < 2) {
				snapshot_usage(1);
				/* not reached */
			}
			if (fsfd >= 0)
				close(fsfd);
			fsfd = open(av[i], O_RDONLY);
			if (fsfd < 0) {
				err(2, "hammer snaprm: cannot open dir %s",
				    av[i]);
				/* not reached */
			}
			mode = tid_m;
		} else if (S_ISLNK(st.st_mode)) {
			dirpath = dirpart(av[i]);
			bzero(linkbuf, sizeof(linkbuf));
			if (readlink(av[i], linkbuf, sizeof(linkbuf) - 1) < 0) {
				err(2, "hammer snaprm: cannot read softlink: "
				       "%s", av[i]);
				/* not reached */
			}
			if (linkbuf[0] == '/') {
				free(dirpath);
				dirpath = dirpart(linkbuf);
			} else {
				asprintf(&ptr, "%s/%s", dirpath, linkbuf);
				free(dirpath);
				dirpath = dirpart(ptr);
			}

			if (fsfd >= 0)
				close(fsfd);
			fsfd = open(dirpath, O_RDONLY);
			if (fsfd < 0) {
				err(2, "hammer snaprm: cannot open dir %s",
				    dirpath);
				/* not reached */
			}

			delete = 1;
			if (i == 0 && ac > 1) {
				mode = path_m;
				if (lstat(av[1], &st) < 0) {
					tid = strtoull(av[1], &ptr, 16);
					if (*ptr == '\0') {
						delete = 0;
						mode = tid_m;
					}
				}
			} else {
				if (mode == tid_m) {
					snapshot_usage(1);
					/* not reached */
				}
				mode = path_m;
			}
			if (delete && (ptr = strrchr(linkbuf, '@')) &&
			    ptr > linkbuf && ptr[-1] == '@' && ptr[1]) {
				tid = strtoull(ptr + 1, &ptr2, 16);
				if (*ptr2 == '\0') {
					snapshot_del(fsfd, tid);
					remove(av[i]);
				}
			}
			free(dirpath);
		} else {
			err(2, "hammer snaprm: not directory or snapshot "
			       "softlink: %s", av[i]);
			/* not reached */
		}
	}
	if (fsfd >= 0)
		close(fsfd);
}

/*
 * snapshot <softlink-dir>
 * snapshot <filesystem> <softlink-dir> [<note>]
 */
void
hammer_cmd_snapshot(char **av, int ac)
{
	const char *filesystem;
	const char *softlink_dir;
	char *softlink_fmt;
	struct statfs buf;
	struct stat st;
	struct hammer_ioc_synctid synctid;
	char *from;
	char *to;
	char *note = NULL;

	if (ac == 1) {
		filesystem = NULL;
		softlink_dir = av[0];
	} else if (ac == 2) {
		filesystem = av[0];
		softlink_dir = av[1];
	} else if (ac == 3) {
		filesystem = av[0];
		softlink_dir = av[1];
		note = av[2];
	} else {
		snapshot_usage(1);
		/* not reached */
		softlink_dir = NULL;
		filesystem = NULL;
	}

	if (stat(softlink_dir, &st) == 0) {
		if (!S_ISDIR(st.st_mode))
			err(2, "File %s already exists", softlink_dir);

		if (filesystem == NULL) {
			if (statfs(softlink_dir, &buf) != 0) {
				err(2, "Unable to determine filesystem of %s",
				    softlink_dir);
			}
			filesystem = buf.f_mntonname;
		}

		softlink_fmt = malloc(strlen(softlink_dir) + 1 + 1 +
		                      sizeof(DEFAULT_SNAPSHOT_NAME));
		if (softlink_fmt == NULL)
			err(2, "Failed to allocate string");

		strcpy(softlink_fmt, softlink_dir);
		if (softlink_fmt[strlen(softlink_fmt)-1] != '/')
			strcat(softlink_fmt, "/");
		strcat(softlink_fmt, DEFAULT_SNAPSHOT_NAME);
	} else {
		softlink_fmt = strdup(softlink_dir);

		if (filesystem == NULL) {
			/*
			 * strip-off last '/path' segment to get the softlink
			 * directory, which we need to determine the filesystem
			 * we are on.
			 */
			char *pos = strrchr(softlink_fmt, '/');
			if (pos != NULL)
				*pos = '\0';

			if (stat(softlink_fmt, &st) != 0 ||
			    !S_ISDIR(st.st_mode)) {
				err(2, "Unable to determine softlink dir %s",
				    softlink_fmt);
			}
			if (statfs(softlink_fmt, &buf) != 0) {
				err(2, "Unable to determine filesystem of %s",
				    softlink_fmt);
			}
			filesystem = buf.f_mntonname;

			/* restore '/' */
			if (pos != NULL)
				*pos = '/';
		}
	}

	/*
	 * Synctid
	 */
	bzero(&synctid, sizeof(synctid));
	synctid.op = HAMMER_SYNCTID_SYNC2;

	int fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(2, "Unable to open %s", filesystem);
	if (ioctl(fd, HAMMERIOC_SYNCTID, &synctid) < 0)
		err(2, "Synctid %s failed", filesystem);

	asprintf(&from, "%s/@@0x%016jx", filesystem, (uintmax_t)synctid.tid);
	if (from == NULL)
		err(2, "Couldn't generate string");

	int sz = strlen(softlink_fmt) + 50;
	to = malloc(sz);
	if (to == NULL)
		err(2, "Failed to allocate string");

	time_t t = time(NULL);
	if (strftime(to, sz, softlink_fmt, localtime(&t)) == 0)
		err(2, "String buffer too small");

	asprintf(&from, "%s/@@0x%016jx", filesystem, (uintmax_t)synctid.tid);

	snapshot_add(fd, from, to, note, synctid.tid);

	close(fd);
	printf("%s\n", to);

	free(softlink_fmt);
	free(from);
	free(to);
}

static
void
snapshot_add(int fd, const char *fsym, const char *tsym, const char *label,
	     hammer_tid_t tid)
{
	struct hammer_ioc_version version;
	struct hammer_ioc_snapshot snapshot;

        bzero(&version, sizeof(version));
        bzero(&snapshot, sizeof(snapshot));

	/*
	 * For HAMMER filesystem v3+ the snapshot is recorded in meta-data.
	 */
        if (ioctl(fd, HAMMERIOC_GET_VERSION, &version) == 0 &&
	    version.cur_version >= 3) {
		snapshot.index = 0;
		snapshot.count = 1;
		snapshot.snaps[0].tid = tid;
		snapshot.snaps[0].ts = time(NULL) * 1000000ULL;
		if (label) {
			snprintf(snapshot.snaps[0].label,
				 sizeof(snapshot.snaps[0].label),
				 "%s",
				 label);
		}
		if (ioctl(fd, HAMMERIOC_ADD_SNAPSHOT, &snapshot) < 0) {
			err(2, "Unable to create snapshot");
		} else if (snapshot.head.error &&
			   snapshot.head.error != EEXIST) {
			errx(2, "Unable to create snapshot: %s\n",
				strerror(snapshot.head.error));
		}
        }

	/*
	 * Create a symlink for the snapshot.  If a file exists with the same
	 * name the new symlink will replace it.
	 */
	if (fsym && tsym) {
		remove(tsym);
		if (symlink(fsym, tsym) < 0) {
			err(2, "Unable to create symlink %s", tsym);
		}
	}
}

static
void
snapshot_ls(const char *path)
{
	/*struct hammer_ioc_version version;*/
	struct hammer_ioc_info info;
	struct hammer_ioc_snapshot snapshot;
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pseudofs_data pfs_od;
	struct hammer_snapshot_data *snap;
	struct tm *tp;
	time_t t;
	u_int32_t i;
	int fd;
	char snapts[64];
	char *mntpoint;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		err(2, "hammer snapls: cannot open %s", path);
		/* not reached */
	}

	bzero(&pfs, sizeof(pfs));
	bzero(&pfs_od, sizeof(pfs_od));
	pfs.pfs_id = -1;
	pfs.ondisk = &pfs_od;
	pfs.bytes = sizeof(struct hammer_pseudofs_data);
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) < 0) {
		err(2, "hammer snapls: cannot retrieve PFS info on %s", path);
		/* not reached */
	}

	bzero(&info, sizeof(info));
	if ((ioctl(fd, HAMMERIOC_GET_INFO, &info)) < 0) {
                err(2, "hammer snapls: cannot retrieve HAMMER info");
		/* not reached */
        }

	mntpoint = libhammer_find_pfs_mount(&pfs.ondisk->unique_uuid);

	printf("Snapshots on %s\tPFS #%d\n",
	    mntpoint ? mntpoint : path, pfs.pfs_id);
	printf("Transaction ID\t\tTimestamp\t\tNote\n");

	if (mntpoint)
		free(mntpoint);

	bzero(&snapshot, sizeof(snapshot));
	do {
		if (ioctl(fd, HAMMERIOC_GET_SNAPSHOT, &snapshot) < 0) {
			err(2, "hammer snapls: %s: not HAMMER fs or "
				"version < 3", path);
			/* not reached */
		}
		for (i = 0; i < snapshot.count; ++i) {
			snap = &snapshot.snaps[i];

			t = snap->ts / 1000000ULL;
			tp = localtime(&t);
			strftime(snapts, sizeof(snapts),
				 "%Y-%m-%d %H:%M:%S %Z", tp);
			printf("0x%016jx\t%s\t%s\n",
				(uintmax_t)snap->tid, snapts,
				strlen(snap->label) ? snap->label : "-");
		}
	} while (snapshot.head.error == 0 && snapshot.count);
}

static
void
snapshot_del(int fsfd, hammer_tid_t tid)
{
	struct hammer_ioc_snapshot snapshot;
	struct hammer_ioc_version version;

        bzero(&version, sizeof(version));

        if (ioctl(fsfd, HAMMERIOC_GET_VERSION, &version) < 0) {
		err(2, "hammer snaprm 0x%016jx", (uintmax_t)tid);
	}
	if (version.cur_version < 3) {
		errx(2, "hammer snaprm 0x%016jx: You must upgrade to version "
			" 3 to use this directive", (uintmax_t)tid);
	}

	bzero(&snapshot, sizeof(snapshot));
	snapshot.count = 1;
	snapshot.snaps[0].tid = tid;

	/*
	 * Do not abort if we are unable to remove the meta-data.
	 */
	if (ioctl(fsfd, HAMMERIOC_DEL_SNAPSHOT, &snapshot) < 0) {
		err(2, "hammer snaprm 0x%016jx",
		      (uintmax_t)tid);
	} else if (snapshot.head.error == ENOENT) {
		fprintf(stderr, "Warning: hammer snaprm 0x%016jx: "
				"meta-data not found\n",
			(uintmax_t)tid);
	} else if (snapshot.head.error) {
		fprintf(stderr, "Warning: hammer snaprm 0x%016jx: %s\n",
			(uintmax_t)tid, strerror(snapshot.head.error));
	}
}

static
void
snapshot_usage(int exit_code)
{
	fprintf(stderr,
    "hammer snap <path> [<note>]\t\tcreate snapshot & link, points to\n"
				"\t\t\t\t\tbase of PFS mount\n"
    "hammer snaplo <path> [<note>]\t\tcreate snapshot & link, points to\n"
				"\t\t\t\t\ttarget dir\n"
    "hammer snapq <dir> [<note>]\t\tcreate snapshot, output path to stdout\n"
    "hammer snaprm <path> ...\t\tdelete snapshots; filesystem is CWD\n"
    "hammer snaprm <transid> ...\t\tdelete snapshots\n"
    "hammer snaprm <filesystem> <transid> ...\tdelete snapshots\n"
    "hammer snapls [<path> ...]\t\tlist available snapshots\n"
    "\n"
    "NOTE: Snapshots are created in filesystem meta-data, any directory\n"
    "      in a HAMMER filesystem or PFS may be specified.  If the path\n"
    "      specified does not exist this function will also create a\n"
    "      softlink.\n"
    "\n"
    "      When deleting snapshots transaction ids may be directly specified\n"
    "      or file paths to snapshot softlinks may be specified.  If a\n"
    "      softlink is specified the softlink will also be deleted.\n"
    "\n"
    "NOTE: The old 'hammer snapshot [<filesystem>] <snapshot-dir>' form\n"
    "      is still accepted but is a deprecated form.  This form will\n"
    "      work for older hammer versions.  The new forms only work for\n"
    "      HAMMER version 3 or later filesystems.  HAMMER can be upgraded\n"
    "      to version 3 in-place.\n"
	);
	exit(exit_code);
}

static
char *
dirpart(const char *path)
{
	const char *ptr;
	char *res;

	ptr = strrchr(path, '/');
	if (ptr) {
		while (ptr > path && ptr[-1] == '/')
			--ptr;
		if (ptr == path)
			ptr = NULL;
	}
	if (ptr == NULL) {
		path = ".";
		ptr = path + 1;
	}
	res = malloc(ptr - path + 1);
	bcopy(path, res, ptr - path);
	res[ptr - path] = 0;
	return(res);
}
