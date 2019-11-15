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
/*
 * Figure out what would have to be built [N]ew, [R]ebuild, [U]pgrade
 * but do not perform any action.
 */
#include "dsynth.h"

static int status_find_leaves(pkg_t *parent, pkg_t *pkg, pkg_t ***build_tailp,
			int *app, int *hasworkp, int level, int first);
static void status_clear_trav(pkg_t *pkg);
static void startstatus(pkg_t **build_listp, pkg_t ***build_tailp);

void
DoStatus(pkg_t *pkgs)
{
	pkg_t *build_list = NULL;
	pkg_t **build_tail = &build_list;
	pkg_t *scan;
	int haswork = 1;
	int first = 1;

	/*
	 * Count up all the packages, do not include dummy packages.
	 */
	for (scan = pkgs; scan; scan = scan->bnext) {
		if ((scan->flags & PKGF_DUMMY) == 0)
			++BuildTotal;
	}

	/*
	 * Nominal bulk build sequence
	 */
	while (haswork) {
		haswork = 0;
		fflush(stdout);
		for (scan = pkgs; scan; scan = scan->bnext) {
			ddprintf(0, "SCANLEAVES %08x %s\n",
				 scan->flags, scan->portdir);
			scan->flags |= PKGF_BUILDLOOP;
			/*
			 * NOTE: We must still find dependencies if PACKAGED
			 *	 to fill in the gaps, as some of them may
			 *	 need to be rebuilt.
			 */
			if (scan->flags & (PKGF_SUCCESS | PKGF_FAILURE |
					   PKGF_ERROR | PKGF_NOBUILD)) {
#if 0
				ddprintf(0, "%s: already built\n",
					 scan->portdir);
#endif
			} else {
				int ap = 0;
				status_find_leaves(NULL, scan, &build_tail,
						  &ap, &haswork, 0, first);
				ddprintf(0, "TOPLEVEL %s %08x\n",
					 scan->portdir, ap);
			}
			scan->flags &= ~PKGF_BUILDLOOP;
			status_clear_trav(scan);
		}
		first = 0;
		fflush(stdout);
		if (haswork == 0)
			break;
		startstatus(&build_list, &build_tail);
	}
	printf("Total packages that would be built: %d/%d\n",
	       BuildSuccessCount, BuildTotal);
}

/*
 * Traverse the packages (pkg) depends on recursively until we find
 * a leaf to build or report as unbuildable.  Calculates and assigns a
 * dependency count.  Returns all parallel-buildable packages.
 *
 * (pkg) itself is only added to the list if it is immediately buildable.
 */
static
int
status_find_leaves(pkg_t *parent, pkg_t *pkg, pkg_t ***build_tailp,
		  int *app, int *hasworkp, int level, int first)
{
	pkglink_t *link;
	pkg_t *scan;
	int idep_count = 0;
	int apsub;

	/*
	 * Already on build list, possibly in-progress, tell caller that
	 * it is not ready.
	 */
	ddprintf(level, "sbuild_find_leaves %d %s %08x {\n",
		 level, pkg->portdir, pkg->flags);
	if (pkg->flags & PKGF_BUILDLIST) {
		ddprintf(level, "} (already on build list)\n");
		*app |= PKGF_NOTREADY;
		return (pkg->idep_count);
	}

	/*
	 * Check dependencies
	 */
	++level;
	PKGLIST_FOREACH(link, &pkg->idepon_list) {
		scan = link->pkg;

		if (scan == NULL)
			continue;
		ddprintf(level, "check %s %08x\t", scan->portdir, scan->flags);

		/*
		 * When accounting for a successful build, just bump
		 * idep_count by one.  scan->idep_count will heavily
		 * overlap packages that we count down multiple branches.
		 *
		 * We must still recurse through PACKAGED packages as
		 * some of their dependencies might be missing.
		 */
		if (scan->flags & PKGF_SUCCESS) {
			ddprintf(0, "SUCCESS - OK\n");
			++idep_count;
			continue;
		}
		if (scan->flags & PKGF_ERROR) {
			ddprintf(0, "ERROR - OK (propagate failure upward)\n");
			*app |= PKGF_NOBUILD_S;
			continue;
		}
		if (scan->flags & PKGF_NOBUILD) {
			ddprintf(0, "NOBUILD - OK "
				    "(propagate failure upward)\n");
			*app |= PKGF_NOBUILD_S;
			continue;
		}

		/*
		 * If already on build-list this dependency is not ready.
		 */
		if (scan->flags & PKGF_BUILDLIST) {
			ddprintf(0, " [BUILDLIST]");
			*app |= PKGF_NOTREADY;
		}

		/*
		 * If not packaged this dependency is not ready for
		 * the caller.
		 */
		if ((scan->flags & PKGF_PACKAGED) == 0) {
			ddprintf(0, " [NOT_PACKAGED]");
			*app |= PKGF_NOTREADY;
		}

		/*
		 * Reduce search complexity, if we have already processed
		 * scan in the traversal it will either already be on the
		 * build list or it will not be buildable.  Either way
		 * the parent is not buildable.
		 */
		if (scan->flags & PKGF_BUILDTRAV) {
			ddprintf(0, " [BUILDTRAV]\n");
			*app |= PKGF_NOTREADY;
			continue;
		}

		/*
		 * Assert on dependency loop
		 */
		++idep_count;
		if (scan->flags & PKGF_BUILDLOOP) {
			dfatal("pkg dependency loop %s -> %s",
				parent->portdir, scan->portdir);
		}
		scan->flags |= PKGF_BUILDLOOP;
		apsub = 0;
		ddprintf(0, " SUBRECURSION {\n");
		idep_count += status_find_leaves(pkg, scan, build_tailp,
						&apsub, hasworkp,
						level + 1, first);
		scan->flags &= ~PKGF_BUILDLOOP;
		*app |= apsub;
		if (apsub & PKGF_NOBUILD) {
			ddprintf(level, "} (sub-nobuild)\n");
		} else if (apsub & PKGF_ERROR) {
			ddprintf(level, "} (sub-error)\n");
		} else if (apsub & PKGF_NOTREADY) {
			ddprintf(level, "} (sub-notready)\n");
		} else {
			ddprintf(level, "} (sub-ok)\n");
		}
	}
	--level;
	pkg->idep_count = idep_count;
	pkg->flags |= PKGF_BUILDTRAV;

	/*
	 * Incorporate scan results into pkg state.
	 */
	if ((pkg->flags & PKGF_NOBUILD) == 0 && (*app & PKGF_NOBUILD)) {
		*hasworkp = 1;
	} else if ((pkg->flags & PKGF_ERROR) == 0 && (*app & PKGF_ERROR)) {
		*hasworkp = 1;
	}
	pkg->flags |= *app & ~PKGF_NOTREADY;

	/*
	 * Clear PACKAGED bit if sub-dependencies aren't clean
	 */
	if ((pkg->flags & PKGF_PACKAGED) &&
	    (pkg->flags & (PKGF_NOTREADY|PKGF_ERROR|PKGF_NOBUILD))) {
		pkg->flags &= ~PKGF_PACKAGED;
		ddassert(pkg->pkgfile);
		*hasworkp = 1;
	}

	/*
	 * Handle propagated flags
	 */
	if (pkg->flags & PKGF_ERROR) {
		ddprintf(level, "} (ERROR - %s)\n", pkg->portdir);
	} else if (pkg->flags & PKGF_NOBUILD) {
		ddprintf(level, "} (SKIPPED - %s)\n", pkg->portdir);
	} else if (*app & PKGF_NOTREADY) {
		/*
		 * We don't set PKGF_NOTREADY in the pkg, it is strictly
		 * a transient flag propagated via build_find_leaves().
		 *
		 * Just don't add the package to the list.
		 */
		;
	} else if (pkg->flags & PKGF_SUCCESS) {
		ddprintf(level, "} (SUCCESS - %s)\n", pkg->portdir);
	} else if (pkg->flags & PKGF_DUMMY) {
		/*
		 * Just mark dummy packages as successful when all of their
		 * sub-depends (flavors) complete successfully.  Note that
		 * dummy packages are not counted in the total, so do not
		 * decrement BuildTotal.
		 */
		ddprintf(level, "} (DUMMY/META - SUCCESS)\n");
		pkg->flags |= PKGF_SUCCESS;
		*hasworkp = 1;
		if (first) {
			dlog(DLOG_ALL | DLOG_FILTER,
			     "[XXX] %s META-ALREADY-BUILT\n",
			     pkg->portdir);
		} else {
			dlog(DLOG_SUCC | DLOG_FILTER,
			     "[XXX] %s meta-node complete\n",
			     pkg->portdir);
		}
	} else if (pkg->flags & PKGF_PACKAGED) {
		/*
		 * We can just mark the pkg successful.  If this is
		 * the first pass, we count this as an initial pruning
		 * pass and reduce BuildTotal.
		 */
		ddprintf(level, "} (PACKAGED - SUCCESS)\n");
		pkg->flags |= PKGF_SUCCESS;
		*hasworkp = 1;
		if (first) {
			dlog(DLOG_ALL | DLOG_FILTER,
			    "[XXX] %s ALREADY-BUILT\n",
			     pkg->portdir);
			--BuildTotal;
		}
	} else {
		/*
		 * All dependencies are successful, queue new work
		 * and indicate not-ready to the parent (since our
		 * package has to be built).
		 */
		*hasworkp = 1;
		ddprintf(level, "} (ADDLIST - %s)\n", pkg->portdir);
		pkg->flags |= PKGF_BUILDLIST;
		**build_tailp = pkg;
		*build_tailp = &pkg->build_next;
		*app |= PKGF_NOTREADY;
	}

	return idep_count;
}

static
void
status_clear_trav(pkg_t *pkg)
{
	pkglink_t *link;
	pkg_t *scan;

	pkg->flags &= ~PKGF_BUILDTRAV;
	PKGLIST_FOREACH(link, &pkg->idepon_list) {
		scan = link->pkg;
		if (scan && (scan->flags & PKGF_BUILDTRAV))
			status_clear_trav(scan);
	}
}

/*
 * This is a fake startbuild() which just marks the build list as built,
 * allowing us to resolve the build tree.
 */
static
void
startstatus(pkg_t **build_listp, pkg_t ***build_tailp)
{
	pkg_t *pkg;

	/*
	 * Nothing to do
	 */
	if (*build_listp == NULL)
		return;

	/*
	 * Sort
	 */
	for (pkg = *build_listp; pkg; pkg = pkg->build_next) {
		if ((pkg->flags & (PKGF_SUCCESS | PKGF_FAILURE |
				   PKGF_ERROR | PKGF_NOBUILD |
				   PKGF_RUNNING)) == 0) {
			pkg->flags |= PKGF_SUCCESS;
			++BuildSuccessCount;
			++BuildCount;
			pkg->flags &= ~PKGF_BUILDLIST;
			printf("  N => %s\n", pkg->portdir);
			/* XXX [R]ebuild and [U]pgrade */
		}

	}
	*build_listp = NULL;
	*build_tailp = build_listp;
}
