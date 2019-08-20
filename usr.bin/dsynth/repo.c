/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * This code uses concepts and configuration based on 'synth', by
 * John R. Marino <draco@marino.st>, which was written in ada.
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
 */
#include "dsynth.h"

typedef struct pinfo {
	struct pinfo *next;
	char *spath;
	int foundit;
} pinfo_t;

static int pinfocmp(const void *s1, const void *s2);
static void scanit(const char *path, const char *subpath,
			int *countp, pinfo_t ***list_tailp);
pinfo_t *pinfofind(pinfo_t **ary, int count, char *spath);

void
DoRebuildRepo(int ask)
{
	char *buf;

	if (ask) {
		if (askyn("Rebuild the repository? ") == 0)
			return;
	}
	asprintf(&buf, "pkg repo -o %s %s", PackagesPath, RepositoryPath);
	printf("Rebuilding repository\n");
	if (system(buf)) {
		printf("Rebuild failed\n");
	} else {
		printf("Rebuild succeeded\n");
	}
}

void
DoUpgradePkgs(pkg_t *pkgs __unused, int ask __unused)
{
	dfatal("Not Implemented");
}

void
PurgeDistfiles(pkg_t *pkgs)
{
	pinfo_t *list;
	pinfo_t *item;
	pinfo_t **list_tail;
	pinfo_t **ary;
	char *dstr;
	char *buf;
	int count;
	int delcount;
	int i;

	printf("Scanning distfiles... ");
	fflush(stdout);
	count = 0;
	list = NULL;
	list_tail = &list;
	scanit(DistFilesPath, NULL, &count, &list_tail);
	printf("Checking %d distfiles\n", count);
	fflush(stdout);

	ary = calloc(count, sizeof(pinfo_t *));
	for (i = 0; i < count; ++i) {
		ary[i] = list;
		list = list->next;
	}
	ddassert(list == NULL);
	qsort(ary, count, sizeof(pinfo_t *), pinfocmp);

	for (; pkgs; pkgs = pkgs->bnext) {
		if (pkgs->distfiles == NULL || pkgs->distfiles[0] == 0)
			continue;
		ddprintf(0, "distfiles %s\n", pkgs->distfiles);
		dstr = strtok(pkgs->distfiles, " \t");
		while (dstr) {
			for (;;) {
				if (pkgs->distsubdir && pkgs->distsubdir[0]) {
					asprintf(&buf, "%s/%s",
						 pkgs->distsubdir, dstr);
					item = pinfofind(ary, count, buf);
					ddprintf(0, "TEST %s %p\n", buf, item);
					free(buf);
					buf = NULL;
				} else {
					item = pinfofind(ary, count, dstr);
					ddprintf(0, "TEST %s %p\n", dstr, item);
				}
				if (item) {
					item->foundit = 1;
					break;
				}
				if (strrchr(dstr, ':') == NULL)
					break;
				*strrchr(dstr, ':') = 0;
			}
			dstr = strtok(NULL, " \t");
		}
	}

	delcount = 0;
	for (i = 0; i < count; ++i) {
		item = ary[i];
		if (item->foundit == 0) {
			++delcount;
		}
	}
	if (askyn("Delete %d of %d items? ", delcount, count)) {
		printf("Deleting %d/%d obsolete source distfiles\n",
		       delcount, count);
		for (i = 0; i < count; ++i) {
			item = ary[i];
			if (item->foundit == 0) {
				asprintf(&buf, "%s/%s",
					 DistFilesPath, item->spath);
				if (remove(buf) < 0)
					printf("Cannot delete %s\n", buf);
				free(buf);
			}
		}
	}


	free(ary);
}

void
RemovePackages(pkg_t *pkgs __unused)
{
	dfatal("Not Implemented");
}

static int
pinfocmp(const void *s1, const void *s2)
{
	const pinfo_t *item1 = *(const pinfo_t *const*)s1;
	const pinfo_t *item2 = *(const pinfo_t *const*)s2;

	return (strcmp(item1->spath, item2->spath));
}

pinfo_t *
pinfofind(pinfo_t **ary, int count, char *spath)
{
	pinfo_t *item;
	int res;
	int b;
	int e;
	int m;

	b = 0;
	e = count;
	while (b != e) {
		m = b + (e - b) / 2;
		item = ary[m];
		res = strcmp(spath, item->spath);
		if (res == 0)
			return item;
		if (res < 0) {
			e = m;
		} else {
			b = m + 1;
		}
	}
	return NULL;
}

void
scanit(const char *path, const char *subpath,
       int *countp, pinfo_t ***list_tailp)
{
	struct dirent *den;
	pinfo_t *item;
	char *npath;
	char *spath;
	DIR *dir;
	struct stat st;

	if ((dir = opendir(path)) != NULL) {
		while ((den = readdir(dir)) != NULL) {
			if (den->d_namlen == 1 && den->d_name[0] == '.')
				continue;
			if (den->d_namlen == 2 && den->d_name[0] == '.' &&
			    den->d_name[1] == '.')
				continue;
			asprintf(&npath, "%s/%s", path, den->d_name);
			if (lstat(npath, &st) < 0) {
				free(npath);
				continue;
			}
			if (S_ISDIR(st.st_mode)) {
				if (subpath) {
					asprintf(&spath, "%s/%s",
						 subpath, den->d_name);
					scanit(npath, spath,
					       countp, list_tailp);
					free(spath);
				} else {
					scanit(npath, den->d_name,
					       countp, list_tailp);
				}
				free(npath);
			} else if (S_ISREG(st.st_mode)) {
				item = calloc(1, sizeof(*item));
				if (subpath) {
					asprintf(&item->spath, "%s/%s",
						 subpath, den->d_name);
				} else {
					item->spath = strdup(den->d_name);
				}
				**list_tailp = item;
				*list_tailp = &item->next;
				++*countp;
				ddprintf(0, "scan   %s\n", item->spath);
			} else {
				free(npath);
			}
		}
		closedir(dir);
	}
}
