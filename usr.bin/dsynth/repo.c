/*
 * Copyright (c) 2019-2020 The DragonFly Project.  All rights reserved.
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
#include <openssl/md5.h>

typedef struct pinfo {
	struct pinfo *next;
	char *spath;
	int foundit;
	int inlocks;
} pinfo_t;

static void removePackagesMetaRecurse(pkg_t *pkg);
static int pinfocmp(const void *s1, const void *s2);
static void scanit(const char *path, const char *subpath,
			int *countp, pinfo_t ***list_tailp,
			int inlocks);
pinfo_t *pinfofind(pinfo_t **ary, int count, char *spath);
static void childRebuildRepo(bulk_t *bulk);
static void scandeletenew(const char *path);

static void rebuildTerminateSignal(int signo);
static char *md5lkfile(char *rpath, int which);
static int lkdircount(char *buf);

static char *RebuildRemovePath;

void
DoRebuildRepo(int ask)
{
	bulk_t *bulk;
	FILE *fp;
	int fd;
	char tpath[256];
	const char *sufx;

	if (ask) {
		if (askyn("Rebuild the repository? ") == 0)
			return;
	}

	/*
	 * Scan the repository for temporary .new files and delete them.
	 */
	scandeletenew(RepositoryPath);

	/*
	 * Generate temporary file
	 */
	snprintf(tpath, sizeof(tpath), "/tmp/meta.XXXXXXXX.conf");

	signal(SIGTERM, rebuildTerminateSignal);
	signal(SIGINT, rebuildTerminateSignal);
	signal(SIGHUP, rebuildTerminateSignal);

	RebuildRemovePath = tpath;

	sufx = UsePkgSufx;
	fd = mkostemps(tpath, 5, 0);
	if (fd < 0)
		dfatal_errno("Cannot create %s", tpath);
	fp = fdopen(fd, "w");
	fprintf(fp, "version = %d;\n", MetaVersion);
	fprintf(fp, "packing_format = \"%s\";\n", sufx + 1);
	fclose(fp);

	/*
	 * Run the operation under our bulk infrastructure to
	 * get the correct environment.
	 */
	initbulk(childRebuildRepo, 1);
	queuebulk(tpath, NULL, NULL, NULL);
	bulk = getbulk();

	if (bulk->r1)
		printf("Rebuild succeeded\n");
	else
		printf("Rebuild failed\n");
	donebulk();

	remove(tpath);
}

static void
repackage(const char *basepath, const char *basefile,
	  const char *decomp_suffix, const char *comp_suffix,
	  const char *decomp, const char *comp);

static void
childRebuildRepo(bulk_t *bulk)
{
	FILE *fp;
	char *ptr;
	size_t len;
	pid_t pid;
	const char *cav[MAXCAC];
	char *pkg_path;
	int cac;
	int repackage_mode = 0;

	/*
	 * We have to use the pkg-static that we built as part of the
	 * build process to rebuild the repo because the system pkg might
	 * not be compatible with the repo format changes made in 1.17.
	 */
	asprintf(&pkg_path, "%s/Template/usr/local/sbin/pkg-static", BuildBase);

	cac = 0;
	cav[cac++] = pkg_path;
	cav[cac++] = "repo";
	cav[cac++] = "-m";
	cav[cac++] = bulk->s1;
	cav[cac++] = "-o";
	cav[cac++] = PackagesPath;

	/*
	 * The yaml needs to generate paths relative to PackagePath
	 */
	if (strncmp(PackagesPath, RepositoryPath, strlen(PackagesPath)) == 0)
		cav[cac++] = PackagesPath;
	else
		cav[cac++] = RepositoryPath;

	printf("pkg repo -m %s -o %s %s\n", bulk->s1, cav[cac-2], cav[cac-1]);

	fp = dexec_open(NULL, cav, cac, &pid, NULL, 1, 0);
	while ((ptr = fgetln(fp, &len)) != NULL)
		fwrite(ptr, 1, len, stdout);
	if (dexec_close(fp, pid) == 0)
		bulk->r1 = strdup("");

	/*
	 * Check package version.  Pkg version 1.12 and later generates
	 * the proper repo compression format.  Prior to that version
	 * the repo directive always generated .txz files.
	 */
	cac = 0;
	cav[cac++] = pkg_path;
	cav[cac++] = "-v";
	fp = dexec_open(NULL, cav, cac, &pid, NULL, 1, 0);
	if ((ptr = fgetln(fp, &len)) != NULL && len > 0) {
		int v1;
		int v2;

		ptr[len-1] = 0;
		if (sscanf(ptr, "%d.%d", &v1, &v2) == 2) {
			printf("pkg repo - pkg version: %d.%d\n", v1, v2);
			if (v1 > 1 || (v1 == 1 && v2 >= 12))
				repackage_mode = 1;
		}
	}
	dexec_close(fp, pid);

	/*
	 * Repackage the .txz files created by pkg repo if necessary
	 */
	if (repackage_mode == 0 && strcmp(UsePkgSufx, ".txz") != 0) {
		const char *comp;
		const char *decomp;

		printf("pkg repo - recompressing digests and packagesite\n");

		if (strcmp(UsePkgSufx, ".tar") == 0) {
			decomp = "unxz";
			comp = "cat";
		} else if (strcmp(UsePkgSufx, ".tgz") == 0) {
			decomp = "unxz";
			comp = "gzip";
		} else if (strcmp(UsePkgSufx, ".tbz") == 0) {
			decomp = "unxz";
			comp = "bzip";
		} else if (strcmp(UsePkgSufx, ".tzst") == 0) {
			decomp = "unxz";
			comp = "zstd";
		} else {
			dfatal("recompressing as %s not supported",
			       UsePkgSufx);
			decomp = "unxz";
			comp = "cat";
		}
		repackage(PackagesPath, "digests",
			  ".txz", UsePkgSufx,
			  decomp, comp);
		repackage(PackagesPath, "packagesite",
			  ".txz", UsePkgSufx,
			  decomp, comp);
	} else if (repackage_mode == 1 && strcmp(UsePkgSufx, ".txz") != 0) {
		const char *comp;
		const char *decomp;

		printf("pkg repo - recompressing meta\n");

		if (strcmp(UsePkgSufx, ".tar") == 0) {
			decomp = "cat";
			comp = "xz";
		} else if (strcmp(UsePkgSufx, ".tgz") == 0) {
			decomp = "gunzip";
			comp = "xz";
		} else if (strcmp(UsePkgSufx, ".tbz") == 0) {
			decomp = "bunzip2";
			comp = "xz";
		} else if (strcmp(UsePkgSufx, ".tzst") == 0) {
			decomp = "unzstd";
			comp = "xz";
		} else {
			dfatal("recompressing from %s not supported",
			       UsePkgSufx);
			decomp = "cat";
			comp = "cat";
		}
		repackage(PackagesPath, "meta",
			  UsePkgSufx, ".txz",
			  decomp, comp);
	}
	free (pkg_path);
}

static
void
repackage(const char *basepath, const char *basefile,
	  const char *decomp_suffix, const char *comp_suffix,
	  const char *decomp, const char *comp)
{
	char *buf;

	asprintf(&buf, "%s < %s/%s%s | %s > %s/%s%s",
		decomp, basepath, basefile, decomp_suffix,
		comp, basepath, basefile, comp_suffix);
	if (system(buf) != 0) {
		dfatal("command failed: %s", buf);
	}
	free(buf);
}

void
DoUpgradePkgs(pkg_t *pkgs __unused, int ask __unused)
{
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
	scanit(DistFilesPath, NULL, &count, &list_tail, 0);
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
				/*
				 * Look for distfile
				 */
				if (pkgs->distsubdir) {
					asprintf(&buf, "%s/%s",
						 pkgs->distsubdir, dstr);
				} else {
					buf = dstr;
				}
				item = pinfofind(ary, count, buf);
				if (item)
					item->foundit = 1;
				if (item && item->inlocks == 0) {
					/*
					 * Look for the lock file
					 */
					int scount;

					scount = lkdircount(buf);

					for (i = 0; i <= scount; ++i) {
						item = pinfofind(ary, count,
							     md5lkfile(buf, i));
						if (item)
							item->foundit = 1;
					}
				}

				/*
				 * Cleanup and iterate
				 */
				if (buf != dstr) {
					free(buf);
					buf = NULL;
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
	if (delcount == 0) {
		printf("No obsolete source files out of %d found\n", count);
	} else if (askyn("Delete %d of %d items? ", delcount, count)) {
		printf("Deleting %d/%d obsolete source distfiles\n",
		       delcount, count);
		for (i = 0; i < count; ++i) {
			item = ary[i];
			if (item->foundit == 0) {
				asprintf(&buf, "%s/%s",
					 DistFilesPath, item->spath);
				if (remove(buf) < 0)
					printf("Cannot delete %s\n", buf);
				else
					printf("Deleted %s\n", item->spath);
				free(buf);
			}
		}
	}


	free(ary);
}

void
RemovePackages(pkg_t *list)
{
	pkg_t *scan;
	char *path;

	for (scan = list; scan; scan = scan->bnext) {
		if ((scan->flags & PKGF_MANUALSEL) == 0)
			continue;
		if (scan->pkgfile) {
			scan->flags &= ~PKGF_PACKAGED;
			scan->pkgfile_size = 0;
			asprintf(&path, "%s/%s", RepositoryPath, scan->pkgfile);
			if (remove(path) == 0)
				printf("Removed: %s\n", path);
			free(path);
		}
		if (scan->pkgfile == NULL ||
		    (scan->flags & (PKGF_DUMMY | PKGF_META))) {
			removePackagesMetaRecurse(scan);
		}
	}
}

static void
removePackagesMetaRecurse(pkg_t *pkg)
{
	pkglink_t *link;
	pkg_t *scan;
	char *path;

	PKGLIST_FOREACH(link, &pkg->idepon_list) {
		scan = link->pkg;
		if (scan == NULL)
			continue;
		if (scan->pkgfile == NULL ||
		    (scan->flags & (PKGF_DUMMY | PKGF_META))) {
			removePackagesMetaRecurse(scan);
			continue;
		}
		scan->flags &= ~PKGF_PACKAGED;
		scan->pkgfile_size = 0;

		asprintf(&path, "%s/%s", RepositoryPath, scan->pkgfile);
		if (remove(path) == 0)
			printf("Removed: %s\n", path);
		free(path);
	}
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
       int *countp, pinfo_t ***list_tailp,
       int inlocks)
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
				int sublocks;

				sublocks =
				    (strcmp(den->d_name, ".locks") == 0);

				if (subpath) {
					asprintf(&spath, "%s/%s",
						 subpath, den->d_name);
					scanit(npath, spath, countp,
					       list_tailp, sublocks);
					free(spath);
				} else {
					scanit(npath, den->d_name, countp,
					       list_tailp, sublocks);
				}
			} else if (S_ISREG(st.st_mode)) {
				item = calloc(1, sizeof(*item));
				if (subpath) {
					asprintf(&item->spath, "%s/%s",
						 subpath, den->d_name);
				} else {
					item->spath = strdup(den->d_name);
				}
				item->inlocks = inlocks;

				**list_tailp = item;
				*list_tailp = &item->next;
				++*countp;
				ddprintf(0, "scan   %s\n", item->spath);
			}
			free(npath);
		}
		closedir(dir);
	}
}

/*
 * This removes any .new files left over in the repo.  These can wind
 * being left around when dsynth is killed.
 */
static void
scandeletenew(const char *path)
{
	struct dirent *den;
	const char *ptr;
	DIR *dir;
	char *buf;

	if ((dir = opendir(path)) == NULL)
		dfatal_errno("Cannot scan directory %s", path);
	while ((den = readdir(dir)) != NULL) {
		if ((ptr = strrchr(den->d_name, '.')) != NULL &&
		    strcmp(ptr, ".new") == 0) {
			asprintf(&buf, "%s/%s", path, den->d_name);
			if (remove(buf) < 0)
				dfatal_errno("remove: Garbage %s\n", buf);
			printf("Deleted Garbage %s\n", buf);
			free(buf);
		}
	}
	closedir(dir);
}

static void
rebuildTerminateSignal(int signo __unused)
{
	if (RebuildRemovePath)
		remove(RebuildRemovePath);
	exit(1);

}

/*
 * There will be a .locks sub-directory in /usr/distfiles and also
 * in each sub-directory underneath it containing the MD5 sums for
 * the files in that subdirectory.
 *
 * This is a bit of a mess.  Sometimes the .locks/ for a subdirectory
 * are in parentdir/.locks and not parentdir/subdir/.locks.  The invocation
 * of do-fetch can be a bit messy so we look for a .locks subdir everywhere.
 *
 * The /usr/dports/Mk/Scripts/do-fetch.sh script uses 'echo blah | md5',
 * so we have to add a newline to the buffer being md5'd.
 *
 * The pass-in rpath is relative to the distfiles base.
 */
static char *
md5lkfile(char *rpath, int which_slash)
{
	static char mstr[128];
	static char lkfile[128];
	uint8_t digest[MD5_DIGEST_LENGTH];
	int bplen;
	int i;

	bplen = 0;
	for (i = 0; i < which_slash; ++i) {
		while (rpath[bplen] && rpath[bplen] != '/')
			++bplen;
		if (rpath[bplen])
			++bplen;
	}
	snprintf(mstr, sizeof(mstr), "%s\n", rpath + bplen);
	MD5(mstr, strlen(mstr), digest);

	snprintf(lkfile, sizeof(lkfile),
		"%*.*s.locks/"
		 "%02x%02x%02x%02x%02x%02x%02x%02x"
		 "%02x%02x%02x%02x%02x%02x%02x%02x"
		 ".lk",
		 bplen, bplen, rpath,
		 digest[0], digest[1], digest[2], digest[3],
		 digest[4], digest[5], digest[6], digest[7],
		 digest[8], digest[9], digest[10], digest[11],
		 digest[12], digest[13], digest[14], digest[15]);

	return lkfile;
}

static int
lkdircount(char *buf)
{
	int i;
	int n;

	n = 0;
	for (i = 0; buf[i]; ++i) {
		if (buf[i] == '/')
			++n;
	}
	return n;
}
