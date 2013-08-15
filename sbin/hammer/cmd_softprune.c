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
 * $DragonFly: src/sbin/hammer/cmd_softprune.c,v 1.7 2008/08/21 23:28:43 thomas Exp $
 */

#include "hammer.h"

static void softprune_usage(int code);
static int hammer_softprune_scanmeta(int fd, struct softprune *scan,
			int delete_all);
static void hammer_meta_flushdelete(int fd, struct hammer_ioc_snapshot *dsnap);
static struct softprune *hammer_softprune_addentry(struct softprune **basep,
			struct hammer_ioc_prune *template,
			const char *dirpath, const char *denname,
			struct stat *st,
			const char *linkbuf, const char *tidptr);
static void hammer_softprune_addelm(struct softprune *scan, hammer_tid_t tid,
			time_t ct, time_t mt);
static void hammer_softprune_finalize(struct softprune *scan);

/*
 * prune <softlink-dir>
 * prune-everything <filesystem>
 */
void
hammer_cmd_softprune(char **av, int ac, int everything_opt)
{
	struct hammer_ioc_prune template;
	struct hammer_ioc_pseudofs_rw pfs;
	struct softprune *base, *scan;
	int fd;
	int rcode;

	base = NULL;
	rcode = 0;
	if (TimeoutOpt > 0)
		alarm(TimeoutOpt);

	bzero(&pfs, sizeof(pfs));
	pfs.bytes = sizeof(*pfs.ondisk);
	pfs.ondisk = malloc(pfs.bytes);
	bzero(pfs.ondisk, pfs.bytes);
	pfs.pfs_id = -1;

	/*
	 * NOTE: To restrict to a single file XXX we have to set
	 * the localization the same (not yet implemented).  Typically
	 * two passes would be needed, one using HAMMER_LOCALIZE_MISC
	 * and one using HAMMER_LOCALIZE_INODE.
	 */

	bzero(&template, sizeof(template));
	template.key_beg.localization = HAMMER_MIN_LOCALIZATION;
	template.key_beg.obj_id = HAMMER_MIN_OBJID;
	template.key_end.localization = HAMMER_MAX_LOCALIZATION;
	template.key_end.obj_id = HAMMER_MAX_OBJID;
	hammer_get_cycle(&template.key_end, NULL);
	template.stat_oldest_tid = HAMMER_MAX_TID;

	/*
	 * For now just allow one directory
	 */
	if (ac == 0 || ac > 1)
		softprune_usage(1);

	/*
	 * Scan the softlink directory.
	 */
	if (everything_opt) {
		const char *dummylink = "";
		scan = hammer_softprune_addentry(&base, &template,
						 *av, NULL, NULL,
						 dummylink, dummylink);
		if (scan == NULL)
			softprune_usage(1);
		scan->prune.nelms = 0;
		scan->prune.head.flags |= HAMMER_IOC_PRUNE_ALL;
	} else {
		hammer_softprune_scandir(&base, &template, *av);
		if (base == NULL) {
			const char *dummylink = "";
			scan = hammer_softprune_addentry(&base, &template,
							 *av, NULL, NULL,
							 dummylink, dummylink);
			if (scan == NULL)
				softprune_usage(1);
			scan->prune.nelms = 0;
		}
		++av;
		--ac;
	}

	/*
	 * XXX future (need to store separate cycles for each filesystem)
	 */
	if (base->next) {
		fprintf(stderr, "Currently only one HAMMER filesystem may "
				"be specified in the softlink scan\n");
		exit(1);
	}

	/*
	 * Issue the prunes
	 */
	for (scan = base; scan; scan = scan->next) {
		/*
		 * Open the filesystem for ioctl calls and extract the
		 * PFS.
		 */
		fd = open(scan->filesystem, O_RDONLY);
		if (fd < 0) {
			warn("Unable to open %s", scan->filesystem);
			rcode = 1;
			continue;
		}

		if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) < 0) {
			warn("Filesystem %s is not HAMMER", scan->filesystem);
			rcode = 1;
			close(fd);
			continue;
		}
		scan->prune_min = pfs.ondisk->prune_min;

		/*
		 * Incorporate meta-data snapshots into the pruning regimen.
		 * If pruning everything we delete the meta-data snapshots.
		 */
		if (hammer_softprune_scanmeta(fd, scan, everything_opt) < 0) {
			warn("Filesystem %s could not scan meta-data snaps",
			     scan->filesystem);
			rcode = 1;
			close(fd);
			continue;
		}

		/*
		 * Finalize operations
		 */
		hammer_softprune_finalize(scan);
		if (everything_opt) {
			printf("Prune %s: EVERYTHING\n",
			       scan->filesystem);
		} else {
			printf("Prune %s: %d snapshots\n",
			       scan->filesystem, scan->prune.nelms);
		}
		if (scan->prune.nelms == 0 &&
		    (scan->prune.head.flags & HAMMER_IOC_PRUNE_ALL) == 0) {
			fprintf(stderr, "No snapshots found\n");
			continue;
		}

		printf("Prune %s: objspace %016jx:%04x %016jx:%04x "
		       "pfs_id %d\n",
		       scan->filesystem,
		       (uintmax_t)scan->prune.key_beg.obj_id,
		       scan->prune.key_beg.localization,
		       (uintmax_t)scan->prune.key_end.obj_id,
		       scan->prune.key_end.localization,
		       pfs.pfs_id);
		printf("Prune %s: prune_min is %dd/%02d:%02d:%02d\n",
		       scan->filesystem,
			pfs.ondisk->prune_min / (24 * 60 * 60),
			pfs.ondisk->prune_min / 60 / 60 % 24,
			pfs.ondisk->prune_min / 60 % 60,
			pfs.ondisk->prune_min % 60);

		RunningIoctl = 1;
		if (ioctl(fd, HAMMERIOC_PRUNE, &scan->prune) < 0) {
			printf("Prune %s failed: %s\n",
			       scan->filesystem, strerror(errno));
			rcode = 2;
		} else if (scan->prune.head.flags & HAMMER_IOC_HEAD_INTR) {
			printf("Prune %s interrupted by timer at "
			       "%016jx %04x\n",
			       scan->filesystem,
			       (uintmax_t)scan->prune.key_cur.obj_id,
			       scan->prune.key_cur.localization);
			if (CyclePath)
				hammer_set_cycle(&scan->prune.key_cur, 0);
			rcode = 0;
		} else {
			if (CyclePath)
				hammer_reset_cycle();
			printf("Prune %s succeeded\n", scan->filesystem);
		}
		printf("Pruned %jd/%jd records (%jd directory entries) "
		       "and %jd bytes\n",
			(uintmax_t)scan->prune.stat_rawrecords,
			(uintmax_t)scan->prune.stat_scanrecords,
			(uintmax_t)scan->prune.stat_dirrecords,
			(uintmax_t)scan->prune.stat_bytes
		);
		RunningIoctl = 0;
		close(fd);
	}
	if (rcode)
		exit(rcode);
}

/*
 * Scan a directory for softlinks representing snapshots and build
 * associated softprune structures.
 *
 * NOTE: Once a filesystem is completely converted to the meta-data
 *	 snapshot mechanic we don't have to scan softlinks any more
 *	 and can just use the meta-data.  But for now we do both.
 */
void
hammer_softprune_scandir(struct softprune **basep,
			 struct hammer_ioc_prune *template,
			 const char *dirname)
{
	struct stat st;
	struct dirent *den;
	DIR *dir;
	char *path;
	int len;
	char *linkbuf;
	char *ptr;

	path = NULL;
	linkbuf = malloc(MAXPATHLEN);

	if ((dir = opendir(dirname)) == NULL)
		err(1, "Cannot open directory %s", dirname);
	while ((den = readdir(dir)) != NULL) {
		if (strcmp(den->d_name, ".") == 0)
			continue;
		if (strcmp(den->d_name, "..") == 0)
			continue;
		if (path)
			free(path);
		asprintf(&path, "%s/%s", dirname, den->d_name);
		if (lstat(path, &st) < 0)
			continue;
		if (!S_ISLNK(st.st_mode))
			continue;
		if ((len = readlink(path, linkbuf, MAXPATHLEN - 1)) < 0)
			continue;
		linkbuf[len] = 0;
		if ((ptr = strrchr(linkbuf, '@')) &&
		    ptr > linkbuf && ptr[-1] == '@') {
			hammer_softprune_addentry(basep, template,
						  dirname, den->d_name, &st,
						  linkbuf, ptr - 1);
		}
	}
	free(linkbuf);
	if (path)
		free(path);
}

/*
 * Scan the metadata snapshots for the filesystem and either delete them
 * or add them to the pruning list.
 */
static
int
hammer_softprune_scanmeta(int fd, struct softprune *scan, int delete_all)
{
	struct hammer_ioc_version	version;
	struct hammer_ioc_snapshot	snapshot;
	struct hammer_ioc_snapshot	dsnapshot;
	struct hammer_snapshot_data	*snap;
	time_t ct;

	/*
	 * Stop if we can't get the version.  Meta-data snapshots only
	 * exist for HAMMER version 3 or greater.
	 */
	bzero(&version, sizeof(version));
	if (ioctl(fd, HAMMERIOC_GET_VERSION, &version) < 0)
		return(-1);
	if (version.cur_version < 3)
		return(0);

	bzero(&snapshot, sizeof(snapshot));
	bzero(&dsnapshot, sizeof(dsnapshot));

	/*
	 * Scan meta-data snapshots, either add them to the prune list or
	 * delete them.  When deleting, just skip any entries which cannot
	 * be deleted.
	 */
	for (;;) {
		if (ioctl(fd, HAMMERIOC_GET_SNAPSHOT, &snapshot) < 0) {
			printf("hammer prune: Unable to access "
			       "meta-data snaps: %s\n", strerror(errno));
			return(-1);
		}
		while (snapshot.index < snapshot.count) {
			snap = &snapshot.snaps[snapshot.index];
			if (delete_all) {
				dsnapshot.snaps[dsnapshot.count++] = *snap;
				if (dsnapshot.count == HAMMER_SNAPS_PER_IOCTL)
					hammer_meta_flushdelete(fd, &dsnapshot);
			} else {
				ct = snap->ts / 1000000ULL;
				hammer_softprune_addelm(scan, snap->tid,
							ct, ct);
			}
			++snapshot.index;
		}
		if (snapshot.head.flags & HAMMER_IOC_SNAPSHOT_EOF)
			break;
		snapshot.index = 0;
	}
	if (delete_all)
		hammer_meta_flushdelete(fd, &dsnapshot);
	return(0);
}

/*
 * Flush any entries built up in the deletion snapshot ioctl structure.
 * Used during a prune-everything.
 */
static void
hammer_meta_flushdelete(int fd, struct hammer_ioc_snapshot *dsnap)
{
	while (dsnap->index < dsnap->count) {
		if (ioctl(fd, HAMMERIOC_DEL_SNAPSHOT, dsnap) < 0)
			break;
		if (dsnap->head.error == 0)
			break;
		++dsnap->index;
	}
	dsnap->index = 0;
	dsnap->count = 0;
}

/*
 * Add the softlink to the appropriate softprune structure, creating a new
 * one if necessary.
 */
static
struct softprune *
hammer_softprune_addentry(struct softprune **basep,
			 struct hammer_ioc_prune *template,
			 const char *dirpath, const char *denname __unused,
			 struct stat *st,
			 const char *linkbuf, const char *tidptr)
{
	struct softprune *scan;
	struct statfs fs;
	char *fspath;

	/*
	 * Calculate filesystem path.
	 */
	if (linkbuf[0] == '/') {
		asprintf(&fspath, "%*.*s",
			 (int)(tidptr - linkbuf), (int)(tidptr - linkbuf),
			 linkbuf);
	} else {
		asprintf(&fspath, "%s/%*.*s", dirpath,
			 (int)(tidptr - linkbuf), (int)(tidptr - linkbuf),
			 linkbuf);
	}
	if (statfs(fspath, &fs) < 0) {
		free(fspath);
		return(NULL);
	}

	/*
	 * Locate the filesystem in an existing softprune structure
	 */
	for (scan = *basep; scan; scan = scan->next) {
		if (bcmp(&fs.f_fsid, &scan->fs.f_fsid, sizeof(fs.f_fsid)) != 0)
			continue;
		if (strcmp(fs.f_mntonname, scan->fs.f_mntonname) != 0)
			continue;
		break;
	}

	/*
	 * Create a new softprune structure if necessasry
	 */
	if (scan == NULL) {
		scan = malloc(sizeof(*scan));
		bzero(scan, sizeof(*scan));

		scan->fs = fs;
		scan->filesystem = fspath;
		scan->prune = *template;
		scan->maxelms = 32;
		scan->prune.elms = malloc(sizeof(struct hammer_ioc_prune_elm) *
					  scan->maxelms);
		scan->next = *basep;
		*basep = scan;
	} else {
		free(fspath);
	}
	hammer_softprune_addelm(scan,
				(hammer_tid_t)strtoull(tidptr + 2, NULL, 0),
				(st ? st->st_ctime : 0),
				(st ? st->st_mtime : 0));
	return(scan);
}

/*
 * Add the entry (unsorted).  Just set the beg_tid, we will sort
 * and set the remaining entries later.
 *
 * Always leave one entry free for our terminator.
 */
static void
hammer_softprune_addelm(struct softprune *scan, hammer_tid_t tid,
			time_t ct, time_t mt)
{
	struct hammer_ioc_prune_elm *elm;

	if (scan->prune.nelms >= scan->maxelms - 1) {
		scan->maxelms = (scan->maxelms * 3 / 2);
		scan->prune.elms = realloc(scan->prune.elms,
					   sizeof(*elm) * scan->maxelms);
	}

	/*
	 * NOTE: Temporarily store the snapshot timestamp in mod_tid.
	 *	 This will be cleaned up in the finalization phase.
	 */
	elm = &scan->prune.elms[scan->prune.nelms];
	elm->beg_tid = tid;
	elm->end_tid = 0;
	elm->mod_tid = 0;
	if (ct < mt)
		elm->mod_tid = ct;
	else
		elm->mod_tid = mt;
	++scan->prune.nelms;
}

/*
 * Finalize a softprune structure after scanning in its softlinks.
 * Sort the elements, remove duplicates, and then fill in end_tid and
 * mod_tid.
 *
 * The array must end up in descending order.
 */
static int
hammer_softprune_qsort_cmp(const void *arg1, const void *arg2)
{
	const struct hammer_ioc_prune_elm *elm1 = arg1;
	const struct hammer_ioc_prune_elm *elm2 = arg2;

	if (elm1->beg_tid < elm2->beg_tid)
		return(1);
	if (elm1->beg_tid > elm2->beg_tid)
		return(-1);
	return(0);
}

static void
hammer_softprune_finalize(struct softprune *scan)
{
	struct hammer_ioc_prune_elm *elm;
	time_t t;
	long delta;
	int i;

	/*
	 * Don't do anything if there are no elements.
	 */
	if (scan->prune.nelms == 0)
		return;

	/*
	 * Sort the elements in descending order, remove duplicates, and
	 * fill in any missing bits.
	 */
	qsort(scan->prune.elms, scan->prune.nelms, sizeof(*elm),
	      hammer_softprune_qsort_cmp);

	for (i = 0; i < scan->prune.nelms; ++i) {
		elm = &scan->prune.elms[i];
		if (i == 0) {
			/*
			 * First (highest TID) (also last if only one element)
			 */
			elm->end_tid = HAMMER_MAX_TID;
		} else if (elm[0].beg_tid == elm[-1].beg_tid) {
			/*
			 * Remove duplicate
			 */
			--scan->prune.nelms;
			if (i != scan->prune.nelms) {
				bcopy(elm + 1, elm,
				      (scan->prune.nelms - i) * sizeof(*elm));
			}
			--i;
			continue;
		} else {
			/*
			 * Middle or last.
			 */
			elm->end_tid = elm[-1].beg_tid;
		}
	}

	/*
	 * If a minimum retention time (in seconds) is configured for the
	 * PFS, remove any snapshots from the pruning list that are within
	 * the period.
	 */
	if (scan->prune_min) {
		t = time(NULL);
		for (i = scan->prune.nelms - 1; i >= 0; --i) {
			elm = &scan->prune.elms[i];
			if (elm->mod_tid == 0)
				continue;
			delta = (long)(t - (time_t)elm->mod_tid);
			if (delta < scan->prune_min)
				break;
		}
		++i;
		if (i) {
			printf("Prune %s: prune_min: Will not clean between "
			       "the teeth of the first %d snapshots\n",
			       scan->filesystem, i);
			bcopy(&scan->prune.elms[i], &scan->prune.elms[0],
			      (scan->prune.nelms - i) * sizeof(scan->prune.elms[0]));
			scan->prune.elms[0].end_tid = HAMMER_MAX_TID;
			scan->prune.nelms -= i;
		}
	}

	/*
	 * Remove the first entry.  This entry represents the prune from
	 * the most recent snapshot to current.  We wish to retain the
	 * fine-grained history for this region.
	 */
	if (scan->prune.nelms) {
		bcopy(&scan->prune.elms[1], &scan->prune.elms[0],
		      (scan->prune.nelms - 1) * sizeof(scan->prune.elms[0]));
		--scan->prune.nelms;
	}

	/*
	 * Add a final element to prune everything from transaction id
	 * 0 to the lowest transaction id (aka last so far).
	 */
	if (scan->prune.nelms) {
		assert(scan->prune.nelms < scan->maxelms);
		elm = &scan->prune.elms[scan->prune.nelms];
		elm->beg_tid = 1;
		elm->end_tid = elm[-1].beg_tid;
		++scan->prune.nelms;
	}

	/*
	 * Adjust mod_tid to what the ioctl() expects.
	 */
	for (i = 0; i < scan->prune.nelms; ++i) {
		elm = &scan->prune.elms[i];
		elm->mod_tid = elm->end_tid - elm->beg_tid;
		printf("TID %016jx - %016jx\n",
		       (uintmax_t)elm->beg_tid, (uintmax_t)elm->end_tid);
	}
}

static
void
softprune_usage(int code)
{
	fprintf(stderr, "Badly formed prune command, use:\n");
	fprintf(stderr, "hammer prune <softlink-dir>\n");
	fprintf(stderr, "hammer prune-everything <filesystem>\n");
	exit(code);
}


