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
 * $DragonFly: src/sbin/hammer/cmd_softprune.c,v 1.1 2008/05/31 18:45:04 dillon Exp $
 */

#include "hammer.h"

struct softprune {
	struct softprune *next;
	struct statfs fs;
	char *filesystem;
	struct hammer_ioc_prune prune;
	int maxelms;
};

static void softprune_usage(int code);
static void hammer_softprune_scandir(struct softprune **basep,
			 struct hammer_ioc_prune *template,
			 const char *dirname);
static void hammer_softprune_addentry(struct softprune **basep,
			 struct hammer_ioc_prune *template,
			 const char *dirpath, char *linkbuf, char *tidptr);
static void hammer_softprune_finalize(struct softprune *scan);

/*
 * softprune <softlink-dir>
 */
void
hammer_cmd_softprune(char **av, int ac)
{
	struct hammer_ioc_prune template;
	struct softprune *base, *scan;
	int fd;
	int rcode;

	base = NULL;
	rcode = 0;

	bzero(&template, sizeof(template));
	template.beg_localization = HAMMER_MIN_LOCALIZATION;
	template.beg_obj_id = HAMMER_MIN_OBJID;
	template.end_localization = HAMMER_MAX_LOCALIZATION;
	template.end_obj_id = HAMMER_MAX_OBJID;
	hammer_get_cycle(&template.end_obj_id, &template.end_localization);
	template.stat_oldest_tid = HAMMER_MAX_TID;

	/*
	 * For now just allow one directory
	 */
	if (ac != 1)
		softprune_usage(1);

	/*
	 * Scan the softlink directories
	 */
	while (ac) {
		hammer_softprune_scandir(&base, &template, *av);
		++av;
		--ac;
	}

	/*
	 * XXX future (need to store separate cycles for each filesystem)
	 */
	if (base == NULL) {
		fprintf(stderr, "No snapshot softlinks found\n");
		exit(1);
	}
	if (base->next) {
		fprintf(stderr, "Currently only one HAMMER filesystem may "
				"be specified in the softlink scan\n");
		exit(1);
	}

	/*
	 * Issue the prunes
	 */
	for (scan = base; scan; scan = scan->next) {
		hammer_softprune_finalize(scan);
		printf("Prune %s: %d snapshots\n",
		       scan->filesystem, scan->prune.nelms);
		if (scan->prune.nelms == 0)
			continue;
		fd = open(scan->filesystem, O_RDONLY);
		if (fd < 0) {
			warn("Unable to open %s", scan->filesystem);
			rcode = 1;
			continue;
		}

		if (ioctl(fd, HAMMERIOC_PRUNE, &scan->prune) < 0) {
			printf("Prune %s failed: %s\n",
			       scan->filesystem, strerror(errno));
			rcode = 2;
		} else if (scan->prune.head.flags & HAMMER_IOC_HEAD_INTR) {
			printf("Prune %s interrupted by timer at "
			       "%016llx %04x\n",
			       scan->filesystem,
			       scan->prune.cur_obj_id,
			       scan->prune.cur_localization);
			if (CyclePath) {
				hammer_set_cycle(scan->prune.cur_obj_id,
						 scan->prune.cur_localization);
			}
			rcode = 0;
		} else {
			if (CyclePath)
				hammer_reset_cycle();
			printf("Prune %s succeeded\n", scan->filesystem);
		}
		printf("Pruned %lld/%lld records (%lld directory entries) "
		       "and %lld bytes\n",
			scan->prune.stat_rawrecords,
			scan->prune.stat_scanrecords,
			scan->prune.stat_dirrecords,
			scan->prune.stat_bytes
		);
		close(fd);
	}
	if (rcode)
		exit(rcode);
}

/*
 * Scan a directory for softlinks representing snapshots and build
 * associated softprune structures.
 */
static void
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
						  dirname, linkbuf, ptr - 1);
		}
	}
	free(linkbuf);
	if (path)
		free(path);
}

/*
 * Add the softlink to the appropriate softprune structure, creating a new
 * if necessary.
 */
static void
hammer_softprune_addentry(struct softprune **basep,
			 struct hammer_ioc_prune *template,
			 const char *dirpath, char *linkbuf, char *tidptr)
{
	struct hammer_ioc_prune_elm *elm;
	struct softprune *scan;
	struct statfs fs;
	char *fspath;

	if (linkbuf[0] == '/') {
		asprintf(&fspath, "%*.*s",
			 (tidptr - linkbuf), (tidptr - linkbuf), linkbuf);
	} else {
		asprintf(&fspath, "%s/%*.*s", dirpath,
			 (tidptr - linkbuf), (tidptr - linkbuf), linkbuf);
	}
	if (statfs(fspath, &fs) < 0) {
		free(fspath);
		return;
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
		scan->next = *basep;
		scan->maxelms = 32;
		scan->prune.elms = malloc(sizeof(*elm) * scan->maxelms);
		*basep = scan;
	} else {
		free(fspath);
	}

	/*
	 * Add the entry (unsorted).  Just set the beg_tid, we will sort
	 * and set the remaining entries later.
	 *
	 * Always leave one entry free for our terminator.
	 */
	if (scan->prune.nelms >= scan->maxelms - 1) {
		scan->maxelms = (scan->maxelms * 3 / 2);
		scan->prune.elms = realloc(scan->prune.elms,
					   sizeof(*elm) * scan->maxelms);
	}
	elm = &scan->prune.elms[scan->prune.nelms];
	elm->beg_tid = strtoull(tidptr + 2, NULL, 0);
	elm->end_tid = 0;
	elm->mod_tid = 0;
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
		elm->mod_tid = elm->end_tid - elm->beg_tid;
	}

	/*
	 * Add a final element to prune everything from transaction id
	 * 0 to the lowest transaction id (aka last so far).
	 */
	assert(scan->prune.nelms < scan->maxelms);
	elm = &scan->prune.elms[scan->prune.nelms++];
	elm->beg_tid = 0;
	elm->end_tid = elm[-1].beg_tid;
	elm->mod_tid = elm->end_tid - elm->beg_tid;
}

static
void
softprune_usage(int code)
{
	fprintf(stderr, "Badly formed command, use:\n");
	fprintf(stderr, "hammer softprune directory-containing-softlinks\n");
	exit(code);
}


