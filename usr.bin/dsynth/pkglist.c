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

#define PKG_HSIZE	32768
#define PKG_HMASK	32767

static int parsepkglist_file(const char *path, int debugstop);
static void childGetPackageInfo(bulk_t *bulk);
static void childGetBinaryDistInfo(bulk_t *bulk);
static void childOptimizeEnv(bulk_t *bulk);
static pkg_t *resolveDeps(pkg_t *dep_list, pkg_t ***list_tailp, int gentopo);
static void resolveDepString(pkg_t *pkg, char *depstr,
			int gentopo, int dep_type);
static pkg_t *processPackageListBulk(int total);
static int scan_and_queue_dir(const char *path, const char *level1, int level);
static int scan_binary_repo(const char *path);
#if 0
static void pkgfree(pkg_t *pkg);
#endif

pkg_t *PkgHash1[PKG_HSIZE];	/* by portdir */
pkg_t *PkgHash2[PKG_HSIZE];	/* by pkgfile */

/*
 * Allocate a new pkg structure plus basic initialization.
 */
static __inline pkg_t *
allocpkg(void)
{
	pkg_t *pkg;

	pkg = calloc(1, sizeof(*pkg));
	pkg->idepon_list.next = &pkg->idepon_list;
	pkg->idepon_list.prev = &pkg->idepon_list;
	pkg->deponi_list.next = &pkg->deponi_list;
	pkg->deponi_list.prev = &pkg->deponi_list;

	return pkg;
}

/*
 * Simple hash for lookups
 */
static __inline int
pkghash(const char *str)
{
	int hv = 0xABC32923;
	while (*str) {
		hv = (hv << 5) ^ *str;
		++str;
	}
	hv = hv ^ (hv / PKG_HSIZE) ^ (hv / PKG_HSIZE / PKG_HSIZE);
	return (hv & PKG_HMASK);
}

static void
pkg_enter(pkg_t *pkg)
{
	pkg_t **pkgp;
	pkg_t *scan;

	if (pkg->portdir) {
		pkgp = &PkgHash1[pkghash(pkg->portdir)];
		while ((scan = *pkgp) != NULL) {
			if (strcmp(pkg->portdir, scan->portdir) == 0)
				break;
			pkgp = &scan->hnext1;
		}
		if (scan && (scan->flags & PKGF_PLACEHOLD)) {
			*pkgp = pkg;
			pkg->hnext1 = scan->hnext1;
			free(scan->portdir);
			free(scan);
			scan = NULL;
		}
		if (scan == NULL)
			*pkgp = pkg;
	}

	if (pkg->pkgfile) {
		pkgp = &PkgHash2[pkghash(pkg->pkgfile)];
		while ((scan = *pkgp) != NULL) {
			if (strcmp(pkg->pkgfile, scan->pkgfile) == 0)
				break;
			pkgp = &scan->hnext2;
		}
		if (scan == NULL)
			*pkgp = pkg;
	}
}

static pkg_t *
pkg_find(const char *match)
{
	pkg_t **pkgp;
	pkg_t *pkg;

	pkgp = &PkgHash1[pkghash(match)];
	for (pkg = *pkgp; pkg; pkg = pkg->hnext1) {
		if (strcmp(pkg->portdir, match) == 0)
			return pkg;
	}
	pkgp = &PkgHash2[pkghash(match)];
	for (pkg = *pkgp; pkg; pkg = pkg->hnext2) {
		if (strcmp(pkg->pkgfile, match) == 0)
			return pkg;
	}
	return NULL;
}

/*
 * Parse a specific list of ports via origin name (portdir/subdir)
 */
pkg_t *
ParsePackageList(int n, char **ary, int debugstop)
{
	pkg_t *list;
	int i;
	int total;

	total = 0;
	initbulk(childGetPackageInfo, MaxBulk);

	/*
	 * Always include ports-mgmt/pkg.  s4 is "x" meaning not a manual
	 * selection, "d" meaning DEBUGSTOP mode, or NULL.
	 */
	queuebulk("ports-mgmt", "pkg", NULL, "x");

	for (i = 0; i < n; ++i) {
		char *l1;
		char *l2;
		char *l3;
		struct stat st;

		l1 = strdup(ary[i]);
		if (stat(l1, &st) == 0 && S_ISREG(st.st_mode)) {
			total += parsepkglist_file(l1, debugstop);
			continue;
		}

		l2 = strchr(l1, '/');
		if (l2 == NULL) {
			printf("Bad portdir specification: %s\n", l1);
			free(l1);
			continue;
		}
		*l2++ = 0;
		l3 = strchr(l2, '@');
		if (l3)
			*l3++ = 0;
		queuebulk(l1, l2, l3, (debugstop ? "d" : NULL));
		++total;
		free(l1);
	}
	printf("Processing %d ports\n", total);

	list = processPackageListBulk(total);

	return list;
}

static
int
parsepkglist_file(const char *path, int debugstop)
{
	FILE *fp;
	char *base;
	char *l1;
	char *l2;
	char *l3;
	size_t len;
	int total;

	if ((fp = fopen(path, "r")) == NULL) {
		dpanic_errno("Cannot read %s\n", path);
		/* NOT REACHED */
		return 0;
	}

	total = 0;

	while ((base = fgetln(fp, &len)) != NULL) {
		if (len == 0 || base[len-1] != '\n')
			continue;
		base[--len] = 0;
		l1 = strtok(base, " \t\r\n");
		if (l1 == NULL) {
			printf("Badly formatted pkg info line: %s\n", base);
			continue;
		}
		l2 = strchr(l1, '/');
		if (l2 == NULL) {
			printf("Badly formatted specification: %s\n", l1);
			continue;
		}
		*l2++ = 0;
		l3 = strchr(l2, '@');
		if (l3)
			*l3++ = 0;
		queuebulk(l1, l2, l3, (debugstop ? "d" : NULL));
		++total;
	}
	fclose(fp);

	return total;
}

/*
 * Parse packages from the list installed on the system.
 */
pkg_t *
GetLocalPackageList(void)
{
	pkg_t *list;
	FILE *fp;
	char *base;
	char *l1;
	char *l2;
	char *l3;
	int total;
	size_t len;

	initbulk(childGetPackageInfo, MaxBulk);
	total = 0;

	fp = popen("pkg info -a -o", "r");

	/*
	 * Always include ports-mgmt/pkg.  s4 is "x" meaning not a manual
	 * selection, "d" meaning DEBUGSTOP mode, or NULL.
	 */
	queuebulk("ports-mgmt", "pkg", NULL, "x");

	while ((base = fgetln(fp, &len)) != NULL) {
		if (len == 0 || base[len-1] != '\n')
			continue;
		base[--len] = 0;
		if (strtok(base, " \t") == NULL) {
			printf("Badly formatted pkg info line: %s\n", base);
			continue;
		}
		l1 = strtok(NULL, " \t");
		if (l1 == NULL) {
			printf("Badly formatted pkg info line: %s\n", base);
			continue;
		}

		l2 = strchr(l1, '/');
		if (l2 == NULL) {
			printf("Badly formatted specification: %s\n", l1);
			continue;
		}
		*l2++ = 0;
		l3 = strchr(l2, '@');
		if (l3)
			*l3++ = 0;
		queuebulk(l1, l2, l3, NULL);
		++total;
	}
	pclose(fp);

	printf("Processing %d ports\n", total);

	list = processPackageListBulk(total);

	return list;
}

pkg_t *
GetFullPackageList(void)
{
	int total;

	initbulk(childGetPackageInfo, MaxBulk);

	total = scan_and_queue_dir(DPortsPath, NULL, 1);
	printf("Scanning %d ports\n", total);

	return processPackageListBulk(total);
}

/*
 * Caller has queued the process list for bulk operation.  We retrieve
 * the results and clean up the bulk operation (we may have to do a second
 * bulk operation so we have to be the ones to clean it up).
 */
static pkg_t *
processPackageListBulk(int total)
{
	bulk_t *bulk;
	pkg_t *scan;
	pkg_t *list;
	pkg_t *dep_list;
	pkg_t **list_tail;
	int count;

	list = NULL;
	list_tail = &list;
	count = 0;

	while ((bulk = getbulk()) != NULL) {
		++count;
		if ((count & 255) == 0) {
			printf("%6.2f%%\r",
				(double)count * 100.0 / (double)total + 0.001);
			fflush(stdout);
		}
		if (bulk->list) {
			*list_tail = bulk->list;
			bulk->list = NULL;
			while ((scan = *list_tail) != NULL) {
				if (bulk->s4 == NULL || bulk->s4[0] != 'x')
					scan->flags |= PKGF_MANUALSEL;
				pkg_enter(scan);
				list_tail = &scan->bnext;
			}
		}
		freebulk(bulk);
	}
	printf("100.00%%\n");
	printf("\nTotal %d\n", count);
	fflush(stdout);

	/*
	 * Resolve all dependencies for the related packages, potentially
	 * adding anything that could not be found to the list.  This will
	 * continue to issue bulk operations and process the result until
	 * no dependencies are left.
	 */
	printf("Resolving dependencies...");
	fflush(stdout);
	dep_list = list;
	while (dep_list) {
		dep_list = resolveDeps(dep_list, &list_tail, 0);
	}
	printf("done\n");

	donebulk();

	/*
	 * Generate the topology
	 */
	resolveDeps(list, NULL, 1);

	/*
	 * Do a final count, ignore place holders.
	 */
	count = 0;
	for (scan = list; scan; scan = scan->bnext) {
		if ((scan->flags & PKGF_ERROR) == 0) {
			++count;
		}
	}
	printf("Total Returned %d\n", count);

	/*
	 * Scan our binary distributions and related dependencies looking
	 * for any packages that have already been built.
	 */
	initbulk(childGetBinaryDistInfo, MaxBulk);
	total = scan_binary_repo(RepositoryPath);
	count = 0;
	printf("Scanning %d packages\n", total);

	while ((bulk = getbulk()) != NULL) {
		++count;
		if ((count & 255) == 0) {
			printf("%6.2f%%\r",
				(double)count * 100.0 / (double)total + 0.001);
			fflush(stdout);
		}
		freebulk(bulk);
	}
	printf("100.00%%\n");
	printf("\nTotal %d\n", count);
	fflush(stdout);
	donebulk();

	printf("all done\n");

	return list;
}

pkg_t *
GetPkgPkg(pkg_t *list)
{
	bulk_t *bulk;
	pkg_t *scan;

	for (scan = list; scan; scan = scan->bnext) {
		if (strcmp(scan->portdir, "ports-mgmt/pkg") == 0)
			return scan;
	}

	/*
	 * This will force pkg to be built, but generally this code
	 * is not reached because the package list processing code
	 * adds ports-mgmt/pkg unconditionally.
	 */
	initbulk(childGetPackageInfo, MaxBulk);
	queuebulk("ports-mgmt", "pkg", NULL, "x");
	bulk = getbulk();
	dassert(bulk, "Cannot find ports-mgmt/pkg");
	scan = bulk->list;
	bulk->list = NULL;
	freebulk(bulk);
	donebulk();

	return scan;
}

/*
 * Try to optimize the environment by supplying information that
 * the ports system would generally have to run stuff to get on
 * every package.
 *
 * See childOptimizeEnv() for the actual handling.  We execute
 * a single make -V... -V... for ports-mgmt/pkg from within the
 * bulk system (which handles the environment and disables
 * /etc/make.conf), and we then call addbuildenv() as appropriate.
 *
 * _PERL5_FROM_BIN
 * add others...
 */
void
OptimizeEnv(void)
{
	bulk_t *bulk;

	initbulk(childOptimizeEnv, MaxBulk);
	queuebulk("ports-mgmt", "pkg", NULL, NULL);
	bulk = getbulk();
	freebulk(bulk);
	donebulk();
}

/*
 * Run through the list resolving dependencies and constructing the topology
 * linkages.   This may append packages to the list.
 */
static pkg_t *
resolveDeps(pkg_t *list, pkg_t ***list_tailp, int gentopo)
{
	pkg_t *scan;
	pkg_t *ret_list = NULL;
	bulk_t *bulk;

	for (scan = list; scan; scan = scan->bnext) {
		resolveDepString(scan, scan->fetch_deps,
				 gentopo, DEP_TYPE_FETCH);
		resolveDepString(scan, scan->ext_deps,
				 gentopo, DEP_TYPE_EXT);
		resolveDepString(scan, scan->patch_deps,
				 gentopo, DEP_TYPE_PATCH);
		resolveDepString(scan, scan->build_deps,
				 gentopo, DEP_TYPE_BUILD);
		resolveDepString(scan, scan->lib_deps,
				 gentopo, DEP_TYPE_LIB);
		resolveDepString(scan, scan->run_deps,
				 gentopo, DEP_TYPE_RUN);
	}

	/*
	 * No bulk ops are queued when doing the final topology
	 * generation.
	 */
	if (gentopo)
		return NULL;
	while ((bulk = getbulk()) != NULL) {
		if (bulk->list) {
			if (ret_list == NULL)
				ret_list = bulk->list;
			**list_tailp = bulk->list;
			bulk->list = NULL;
			while (**list_tailp) {
				pkg_enter(**list_tailp);
				*list_tailp = &(**list_tailp)->bnext;
			}
		}
		freebulk(bulk);
	}
	return (ret_list);
}

static void
resolveDepString(pkg_t *pkg, char *depstr, int gentopo, int dep_type)
{
	char *copy_base;
	char *copy;
	char *dep;
	char *sep;
	char *tag;
	char *flavor;
	pkg_t *dpkg;

	if (depstr == NULL || depstr[0] == 0)
		return;

	copy_base = strdup(depstr);
	copy = copy_base;

	for (;;) {
		do {
			dep = strsep(&copy, " \t");
		} while (dep && *dep == 0);
		if (dep == NULL)
			break;

		/*
		 * Ignore dependencies prefixed with ${NONEXISTENT}
		 */
		if (strncmp(dep, "/nonexistent:", 13) == 0)
			continue;

		dep = strchr(dep, ':');
		if (dep == NULL || *dep != ':') {
			printf("Error parsing dependency for %s: %s\n",
			       pkg->portdir, copy_base);
			continue;
		}
		++dep;

		/*
		 * Strip-off any DPortsPath prefix.  EXTRACT_DEPENDS
		 * often (always?) generates this prefix.
		 */
		if (strncmp(dep, DPortsPath, strlen(DPortsPath)) == 0) {
			dep += strlen(DPortsPath);
			if (*dep == '/')
				++dep;
		}

		/*
		 * Strip-off any tag (such as :patch).  We don't try to
		 * organize dependencies at this fine a grain (for now).
		 */
		tag = strchr(dep, ':');
		if (tag)
			*tag++ = 0;

		/*
		 * Locate the dependency
		 */
		if ((dpkg = pkg_find(dep)) != NULL) {
			if (gentopo) {
				pkglink_t *link;

				/*
				 * NOTE: idep_count is calculated recursively
				 *	 at build-time
				 */
				ddprintf(0, "Add Dependency %s -> %s\n",
					pkg->portdir, dpkg->portdir);
				link = calloc(1, sizeof(*link));
				link->pkg = dpkg;
				link->next = &pkg->idepon_list;
				link->prev = pkg->idepon_list.prev;
				link->next->prev = link;
				link->prev->next = link;
				link->dep_type = dep_type;

				link = calloc(1, sizeof(*link));
				link->pkg = pkg;
				link->next = &dpkg->deponi_list;
				link->prev = dpkg->deponi_list.prev;
				link->next->prev = link;
				link->prev->next = link;
				link->dep_type = dep_type;
				++dpkg->depi_count;
			}
			continue;
		}

		/*
		 * This shouldn't happen because we already took a first
		 * pass and should have generated the pkgs.
		 */
		if (gentopo) {
			printf("Topology Generate failed for %s: %s\n",
				pkg->portdir, copy_base);
			continue;
		}

		/*
		 * Separate out the two dports directory components and
		 * extract the optional '@flavor' specification.
		 */
		sep = strchr(dep, '/');
		if (sep == NULL) {
			printf("Error parsing dependency for %s: %s\n",
			       pkg->portdir, copy_base);
			continue;
		}
		*sep++ = 0;

		/*
		 * The flavor hangs off the separator, not the tag
		 */
		flavor = strrchr(sep, '@');
#if 0
		if (tag)
			flavor = strrchr(tag, '@');
		else
			flavor = strrchr(sep, '@');
#endif

		if (flavor)
			*flavor++ = 0;

		if (flavor)
			ddprintf(0, "QUEUE DEPENDENCY FROM PKG %s: %s/%s@%s\n",
			       pkg->portdir, dep, sep, flavor);
		else
			ddprintf(0, "QUEUE DEPENDENCY FROM PKG %s: %s/%s\n",
			       pkg->portdir, dep, sep);

		/*
		 * Use a place-holder to prevent duplicate dependencies from
		 * being processed.  The placeholder will be replaced by
		 * the actual dependency.
		 */
		dpkg = allocpkg();
		if (flavor)
			asprintf(&dpkg->portdir, "%s/%s@%s", dep, sep, flavor);
		else
			asprintf(&dpkg->portdir, "%s/%s", dep, sep);
		dpkg->flags = PKGF_PLACEHOLD;
		pkg_enter(dpkg);

		queuebulk(dep, sep, flavor, NULL);
	}
	free(copy_base);
}

void
FreePackageList(pkg_t *pkgs __unused)
{
	dfatal("not implemented");
}

/*
 * Scan some or all dports to allocate the related pkg structure.  Dependencies
 * are stored but not processed.
 *
 * Threaded function
 */
static void
childGetPackageInfo(bulk_t *bulk)
{
	pkg_t *pkg;
	pkg_t *dummy_node;
	pkg_t **list_tail;
	char *flavors_save;
	char *flavors;
	char *flavor;
	char *ptr;
	FILE *fp;
	int line;
	size_t len;
	char *portpath;
	char *flavarg;
	const char *cav[MAXCAC];
	pid_t pid;
	int cac;

	/*
	 * If the package has flavors we will loop on each one.  If a flavor
	 * is not passed in s3 we will loop on all flavors, otherwise we will
	 * only process the passed-in flavor.
	 */
	flavor = bulk->s3;	/* usually NULL */
	flavors = NULL;
	flavors_save = NULL;
	dummy_node = NULL;

	bulk->list = NULL;
	list_tail = &bulk->list;
again:
	asprintf(&portpath, "%s/%s/%s", DPortsPath, bulk->s1, bulk->s2);
	if (flavor)
		asprintf(&flavarg, "FLAVOR=%s", flavor);
	else
		flavarg = NULL;

	cac = 0;
	cav[cac++] = MAKE_BINARY;
	cav[cac++] = "-C";
	cav[cac++] = portpath;
	if (flavarg)
		cav[cac++] = flavarg;
	cav[cac++] = "-VPKGVERSION";
	cav[cac++] = "-VPKGFILE:T";
	cav[cac++] = "-VDISTFILES";
	cav[cac++] = "-VDIST_SUBDIR";
	cav[cac++] = "-VMAKE_JOBS_NUMBER";
	cav[cac++] = "-VIGNORE";
	cav[cac++] = "-VFETCH_DEPENDS";
	cav[cac++] = "-VEXTRACT_DEPENDS";
	cav[cac++] = "-VPATCH_DEPENDS";
	cav[cac++] = "-VBUILD_DEPENDS";
	cav[cac++] = "-VLIB_DEPENDS";
	cav[cac++] = "-VRUN_DEPENDS";
	cav[cac++] = "-VSELECTED_OPTIONS";
	cav[cac++] = "-VDESELECTED_OPTIONS";
	cav[cac++] = "-VUSE_LINUX";
	cav[cac++] = "-VFLAVORS";
	cav[cac++] = "-VUSES";

	fp = dexec_open(cav, cac, &pid, NULL, 1, 1);
	free(portpath);
	freestrp(&flavarg);

	pkg = allocpkg();
	if (flavor)
		asprintf(&pkg->portdir, "%s/%s@%s", bulk->s1, bulk->s2, flavor);
	else
		asprintf(&pkg->portdir, "%s/%s", bulk->s1, bulk->s2);

	line = 1;
	while ((ptr = fgetln(fp, &len)) != NULL) {
		if (len == 0 || ptr[len-1] != '\n') {
			dfatal("Bad package info for %s/%s response line %d",
			       bulk->s1, bulk->s2, line);
		}
		ptr[--len] = 0;

		switch(line) {
		case 1:		/* PKGVERSION */
			asprintf(&pkg->version, "%s", ptr);
			break;
		case 2:		/* PKGFILE */
			asprintf(&pkg->pkgfile, "%s", ptr);
			break;
		case 3:		/* DISTFILES */
			asprintf(&pkg->distfiles, "%s", ptr);
			break;
		case 4:		/* DIST_SUBDIR */
			pkg->distsubdir = strdup_or_null(ptr);
			break;
		case 5:		/* MAKE_JOBS_NUMBER */
			pkg->make_jobs_number = strtol(ptr, NULL, 0);
			break;
		case 6:		/* IGNORE */
			pkg->ignore = strdup_or_null(ptr);
			break;
		case 7:		/* FETCH_DEPENDS */
			pkg->fetch_deps = strdup_or_null(ptr);
			break;
		case 8:		/* EXTRACT_DEPENDS */
			pkg->ext_deps = strdup_or_null(ptr);
			break;
		case 9:		/* PATCH_DEPENDS */
			pkg->patch_deps = strdup_or_null(ptr);
			break;
		case 10:	/* BUILD_DEPENDS */
			pkg->build_deps = strdup_or_null(ptr);
			break;
		case 11:	/* LIB_DEPENDS */
			pkg->lib_deps = strdup_or_null(ptr);
			break;
		case 12:	/* RUN_DEPENDS */
			pkg->run_deps = strdup_or_null(ptr);
			break;
		case 13:	/* SELECTED_OPTIONS */
			pkg->pos_options = strdup_or_null(ptr);
			break;
		case 14:	/* DESELECTED_OPTIONS */
			pkg->neg_options = strdup_or_null(ptr);
			break;
		case 15:	/* USE_LINUX */
			if (ptr[0])
				pkg->use_linux = 1;
			break;
		case 16:	/* FLAVORS */
			asprintf(&pkg->flavors, "%s", ptr);
			break;
		case 17:	/* USES */
			asprintf(&pkg->uses, "%s", ptr);
			if (strstr(pkg->uses, "metaport"))
				pkg->flags |= PKGF_META;
			break;
		default:
			printf("EXTRA LINE: %s\n", ptr);
			break;
		}
		++line;
	}
	if (line == 1) {
		printf("DPort not found: %s/%s\n", bulk->s1, bulk->s2);
		pkg->flags |= PKGF_NOTFOUND;
	} else if (line != 17 + 1) {
		printf("DPort corrupt: %s/%s\n", bulk->s1, bulk->s2);
		pkg->flags |= PKGF_CORRUPT;
	}
	if (dexec_close(fp, pid)) {
		printf("make -V* command for %s/%s failed\n",
			bulk->s1, bulk->s2);
		pkg->flags |= PKGF_CORRUPT;
	}
	ddassert(bulk->s1);

	/*
	 * DEBUGSTOP mode
	 */
	if (bulk->s4 && bulk->s4[0] == 'd')
		pkg->flags |= PKGF_DEBUGSTOP;

	/*
	 * Generate flavors
	 */
	if (flavor == NULL) {
		/*
		 * If there are flavors add the current unflavored pkg
		 * as a dummy node so dependencies can attach to it,
		 * then iterate the first flavor and loop.
		 *
		 * We must NULL out pkgfile because it will have the
		 * default flavor and conflict with the actual flavored
		 * pkg.
		 */
		if (pkg->flavors && pkg->flavors[0]) {
			dummy_node = pkg;

			pkg->flags |= PKGF_DUMMY;

			freestrp(&pkg->fetch_deps);
			freestrp(&pkg->ext_deps);
			freestrp(&pkg->patch_deps);
			freestrp(&pkg->build_deps);
			freestrp(&pkg->lib_deps);
			freestrp(&pkg->run_deps);

			freestrp(&pkg->pkgfile);
			*list_tail = pkg;
			while (*list_tail)
				list_tail = &(*list_tail)->bnext;

			flavors_save = strdup(pkg->flavors);
			flavors = flavors_save;
			do {
				flavor = strsep(&flavors, " \t");
			} while (flavor && *flavor == 0);
			goto again;
		}

		/*
		 * No flavors, add the current unflavored pkg as a real
		 * node.
		 */
		*list_tail = pkg;
		while (*list_tail)
			list_tail = &(*list_tail)->bnext;
	} else {
		/*
		 * Add flavored package and iterate.
		 */
		*list_tail = pkg;
		while (*list_tail)
			list_tail = &(*list_tail)->bnext;

		/*
		 * Flavor iteration under dummy node, add dependency
		 */
		if (dummy_node) {
			pkglink_t *link;

			ddprintf(0, "Add Dependency %s -> %s (flavor rollup)\n",
				dummy_node->portdir, pkg->portdir);
			link = calloc(1, sizeof(*link));
			link->pkg = pkg;
			link->next = &dummy_node->idepon_list;
			link->prev = dummy_node->idepon_list.prev;
			link->next->prev = link;
			link->prev->next = link;
			link->dep_type = DEP_TYPE_BUILD;

			link = calloc(1, sizeof(*link));
			link->pkg = dummy_node;
			link->next = &pkg->deponi_list;
			link->prev = pkg->deponi_list.prev;
			link->next->prev = link;
			link->prev->next = link;
			link->dep_type = DEP_TYPE_BUILD;
			++pkg->depi_count;
		}

		if (flavors) {
			do {
				flavor = strsep(&flavors, " \t");
			} while (flavor && *flavor == 0);
			if (flavor)
				goto again;
			free(flavors);
		}
	}
}

/*
 * Query the package (at least to make sure it hasn't been truncated)
 * and mark it as PACKAGED if found.
 *
 * Threaded function
 */
static void
childGetBinaryDistInfo(bulk_t *bulk)
{
	char *ptr;
	FILE *fp;
	size_t len;
	pkg_t *pkg;
	const char *cav[MAXCAC];
	char *repopath;
	char buf[1024];
	pid_t pid;
	int cac;

	asprintf(&repopath, "%s/%s", RepositoryPath, bulk->s1);

	cac = 0;
	cav[cac++] = PKG_BINARY;
	cav[cac++] = "query";
	cav[cac++] = "-F";
	cav[cac++] = repopath;
	cav[cac++] = "%n-%v";

	fp = dexec_open(cav, cac, &pid, NULL, 1, 0);

	while ((ptr = fgetln(fp, &len)) != NULL) {
		if (len == 0 || ptr[len-1] != '\n')
			continue;
		ptr[len-1] = 0;
		snprintf(buf, sizeof(buf), "%s%s", ptr, UsePkgSufx);

		pkg = pkg_find(buf);
		if (pkg) {
			pkg->flags |= PKGF_PACKAGED;
		} else {
			ddprintf(0, "Note: package scan, not in list, "
				    "skipping %s\n", buf);
		}
	}
	if (dexec_close(fp, pid)) {
		printf("pkg query command failed for %s\n", repopath);
	}
	free(repopath);
}

static void
childOptimizeEnv(bulk_t *bulk)
{
	char *portpath;
	char *ptr;
	FILE *fp;
	int line;
	size_t len;
	const char *cav[MAXCAC];
	pid_t pid;
	int cac;

	asprintf(&portpath, "%s/%s/%s", DPortsPath, bulk->s1, bulk->s2);

	cac = 0;
	cav[cac++] = MAKE_BINARY;
	cav[cac++] = "-C";
	cav[cac++] = portpath;
	cav[cac++] = "-V_PERL5_FROM_BIN";

	fp = dexec_open(cav, cac, &pid, NULL, 1, 1);
	free(portpath);

	line = 1;
	while ((ptr = fgetln(fp, &len)) != NULL) {
		if (len == 0 || ptr[len-1] != '\n') {
			dfatal("Bad package info for %s/%s response line %d",
			       bulk->s1, bulk->s2, line);
		}
		ptr[--len] = 0;

		switch(line) {
		case 1:		/* _PERL5_FROM_BIN */
			addbuildenv("_PERL5_FROM_BIN", ptr, BENV_ENVIRONMENT);
			break;
		default:
			printf("childOptimizeEnv: EXTRA LINE: %s\n", ptr);
			break;
		}
		++line;
	}
	if (line == 1) {
		printf("DPort not found: %s/%s\n", bulk->s1, bulk->s2);
	} else if (line != 1 + 1) {
		printf("DPort corrupt: %s/%s\n", bulk->s1, bulk->s2);
	}
	if (dexec_close(fp, pid)) {
		printf("childOptimizeEnv() failed\n");
	}
}

static int
scan_and_queue_dir(const char *path, const char *level1, int level)
{
	DIR *dir;
	char *s1;
	char *s2;
	struct dirent *den;
	struct stat st;
	int count = 0;

	dir = opendir(path);
	dassert(dir, "Cannot open dports path \"%s\"", path);

	while ((den = readdir(dir)) != NULL) {
		if (den->d_namlen == 1 && den->d_name[0] == '.')
			continue;
		if (den->d_namlen == 2 &&
		    den->d_name[0] == '.' && den->d_name[1] == '.')
			continue;
		asprintf(&s1, "%s/%s", path, den->d_name);
		if (lstat(s1, &st) < 0 || !S_ISDIR(st.st_mode)) {
			free(s1);
			continue;
		}
		if (level == 1) {
			count += scan_and_queue_dir(s1, den->d_name, 2);
			free(s1);
			continue;
		}
		asprintf(&s2, "%s/Makefile", s1);
		if (lstat(s2, &st) == 0) {
			queuebulk(level1, den->d_name, NULL, NULL);
			++count;
		}
		free(s1);
		free(s2);
	}
	closedir(dir);

	return count;
}

static int
scan_binary_repo(const char *path)
{
	DIR *dir;
	struct dirent *den;
	size_t len;
	int count;

	count = 0;
	dir = opendir(path);
	dassert(dir, "Cannot open repository path \"%s\"", path);

	/*
	 * NOTE: Test includes the '.' in the suffix.
	 */
	while ((den = readdir(dir)) != NULL) {
		len = strlen(den->d_name);
		if (len > 4 &&
		    strcmp(den->d_name + len - 4, UsePkgSufx) == 0) {
			queuebulk(den->d_name, NULL, NULL, NULL);
			++count;
		}
	}
	closedir(dir);

	return count;
}

#if 0
static void
pkgfree(pkg_t *pkg)
{
	freestrp(&pkg->portdir);
	freestrp(&pkg->version);
	freestrp(&pkg->pkgfile);
	freestrp(&pkg->ignore);
	freestrp(&pkg->fetch_deps);
	freestrp(&pkg->ext_deps);
	freestrp(&pkg->patch_deps);
	freestrp(&pkg->build_deps);
	freestrp(&pkg->lib_deps);
	freestrp(&pkg->run_deps);
	freestrp(&pkg->pos_options);
	freestrp(&pkg->neg_options);
	freestrp(&pkg->flavors);
	free(pkg);
}
#endif
