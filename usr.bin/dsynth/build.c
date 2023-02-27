/*
 * Copyright (c) 2019-2022 The DragonFly Project.  All rights reserved.
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

#include <netdb.h>

#include "dsynth.h"

static worker_t WorkerAry[MAXWORKERS];
static int BuildInitialized;
static int RunningWorkers;
int DynamicMaxWorkers;
static int FailedWorkers;
static long RunningPkgDepSize;
static pthread_mutex_t DbmMutex;
static pthread_mutex_t WorkerMutex;
static pthread_cond_t WorkerCond;

static int build_find_leaves(pkg_t *parent, pkg_t *pkg,
			pkg_t ***build_tailp, int *app, int *hasworkp,
			int depth, int first, int first_one_only);
static int buildCalculateDepiDepth(pkg_t *pkg);
static void build_clear_trav(pkg_t *pkg);
static void startbuild(pkg_t **build_listp, pkg_t ***build_tailp);
static int qsort_depi(const void *pkg1, const void *pkg2);
static int qsort_idep(const void *pkg1, const void *pkg2);
static void startworker(pkg_t *pkg, worker_t *work);
static void cleanworker(worker_t *work);
static void waitbuild(int whilematch, int dynamicmax);
static void workercomplete(worker_t *work);
static void *childBuilderThread(void *arg);
static int childInstallPkgDeps(worker_t *work);
static size_t childInstallPkgDeps_recurse(FILE *fp, pkglink_t *list,
			int undoit, int depth, int first_one_only);
static void dophase(worker_t *work, wmsg_t *wmsg,
			int wdog, int phaseid, const char *phase);
static void phaseReapAll(void);
static void phaseTerminateSignal(int sig);
static char *buildskipreason(pkglink_t *parent, pkg_t *pkg);
static int buildskipcount_dueto(pkg_t *pkg, int mode);
static int mptylogpoll(int ptyfd, int fdlog, wmsg_t *wmsg,
			time_t *wdog_timep);
static void doHook(pkg_t *pkg, const char *id, const char *path, int waitfor);
static void childHookRun(bulk_t *bulk);
static void adjloadavg(double *dload);
static void check_packaged(const char *dbmpath, pkg_t *pkgs);

static worker_t *SigWork;
static int MasterPtyFd = -1;
static int CopyFileFd = -1;
static pid_t SigPid;
static const char *WorkerFlavorPrt = "";	/* "" or "@flavor" */
static DBM *CheckDBM;

#define MPTY_FAILED	-2
#define MPTY_AGAIN	-1
#define MPTY_EOF	0
#define MPTY_DATA	1

int BuildTotal;
int BuildCount;
int BuildSkipCount;
int BuildIgnoreCount;
int BuildFailCount;
int BuildSuccessCount;
int BuildMissingCount;
int BuildMetaCount;
int PkgVersionPkgSuffix;

/*
 * Initialize the WorkerAry[]
 */
void
DoInitBuild(int slot_override)
{
	worker_t *work;
	struct stat st;
	char *path;
	int i;

	ddassert(slot_override < 0 || MaxWorkers == 1);

	bzero(WorkerAry, MaxWorkers * sizeof(worker_t));
	pthread_mutex_init(&WorkerMutex, NULL);
	pthread_mutex_init(&DbmMutex, NULL);

	for (i = 0; i < MaxWorkers; ++i) {
		work = &WorkerAry[i];
		work->index = (slot_override >= 0) ? slot_override : i;
		work->state = WORKER_NONE;
		asprintf(&work->basedir, "%s/SL%02d", BuildBase, work->index);
		pthread_cond_init(&work->cond, NULL);
	}
	BuildCount = 0;

	/*
	 * Create required sub-directories. The base directories must already
	 * exist as a dsynth configuration safety.
	 */
	if (stat(RepositoryPath, &st) < 0) {
		if (mkdir(RepositoryPath, 0755) < 0)
			dfatal("Cannot mkdir %s\n", RepositoryPath);
	}

	/*
	 * An empty directory for LOCALBASE= in the pkg scan.
	 */
	asprintf(&path, "%s/empty", BuildBase);
	if (stat(path, &st) < 0) {
		if (mkdir(path, 0755) < 0)
			dfatal("Cannot mkdir %s\n", path);
	}
	free(path);

	BuildInitialized = 1;

	/*
	 * slow-start (increases at a rate of 1 per 5 seconds)
	 */
	if (SlowStartOpt > MaxWorkers)
		DynamicMaxWorkers = MaxWorkers;
	else if (SlowStartOpt > 0)
		DynamicMaxWorkers = SlowStartOpt;
	else
		DynamicMaxWorkers = MaxWorkers;
}

/*
 * Called by the frontend to clean-up any hanging mounts.
 */
void
DoCleanBuild(int resetlogs)
{
	int i;

	ddassert(BuildInitialized);

	if (resetlogs)
		dlogreset();
	for (i = 0; i < MaxWorkers; ++i) {
		DoWorkerUnmounts(&WorkerAry[i]);
	}
}

void
DoBuild(pkg_t *pkgs)
{
	pkg_t *build_list = NULL;
	pkg_t **build_tail = &build_list;
	pkg_t *scan;
	bulk_t *bulk;
	int haswork = 1;
	int first = 1;
	int newtemplate;
	time_t startTime;
	time_t t;
	int h, m, s;
	char *dbmpath;

	/*
	 * We use our bulk system to run hooks.  This will be for
	 * Skipped and Ignored.  Success and Failure are handled by
	 * WorkerProcess() which is a separately-exec'd dsynth.
	 */
	if (UsingHooks)
		initbulk(childHookRun, MaxBulk);

	/*
	 * Count up the packages, not counting dummies
	 */
	for (scan = pkgs; scan; scan = scan->bnext) {
		if ((scan->flags & PKGF_DUMMY) == 0)
			++BuildTotal;
	}

	/*
	 * Remove binary package files for dports whos directory
	 * has changed.
	 */
	asprintf(&dbmpath, "%s/ports_crc", BuildBase);
	CheckDBM = dbm_open(dbmpath, O_CREAT|O_RDWR, 0644);
	check_packaged(dbmpath, pkgs);

	doHook(NULL, "hook_run_start", HookRunStart, 1);

	/*
	 * The pkg and pkg-static binaries are needed.  If already present
	 * then assume that the template is also valid, otherwise add to
	 * the list and build both.
	 */
	scan = GetPkgPkg(&pkgs);

	/*
	 * Create our template.  The template will be missing pkg
	 * and pkg-static.
	 */
	if (FetchOnlyOpt) {
		newtemplate = DoCreateTemplate(0);
	} else if ((scan->flags & (PKGF_SUCCESS | PKGF_PACKAGED)) == 0) {
		/* force a fresh template */
		newtemplate = DoCreateTemplate(1);
	} else {
		newtemplate = DoCreateTemplate(0);
	}

	/*
	 * This will clear the screen and set-up our gui, so sleep
	 * a little first in case the user wants to see what was
	 * printed before.
	 */
	sleep(2);
	pthread_mutex_lock(&WorkerMutex);
	startTime = time(NULL);
	RunStatsInit();
	RunStatsReset();

	/*
	 * Build pkg/pkg-static
	 */
	if ((scan->flags & (PKGF_SUCCESS | PKGF_PACKAGED)) == 0 && FetchOnlyOpt == 0) {
		build_list = scan;
		build_tail = &scan->build_next;
		startbuild(&build_list, &build_tail);
		while (RunningWorkers == 1)
			waitbuild(1, 0);

		if (scan->flags & PKGF_NOBUILD)
			dfatal("Unable to build 'pkg'");
		if (scan->flags & PKGF_ERROR)
			dfatal("Error building 'pkg'");
		if ((scan->flags & PKGF_SUCCESS) == 0)
			dfatal("Error building 'pkg'");
		newtemplate = 1;
	}

	/*
	 * Install pkg/pkg-static into the template
	 */
	if (newtemplate && FetchOnlyOpt == 0) {
		char *buf;
		int rc;

		asprintf(&buf,
			 "cd %s/Template; "
			 "tar --exclude '+*' --exclude '*/man/*' "
			 "-xvzpf %s/%s > /dev/null 2>&1",
			 BuildBase,
			 RepositoryPath,
			 scan->pkgfile);
		rc = system(buf);
		if (rc)
			dfatal("Command failed: %s\n", buf);
		freestrp(&buf);
	}

	/*
	 * Figure out pkg version
	 */
	if (scan->version) {
		int v1;
		int v2;

		dlog(DLOG_ALL, "[XXX] pkg version %s\n", scan->version);
		if (sscanf(scan->version, "%d.%d", &v1, &v2) == 2) {
			if ((v1 == 1 && v2 >= 17) || v1 > 1)
			    PkgVersionPkgSuffix = 1;
		}
	}

	/*
	 * Calculate depi_depth, the longest chain of dependencies
	 * for who depends on me, weighted by powers of two.
	 */
	for (scan = pkgs; scan; scan = scan->bnext) {
		buildCalculateDepiDepth(scan);
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
					   PKGF_ERROR)) {
#if 0
				ddprintf(0, "%s: already built\n",
					 scan->portdir);
#endif
			} else {
				int ap = 0;
				build_find_leaves(NULL, scan, &build_tail,
						  &ap, &haswork, 0, first, 0);
				ddprintf(0, "TOPLEVEL %s %08x\n",
					 scan->portdir, ap);
			}
			scan->flags &= ~PKGF_BUILDLOOP;
			build_clear_trav(scan);
		}
		first = 0;
		fflush(stdout);
		startbuild(&build_list, &build_tail);

		if (haswork == 0 && RunningWorkers) {
			waitbuild(RunningWorkers, 1);
			haswork = 1;
		}
	}
	pthread_mutex_unlock(&WorkerMutex);

	/*
	 * What is left that cannot be built?  The list really ought to be
	 * empty at this point, report anything that remains.
	 */
	for (scan = pkgs; scan; scan = scan->bnext) {
		if (scan->flags & (PKGF_SUCCESS | PKGF_FAILURE))
			continue;
		dlog(DLOG_ABN, "[XXX] %s lost in the ether [flags=%08x]\n",
		     scan->portdir, scan->flags);
		++BuildMissingCount;
	}

	/*
	 * Final updates
	 */
	RunStatsUpdateTop(0);
	RunStatsUpdateLogs();
	RunStatsSync();
	RunStatsDone();

	doHook(NULL, "hook_run_end", HookRunEnd, 1);
	if (UsingHooks) {
		while ((bulk = getbulk()) != NULL)
			freebulk(bulk);
		donebulk();
	}

	t = time(NULL) - startTime;
	h = t / 3600;
	m = t / 60 % 60;
	s = t % 60;

	if (CheckDBM) {
		dbm_close(CheckDBM);
		CheckDBM = NULL;
	}

	dlog(DLOG_ALL|DLOG_STDOUT,
		"\n"
		"Initial queue size: %d\n"
		"    packages built: %d\n"
		"           ignored: %d\n"
		"           skipped: %d\n"
		"            failed: %d\n"
		"           missing: %d\n"
		"        meta-nodes: %d\n"
		"\n"
		"Duration: %02d:%02d:%02d\n"
		"\n",
		BuildTotal,
		BuildSuccessCount,
		BuildIgnoreCount,
		BuildSkipCount,
		BuildFailCount,
		BuildMissingCount,
		BuildMetaCount,
		h, m, s);
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
build_find_leaves(pkg_t *parent, pkg_t *pkg, pkg_t ***build_tailp,
		  int *app, int *hasworkp, int depth, int first,
		  int first_one_only)
{
	pkglink_t *link;
	pkg_t *scan;
	int idep_count = 0;
	int apsub;
	int dfirst_one_only;
	int ndepth;
	char *buf;

	ndepth = depth + 1;

	/*
	 * Already on build list, possibly in-progress, tell caller that
	 * it is not ready.
	 */
	ddprintf(ndepth, "sbuild_find_leaves %d %s %08x {\n",
		 depth, pkg->portdir, pkg->flags);
	if (pkg->flags & PKGF_BUILDLIST) {
		ddprintf(ndepth, "} (already on build list)\n");
		*app |= PKGF_NOTREADY;
		return (pkg->idep_count);
	}

	/*
	 * Check dependencies
	 */
	PKGLIST_FOREACH(link, &pkg->idepon_list) {
		scan = link->pkg;

		if (scan == NULL) {
			if (first_one_only)
				break;
			continue;
		}
		ddprintf(ndepth, "check %s %08x\t", scan->portdir, scan->flags);

		/*
		 * If this dependency is to a DUMMY node it is a dependency
		 * only on the default flavor which is only the first node
		 * under this one, not all of them.
		 *
		 * DUMMY nodes can be marked SUCCESS so the build skips past
		 * them, but this doesn't mean that their sub-nodes succeeded.
		 * We have to check, so recurse even if it is marked
		 * successful.
		 *
		 * NOTE: The depth is not being for complex dependency type
		 *	 tests like it is in childInstallPkgDeps_recurse(),
		 *	 so we don't have to hicup it like we do in that
		 *	 procedure.
		 */
		dfirst_one_only = (scan->flags & PKGF_DUMMY) ? 1 : 0;
		if (dfirst_one_only)
			goto skip_to_flavor;

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
			if (first_one_only)
				break;
			continue;
		}

		/*
		 * ERROR includes FAILURE, which is set in numerous situations
		 * including when NOBUILD state is finally processed.  So
		 * check for NOBUILD state first.
		 *
		 * An ERROR in a sub-package causes a NOBUILD in packages
		 * that depend on it.
		 */
		if (scan->flags & PKGF_NOBUILD) {
			ddprintf(0, "NOBUILD - OK "
				    "(propagate failure upward)\n");
			*app |= PKGF_NOBUILD_S;
			if (first_one_only)
				break;
			continue;
		}
		if (scan->flags & PKGF_ERROR) {
			ddprintf(0, "ERROR - OK (propagate failure upward)\n");
			*app |= PKGF_NOBUILD_S;
			if (first_one_only)
				break;
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
			if (first_one_only)
				break;
			continue;
		}
skip_to_flavor:

		/*
		 * Assert on dependency loop
		 */
		++idep_count;
		if (scan->flags & PKGF_BUILDLOOP) {
			dfatal("pkg dependency loop %s -> %s",
				parent->portdir, scan->portdir);
		}

		/*
		 * NOTE: For debug tabbing purposes we use (ndepth + 1)
		 *	 here (i.e. depth + 2) in our iteration.
		 */
		scan->flags |= PKGF_BUILDLOOP;
		apsub = 0;
		ddprintf(0, " SUBRECURSION {\n");
		idep_count += build_find_leaves(pkg, scan, build_tailp,
						&apsub, hasworkp,
						ndepth + 1, first,
						dfirst_one_only);
		scan->flags &= ~PKGF_BUILDLOOP;
		*app |= apsub;
		if (apsub & PKGF_NOBUILD) {
			ddprintf(ndepth, "} (sub-nobuild)\n");
		} else if (apsub & PKGF_ERROR) {
			ddprintf(ndepth, "} (sub-error)\n");
		} else if (apsub & PKGF_NOTREADY) {
			ddprintf(ndepth, "} (sub-notready)\n");
		} else {
			ddprintf(ndepth, "} (sub-ok)\n");
		}
		if (first_one_only)
			break;
	}
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
	 * Clear the PACKAGED bit if sub-dependencies aren't clean.
	 *
	 * NOTE: PKGF_NOTREADY is not stored in pkg->flags, only in *app,
	 *	 so incorporate *app to test for it.
	 */
	if ((pkg->flags & PKGF_PACKAGED) &&
	    ((pkg->flags | *app) & (PKGF_NOTREADY|PKGF_ERROR|PKGF_NOBUILD))) {
		pkg->flags &= ~PKGF_PACKAGED;
		ddassert(pkg->pkgfile);
		asprintf(&buf, "%s/%s", RepositoryPath, pkg->pkgfile);
		if (OverridePkgDeleteOpt >= 1) {
			pkg->flags |= PKGF_PACKAGED;
			dlog(DLOG_ALL,
			     "[XXX] %s DELETE-PACKAGE %s "
			     "(OVERRIDE, NOT DELETED)\n",
			     pkg->portdir, buf);
		} else if (remove(buf) < 0) {
			dlog(DLOG_ALL,
			     "[XXX] %s DELETE-PACKAGE %s (failed)\n",
			     pkg->portdir, buf);
		} else {
			dlog(DLOG_ALL,
			     "[XXX] %s DELETE-PACKAGE %s "
			     "(due to dependencies)\n",
			     pkg->portdir, buf);
		}
		freestrp(&buf);
		*hasworkp = 1;
	}

	/*
	 * Set PKGF_NOBUILD_I if there is IGNORE data
	 */
	if (pkg->ignore) {
		pkg->flags |= PKGF_NOBUILD_I;
	}

	/*
	 * Handle propagated flags
	 */
	if (pkg->flags & PKGF_ERROR) {
		/*
		 * This can only happen if the ERROR has already been
		 * processed and accounted for.
		 */
		ddprintf(depth, "} (ERROR - %s)\n", pkg->portdir);
	} else if (*app & PKGF_NOTREADY) {
		/*
		 * We don't set PKGF_NOTREADY in the pkg, it is strictly
		 * a transient flag propagated via build_find_leaves().
		 *
		 * Just don't add the package to the list.
		 *
		 * NOTE: Even if NOBUILD is set (meaning we could list it
		 *	 and let startbuild() finish it up as a skip, we
		 *	 don't process it to the list because we want to
		 *	 process all the dependencies, so someone doing a
		 *	 manual build can get more complete information and
		 *	 does not have to iterate each failed dependency one
		 *	 at a time.
		 */
		;
	} else if (pkg->flags & PKGF_SUCCESS) {
		ddprintf(depth, "} (SUCCESS - %s)\n", pkg->portdir);
	} else if (pkg->flags & PKGF_DUMMY) {
		/*
		 * Just mark dummy packages as successful when all of their
		 * sub-depends (flavors) complete successfully.  Note that
		 * dummy packages are not counted in the total, so do not
		 * decrement BuildTotal.
		 *
		 * Do not propagate *app up for the dummy node or add it to
		 * the build list.  The dummy node itself is not an actual
		 * dependency.  Packages which depend on the default flavor
		 * (aka this dummy node) actually depend on the first flavor
		 * under this node.
		 *
		 * So if there is a generic dependency (i.e. no flavor
		 * specified), the upper recursion detects PKGF_DUMMY and
		 * traverses through the dummy node to the default flavor
		 * without checking the error/nobuild flags on this dummy
		 * node.
		 */
		if (pkg->flags & PKGF_NOBUILD) {
			ddprintf(depth, "} (DUMMY/META - IGNORED "
				 "- MARK SUCCESS ANYWAY)\n");
		} else {
			ddprintf(depth, "} (DUMMY/META - SUCCESS)\n");
		}
		pkg->flags |= PKGF_SUCCESS;
		*hasworkp = 1;
		if (first) {
			dlog(DLOG_ALL | DLOG_FILTER,
			     "[XXX] %s META-ALREADY-BUILT\n",
			     pkg->portdir);
		} else {
			dlog(DLOG_SUCC, "[XXX] %s meta-node complete\n",
			     pkg->portdir);
			RunStatsUpdateCompletion(NULL, DLOG_SUCC, pkg, "", "");
			++BuildMetaCount;   /* Only for not built meta nodes */
		}
	} else if (pkg->flags & PKGF_PACKAGED) {
		/*
		 * We can just mark the pkg successful.  If this is
		 * the first pass, we count this as an initial pruning
		 * pass and reduce BuildTotal.
		 */
		ddprintf(depth, "} (PACKAGED - SUCCESS)\n");
		pkg->flags |= PKGF_SUCCESS;
		*hasworkp = 1;
		if (first) {
			dlog(DLOG_ALL | DLOG_FILTER,
			     "[XXX] %s Already-Built\n",
			     pkg->portdir);
			--BuildTotal;
		} else {
			dlog(DLOG_ABN | DLOG_FILTER,
			     "[XXX] %s flags=%08x Packaged Unexpectedly\n",
			     pkg->portdir, pkg->flags);
			/* ++BuildSuccessTotal; XXX not sure */
			goto addlist;
		}
	} else {
		/*
		 * All dependencies are successful, queue new work
		 * and indicate not-ready to the parent (since our
		 * package has to be built).
		 *
		 * NOTE: The NOBUILD case propagates to here as well
		 *	 and is ultimately handled by startbuild().
		 */
addlist:
		*hasworkp = 1;
		if (pkg->flags & PKGF_NOBUILD_I)
			ddprintf(depth, "} (ADDLIST(IGNORE/BROKEN) - %s)\n",
				 pkg->portdir);
		else if (pkg->flags & PKGF_NOBUILD)
			ddprintf(depth, "} (ADDLIST(NOBUILD) - %s)\n",
				 pkg->portdir);
		else
			ddprintf(depth, "} (ADDLIST - %s)\n", pkg->portdir);
		pkg->flags |= PKGF_BUILDLIST;
		**build_tailp = pkg;
		*build_tailp = &pkg->build_next;
		pkg->build_next = NULL;
		*app |= PKGF_NOTREADY;
	}

	return idep_count;
}

static
void
build_clear_trav(pkg_t *pkg)
{
	pkglink_t *link;
	pkg_t *scan;

	pkg->flags &= ~PKGF_BUILDTRAV;
	PKGLIST_FOREACH(link, &pkg->idepon_list) {
		scan = link->pkg;
		if (scan && (scan->flags & PKGF_BUILDTRAV))
			build_clear_trav(scan);
	}
}

/*
 * Calculate the longest chain of packages that depend on me.  The
 * long the chain, the more important my package is to build earlier
 * rather than later.
 */
static int
buildCalculateDepiDepth(pkg_t *pkg)
{
	pkglink_t *link;
	pkg_t *scan;
	int best_depth = 0;
	int res;

	if (pkg->depi_depth)
		return(pkg->depi_depth + 1);
	pkg->flags |= PKGF_BUILDLOOP;
	PKGLIST_FOREACH(link, &pkg->deponi_list) {
		scan = link->pkg;
		if (scan && (scan->flags & PKGF_BUILDLOOP) == 0) {
			res = buildCalculateDepiDepth(scan);
			if (best_depth < res)
				best_depth = res;
		}
	}
	pkg->flags &= ~PKGF_BUILDLOOP;
	pkg->depi_depth = best_depth;

	return (best_depth + 1);
}

/*
 * Take a list of pkg ready to go, sort it, and assign it to worker
 * slots.  This routine blocks in waitbuild() until it can dispose of
 * the entire list.
 *
 * WorkerMutex is held by the caller.
 */
static
void
startbuild(pkg_t **build_listp, pkg_t ***build_tailp)
{
	pkg_t *pkg;
	pkg_t **idep_ary;
	pkg_t **depi_ary;
	int count;
	int idep_index;
	int depi_index;
	int i;
	int n;
	worker_t *work;
	static int IterateWorker;

	/*
	 * Nothing to do
	 */
	if (*build_listp == NULL)
		return;

	/*
	 * Sort
	 */
	count = 0;
	for (pkg = *build_listp; pkg; pkg = pkg->build_next)
		++count;
	idep_ary = calloc(count, sizeof(pkg_t *));
	depi_ary = calloc(count, sizeof(pkg_t *));

	count = 0;
	for (pkg = *build_listp; pkg; pkg = pkg->build_next) {
		idep_ary[count] = pkg;
		depi_ary[count] = pkg;
		++count;
	}

	/*
	 * idep_ary - sorted by #of dependencies this pkg has.
	 * depi_ary - sorted by #of other packages that depend on this pkg.
	 */
	qsort(idep_ary, count, sizeof(pkg_t *), qsort_idep);
	qsort(depi_ary, count, sizeof(pkg_t *), qsort_depi);

	idep_index = 0;
	depi_index = 0;

	/*
	 * Half the workers build based on the highest depi count,
	 * the other half build based on the highest idep count.
	 *
	 * This is an attempt to get projects which many other projects
	 * depend on built first, but to also try to build large projects
	 * (which tend to have a lot of dependencies) earlier rather than
	 * later so the end of the bulk run doesn't inefficiently build
	 * the last few huge projects.
	 *
	 * Loop until we manage to assign slots to everyone.  We do not
	 * wait for build completion.
	 *
	 * This is the point where we handle DUMMY packages (these are
	 * dummy unflavored packages which 'cover' all the flavors for
	 * a package).  These are not real packages are marked SUCCESS
	 * at this time because their dependencies (the flavors) have all
	 * been built.
	 */
	while (idep_index != count || depi_index != count) {
		pkg_t *pkgi;
		pkg_t *ipkg;

		/*
		 * Find candidate to start sorted by depi or idep.
		 */
		ipkg = NULL;
		while (idep_index < count) {
			ipkg = idep_ary[idep_index];
			if ((ipkg->flags &
			     (PKGF_SUCCESS | PKGF_FAILURE |
			      PKGF_ERROR | PKGF_RUNNING)) == 0) {
				break;
			}
			ipkg = NULL;
			++idep_index;
		}

		pkgi = NULL;
		while (depi_index < count) {
			pkgi = depi_ary[depi_index];
			if ((pkgi->flags &
			     (PKGF_SUCCESS | PKGF_FAILURE |
			      PKGF_ERROR | PKGF_RUNNING)) == 0) {
				break;
			}
			pkgi = NULL;
			++depi_index;
		}

		/*
		 * ipkg and pkgi must either both be NULL, or both
		 * be non-NULL.
		 */
		if (ipkg == NULL && pkgi == NULL)
			break;
		ddassert(ipkg && pkgi);

		/*
		 * Handle the NOBUILD case right here, there's no point
		 * queueing it anywhere.
		 */
		if (ipkg->flags & PKGF_NOBUILD) {
			char *reason;
			char skipbuf[16];
			int scount;

			scount = buildskipcount_dueto(ipkg, 1);
			buildskipcount_dueto(ipkg, 0);
			if (scount) {
				snprintf(skipbuf, sizeof(skipbuf), " %d",
					 scount);
			} else {
				skipbuf[0] = 0;
			}

			ipkg->flags |= PKGF_FAILURE;
			ipkg->flags &= ~PKGF_BUILDLIST;

			reason = buildskipreason(NULL, ipkg);
			if (ipkg->flags & PKGF_NOBUILD_I) {
				++BuildIgnoreCount;
				dlog(DLOG_IGN,
				     "[XXX] %s%s ignored due to %s\n",
				     ipkg->portdir, skipbuf, reason);
				RunStatsUpdateCompletion(NULL, DLOG_IGN, ipkg,
							 reason, skipbuf);
				doHook(ipkg, "hook_pkg_ignored",
				       HookPkgIgnored, 0);
			} else {
				++BuildSkipCount;
				dlog(DLOG_SKIP,
				     "[XXX] %s%s skipped due to %s\n",
				     ipkg->portdir, skipbuf, reason);
				RunStatsUpdateCompletion(NULL, DLOG_SKIP, ipkg,
							 reason, skipbuf);
				doHook(ipkg, "hook_pkg_skipped",
				       HookPkgSkipped, 0);
			}
			free(reason);
			++BuildCount;
			continue;
		}
		if (pkgi->flags & PKGF_NOBUILD) {
			char *reason;
			char skipbuf[16];
			int scount;

			scount = buildskipcount_dueto(pkgi, 1);
			buildskipcount_dueto(pkgi, 0);
			if (scount) {
				snprintf(skipbuf, sizeof(skipbuf), " %d",
					 scount);
			} else {
				skipbuf[0] = 0;
			}

			pkgi->flags |= PKGF_FAILURE;
			pkgi->flags &= ~PKGF_BUILDLIST;

			reason = buildskipreason(NULL, pkgi);
			if (pkgi->flags & PKGF_NOBUILD_I) {
				++BuildIgnoreCount;
				dlog(DLOG_IGN, "[XXX] %s%s ignored due to %s\n",
				     pkgi->portdir, skipbuf, reason);
				RunStatsUpdateCompletion(NULL, DLOG_IGN, pkgi,
							 reason, skipbuf);
				doHook(pkgi, "hook_pkg_ignored",
				       HookPkgIgnored, 0);
			} else {
				++BuildSkipCount;
				dlog(DLOG_SKIP,
				     "[XXX] %s%s skipped due to %s\n",
				     pkgi->portdir, skipbuf, reason);
				RunStatsUpdateCompletion(NULL, DLOG_SKIP, pkgi,
							 reason, skipbuf);
				doHook(pkgi, "hook_pkg_skipped",
				       HookPkgSkipped, 0);
			}
			free(reason);
			++BuildCount;
			continue;
		}

		/*
		 * Block while no slots are available.  waitbuild()
		 * will clean out any DONE states.
		 */
		while (RunningWorkers >= DynamicMaxWorkers ||
		       RunningWorkers >= MaxWorkers - FailedWorkers) {
			waitbuild(RunningWorkers, 1);
		}

		/*
		 * Find an available worker slot, there should be at
		 * least one.
		 */
		for (i = 0; i < MaxWorkers; ++i) {
			n = IterateWorker % MaxWorkers;
			work = &WorkerAry[n];

			if (work->state == WORKER_DONE ||
			    work->state == WORKER_FAILED) {
				workercomplete(work);
			}
			if (work->state == WORKER_NONE ||
			    work->state == WORKER_IDLE) {
				if (n <= MaxWorkers / 2) {
					startworker(pkgi, work);
				} else {
					startworker(ipkg, work);
				}
				/*RunStatsUpdate(work);*/
				break;
			}
			++IterateWorker;
		}
		ddassert(i != MaxWorkers);
	}
	RunStatsSync();

	/*
	 * We disposed of the whole list
	 */
	free(idep_ary);
	free(depi_ary);
	*build_listp = NULL;
	*build_tailp = build_listp;
}

typedef const pkg_t *pkg_tt;

static int
qsort_idep(const void *pkg1_arg, const void *pkg2_arg)
{
	const pkg_t *pkg1 = *(const pkg_tt *)pkg1_arg;
	const pkg_t *pkg2 = *(const pkg_tt *)pkg2_arg;

	return (pkg2->idep_count - pkg1->idep_count);
}

static int
qsort_depi(const void *pkg1_arg, const void *pkg2_arg)
{
	const pkg_t *pkg1 = *(const pkg_tt *)pkg1_arg;
	const pkg_t *pkg2 = *(const pkg_tt *)pkg2_arg;

	return ((pkg2->depi_count * pkg2->depi_depth) -
		(pkg1->depi_count * pkg1->depi_depth));
}

/*
 * Frontend starts a pkg up on a worker
 *
 * WorkerMutex must be held.
 */
static void
startworker(pkg_t *pkg, worker_t *work)
{
	switch(work->state) {
	case WORKER_NONE:
		pthread_create(&work->td, NULL, childBuilderThread, work);
		work->state = WORKER_IDLE;
		/* fall through */
	case WORKER_IDLE:
		work->pkg_dep_size =
		childInstallPkgDeps_recurse(NULL, &pkg->idepon_list, 0, 1, 0);
		childInstallPkgDeps_recurse(NULL, &pkg->idepon_list, 1, 1, 0);
		RunningPkgDepSize += work->pkg_dep_size;

		dlog(DLOG_ALL, "[%03d] START   %s "
			       "##idep=%02d depi=%02d/%02d dep=%-4.2fG\n",
		     work->index, pkg->portdir,
		     pkg->idep_count, pkg->depi_count, pkg->depi_depth,
		     (double)work->pkg_dep_size / (double)ONEGB);

		cleanworker(work);
		pkg->flags |= PKGF_RUNNING;
		work->pkg = pkg;
		pthread_cond_signal(&work->cond);
		++RunningWorkers;
		/*RunStatsUpdate(work);*/
		break;
	case WORKER_PENDING:
	case WORKER_RUNNING:
	case WORKER_DONE:
	case WORKER_FAILED:
	case WORKER_FROZEN:
	case WORKER_EXITING:
	default:
		dfatal("startworker: [%03d] Unexpected state %d for worker %d",
		       work->index, work->state, work->index);
		break;
	}
}

static void
cleanworker(worker_t *work)
{
	work->state = WORKER_PENDING;
	work->flags = 0;
	work->accum_error = 0;
	work->start_time = time(NULL);
}

/*
 * Frontend finishes up a completed pkg on a worker.
 *
 * If the worker is in a FAILED state we clean the pkg out but (for now)
 * leave it in its failed state so we can debug.  At this point
 * workercomplete() will be called every once in a while on the state
 * and we have to deal with the NULL pkg.
 *
 * WorkerMutex must be held.
 */
static void
workercomplete(worker_t *work)
{
	pkg_t *pkg;
	time_t t;
	int h;
	int m;
	int s;

	/*
	 * Steady state FAILED case.
	 */
	if (work->state == WORKER_FAILED) {
		if (work->pkg == NULL)
			return;
	}

	t = time(NULL) - work->start_time;
	h = t / 3600;
	m = t / 60 % 60;
	s = t % 60;

	/*
	 * Reduce total dep size
	 */
	RunningPkgDepSize -= work->pkg_dep_size;
	RunningPkgDepSize -= work->memuse;
	work->pkg_dep_size = 0;
	work->memuse = 0;

	/*
	 * Process pkg out of the worker
	 */
	pkg = work->pkg;
	if (pkg->flags & (PKGF_ERROR|PKGF_NOBUILD)) {
		char skipbuf[16];
		int scount;

		/*
		 * Normally mark the package as failed, but if we are doing
		 * a fetch-only, mark it as successful so dependant ports
		 * still get fetched.
		 */
		if (FetchOnlyOpt)
			pkg->flags |= PKGF_SUCCESS;
		else
			pkg->flags |= PKGF_FAILURE;

		scount = buildskipcount_dueto(pkg, 1);
		buildskipcount_dueto(pkg, 0);
		if (scount) {
			snprintf(skipbuf, sizeof(skipbuf), " %d",
				 scount);
		} else {
			skipbuf[0] = 0;
		}

		/*
		 * This NOBUILD condition XXX can occur if the package is
		 * not allowed to be built.
		 */
		if (pkg->flags & PKGF_NOBUILD) {
			char *reason;

			reason = buildskipreason(NULL, pkg);
			if (pkg->flags & PKGF_NOBUILD_I) {
				++BuildIgnoreCount;
				dlog(DLOG_SKIP, "[%03d] IGNORD %s%s - %s\n",
				     work->index, pkg->portdir,
				     skipbuf, reason);
				RunStatsUpdateCompletion(work, DLOG_SKIP, pkg,
							 reason, skipbuf);
				doHook(pkg, "hook_pkg_ignored",
				       HookPkgIgnored, 0);
			} else {
				++BuildSkipCount;
				dlog(DLOG_SKIP, "[%03d] SKIPPD %s%s - %s\n",
				     work->index, pkg->portdir,
				     skipbuf, reason);
				RunStatsUpdateCompletion(work, DLOG_SKIP, pkg,
							 reason, skipbuf);
				doHook(pkg, "hook_pkg_skipped",
				       HookPkgSkipped, 0);
			}
			free(reason);
		} else {
			++BuildFailCount;
			dlog(DLOG_FAIL | DLOG_RED,
			     "[%03d] FAILURE %s%s ##%16.16s %02d:%02d:%02d\n",
			     work->index, pkg->portdir, skipbuf,
			     getphasestr(work->phase),
			     h, m, s);
			RunStatsUpdateCompletion(work, DLOG_FAIL, pkg,
						 skipbuf, "");
			doHook(pkg, "hook_pkg_failure", HookPkgFailure, 0);
		}
	} else {
		if (CheckDBM) {
			datum key;
			datum data;

			key.dptr = pkg->portdir;
			key.dsize = strlen(pkg->portdir);
			data.dptr = &pkg->crc32;
			data.dsize = sizeof(pkg->crc32);
#ifndef __DB_IS_THREADSAFE
			pthread_mutex_lock(&DbmMutex);
#endif
			dbm_store(CheckDBM, key, data, DBM_REPLACE);
#ifndef __DB_IS_THREADSAFE
			pthread_mutex_unlock(&DbmMutex);
#endif
		}
		pkg->flags |= PKGF_SUCCESS;
		++BuildSuccessCount;
		dlog(DLOG_SUCC | DLOG_GRN,
		     "[%03d] SUCCESS %s ##%02d:%02d:%02d\n",
		     work->index, pkg->portdir, h, m, s);
		RunStatsUpdateCompletion(work, DLOG_SUCC, pkg, "", "");
		doHook(pkg, "hook_pkg_success", HookPkgSuccess, 0);
	}
	++BuildCount;
	pkg->flags &= ~PKGF_BUILDLIST;
	pkg->flags &= ~PKGF_RUNNING;
	work->pkg = NULL;
	--RunningWorkers;

	if (work->state == WORKER_FAILED) {
		dlog(DLOG_ALL, "[%03d] XXX/XXX WORKER IS IN A FAILED STATE\n",
		     work->index);
		++FailedWorkers;
	} else if (work->flags & WORKERF_FREEZE) {
		dlog(DLOG_ALL, "[%03d] FROZEN(DEBUG) %s\n",
		     work->index, pkg->portdir);
		work->state = WORKER_FROZEN;
	} else {
		work->state = WORKER_IDLE;
	}
}

/*
 * Wait for one or more workers to complete.
 *
 * WorkerMutex must be held.
 */
static void
waitbuild(int whilematch, int dynamicmax)
{
	static time_t wblast_time;
	static time_t dmlast_time;
	struct timespec ts;
	worker_t *work;
	time_t t;
	int i;

	if (whilematch == 0)
		whilematch = 1;

	while (RunningWorkers == whilematch) {
		for (i = 0; i < MaxWorkers; ++i) {
			work = &WorkerAry[i];
			if (work->state == WORKER_DONE ||
			    work->state == WORKER_FAILED) {
				workercomplete(work);
			} else {
				pthread_cond_signal(&work->cond);
			}
			RunStatsUpdate(work, NULL);
		}
		RunStatsUpdateTop(1);
		RunStatsUpdateLogs();
		RunStatsSync();
		if (RunningWorkers == whilematch) {
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 1;
			ts.tv_nsec = 0;
			pthread_cond_timedwait(&WorkerCond, &WorkerMutex, &ts);
		}

		/*
		 * Dynamically reduce MaxWorkers based on the load.  When
		 * the load exceeds 2 x ncpus we reduce the number of workers
		 * up to 75% of MaxWorkers @ (5 x ncpus) load.
		 *
		 * Dynamically reduce MaxWorkers based on swap use, starting
		 * at 10% swap and up to 75% of MaxWorkers at 40% swap.
		 *
		 * NOTE! Generally speaking this allows more workers to be
		 *	 configured which helps in two ways.  First it allows
		 *	 a higher build rate for smaller packages.  Second
		 *	 it allows dsynth to ratchet-down the number of slots
		 *	 when large packages are forcing the load up.
		 *
		 *	 A high load doesn't hurt efficiency, but swap usage
		 *	 due to loading the tmpfs in lots of worker slots up
		 *	 with tons of pkg install's (pre-reqs for a build)
		 *	 does.  Reducing the number of worker slots has a
		 *	 huge beneficial effect on reducing swap use / paging.
		 */
		t = time(NULL);
		if (dynamicmax && (wblast_time == 0 ||
				   (unsigned)(t - wblast_time) >= 5)) {
			double min_load = 1.5 * NumCores;
			double max_load = 5.0 * NumCores;
			double min_swap = 0.10;
			double max_swap = 0.40;
			double dload[3];
			double dswap;
			int max1;
			int max2;
			int max3;
			int max_sel;
			int noswap;

			wblast_time = t;

			/*
			 * Cap based on load.  This is back-loaded.
			 */
			getloadavg(dload, 3);
			adjloadavg(dload);
			if (dload[0] < min_load) {
				max1 = MaxWorkers;
			} else if (dload[0] <= max_load) {
				max1 = MaxWorkers -
				       MaxWorkers * 0.75 *
				       (dload[0] - min_load) /
				       (max_load - min_load);
			} else {
				max1 = MaxWorkers * 25 / 100;
			}

			/*
			 * Cap based on swap use.  This is back-loaded.
			 */
			dswap = getswappct(&noswap);
			if (dswap < min_swap) {
				max2 = MaxWorkers;
			} else if (dswap <= max_swap) {
				max2 = MaxWorkers -
				       MaxWorkers * 0.75 *
				       (dswap - min_swap) /
				       (max_swap - min_swap);
			} else {
				max2 = MaxWorkers * 25 / 100;
			}

			/*
			 * Cap based on aggregate pkg-dependency memory
			 * use installed in worker slots.  This is
			 * front-loaded.
			 *
			 * Since it can take a while for workers to retire
			 * (to reduce RunningPkgDepSize), just set our
			 * target 1 below the current run count to allow
			 * jobs to retire without being replaced with new
			 * jobs.
			 *
			 * In addition, in order to avoid a paging 'shock',
			 * We enforce a 30 second-per-increment slow-start
			 * once RunningPkgDepSize exceeds 1/2 the target.
			 */
			if (RunningPkgDepSize > PkgDepMemoryTarget) {
				max3 = RunningWorkers - 1;
			} else if (RunningPkgDepSize > PkgDepMemoryTarget / 2) {
				if (dmlast_time == 0 ||
				    (unsigned)(t - dmlast_time) >= 30) {
					dmlast_time = t;
					max3 = RunningWorkers + 1;
				} else {
					max3 = RunningWorkers;
				}
			} else {
				max3 = MaxWorkers;
			}

			/*
			 * Dynamic scale adjustment
			 */

			if (PkgDepScaleTarget != 100) {
				max1 = max1 * (PkgDepScaleTarget + 50) / 100;
				max2 = max2 * (PkgDepScaleTarget + 50) / 100;
				max3 = max3 * (PkgDepScaleTarget + 50) / 100;
			}

			/*
			 * Priority reduction, convert to DynamicMaxWorkers
			 */
			max_sel = max1;
			if (max_sel > max2)
				max_sel = max2;
			if (max_sel > max3)
				max_sel = max3;

			/*
			 * Restrict to allowed range, and also handle
			 * slow-start.
			 */
			if (max_sel < 1)
				max_sel = 1;
			if (max_sel > DynamicMaxWorkers + 1)
				max_sel = DynamicMaxWorkers + 1;
			if (max_sel > MaxWorkers)
				max_sel = MaxWorkers;

			/*
			 * Stop waiting if DynamicMaxWorkers is going to
			 * increase.
			 */
			if (DynamicMaxWorkers < max1)
				whilematch = -1;

			/*
			 * And adjust
			 */
			if (DynamicMaxWorkers != max1) {
				dlog(DLOG_ALL | DLOG_FILTER,
				     "[XXX] Load=%-6.2f(%2d) "
				     "Swap=%-3.2f%%(%2d) "
				     "Mem=%3.2fG(%2d) "
				     "Adjust Workers %d->%d\n",
				     dload[0], max1,
				     dswap * 100.0, max2,
				     RunningPkgDepSize / (double)ONEGB, max3,
				     DynamicMaxWorkers, max_sel);
				DynamicMaxWorkers = max_sel;
			}
		}
	}
}


/*
 * Worker pthread (WorkerAry)
 *
 * This thread belongs to the dsynth master process and handles a worker slot.
 * (this_thread) -> WORKER fork/exec (WorkerPocess) -> (pty) -> sub-processes.
 */
static void *
childBuilderThread(void *arg)
{
	char *envary[1] = { NULL };
	worker_t *work = arg;
	wmsg_t wmsg;
	pkg_t *pkg;
	pid_t pid;
	int status;
	int flags;
	volatile int dowait;
	char slotbuf[8];
	char fdbuf[8];
	char flagsbuf[16];

	setNumaDomain(work->index);
	pthread_mutex_lock(&WorkerMutex);

	while (work->terminate == 0) {
		dowait = 1;

		switch(work->state) {
		case WORKER_IDLE:
			break;
		case WORKER_PENDING:
			/*
			 * Fork the management process for the pkg operation
			 * on this worker slot.
			 *
			 * This process will set up the environment, do the
			 * mounts, will become the reaper, and will process
			 * pipe commands and handle chroot operations.
			 *
			 * NOTE: If SOCK_CLOEXEC is not supported WorkerMutex
			 *	 is sufficient to interlock F_SETFD FD_CLOEXEC
			 *	 operations.
			 */
			ddassert(work->pkg);
			if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC,
				       PF_UNSPEC, work->fds)) {
				dfatal_errno("socketpair() during worker fork");
			}
			snprintf(slotbuf, sizeof(slotbuf), "%d", work->index);
			snprintf(fdbuf, sizeof(fdbuf), "3");

			/*
			 * Pass global flags and add-in the DEBUGSTOP if
			 * the package is flagged for debugging.
			 */
			flags = WorkerProcFlags;

			if (work->pkg->flags & PKGF_DEBUGSTOP)
				flags |= WORKER_PROC_DEBUGSTOP;
			else
				flags &= ~WORKER_PROC_DEBUGSTOP;

			if (PkgVersionPkgSuffix)
				flags |= WORKER_PROC_PKGV17;
			else
				flags &= ~WORKER_PROC_PKGV17;

			snprintf(flagsbuf, sizeof(flagsbuf), "%d", flags);

			/*
			 * fds[0] - master
			 * fds[1] - slave
			 *
			 * We pass the salve descriptor in fd 3 and close all
			 * other descriptors for security.
			 */
			pthread_mutex_unlock(&WorkerMutex);
			pid = vfork();
			if (pid == 0) {
				close(work->fds[0]);
				dup2(work->fds[1], 3);
				closefrom(4);
				fcntl(3, F_SETFD, 0);
				execle(DSynthExecPath, DSynthExecPath,
				       "-p", Profile,
				       "WORKER", slotbuf, fdbuf,
				       work->pkg->portdir, work->pkg->pkgfile,
				       flagsbuf,
				       NULL, envary);
				write(2, "EXECLE FAILURE\n", 15);
				_exit(1);
			}
			pthread_mutex_lock(&WorkerMutex);
			close(work->fds[1]);
			work->phase = PHASE_PENDING;
			work->lines = 0;
			work->memuse = 0;
			work->pid = pid;
			work->state = WORKER_RUNNING;
			/* fall through */
		case WORKER_RUNNING:
			/*
			 * Poll for status updates, if NULL is returned
			 * and status is non-zero, the communications link
			 * failed unexpectedly.
			 */
			pkg = work->pkg;
			pthread_mutex_unlock(&WorkerMutex);
			status = ipcreadmsg(work->fds[0], &wmsg);
			pthread_mutex_lock(&WorkerMutex);

			if (status == 0) {
				/*
				 * Normal message, can include normal
				 * termination which changes us over
				 * to another state.
				 */
				dowait = 0;
				switch(wmsg.cmd) {
				case WMSG_CMD_INSTALL_PKGS:
					wmsg.cmd = WMSG_RES_INSTALL_PKGS;
					wmsg.status = childInstallPkgDeps(work);
					pthread_mutex_unlock(&WorkerMutex);
					ipcwritemsg(work->fds[0], &wmsg);
					pthread_mutex_lock(&WorkerMutex);
					break;
				case WMSG_CMD_STATUS_UPDATE:
					work->phase = wmsg.phase;
					work->lines = wmsg.lines;
					if (work->memuse != wmsg.memuse) {
						RunningPkgDepSize +=
						wmsg.memuse - work->memuse;
						work->memuse = wmsg.memuse;
					}
					break;
				case WMSG_CMD_SUCCESS:
					work->flags |= WORKERF_SUCCESS;
					break;
				case WMSG_CMD_FAILURE:
					work->flags |= WORKERF_FAILURE;
					break;
				case WMSG_CMD_FREEZEWORKER:
					work->flags |= WORKERF_FREEZE;
					break;
				default:
					break;
				}
				RunStatsUpdate(work, NULL);
				RunStatsSync();
			} else {
				close(work->fds[0]);
				pthread_mutex_unlock(&WorkerMutex);
				while (waitpid(work->pid, &status, 0) < 0 &&
				       errno == EINTR) {
					;
				}
				pthread_mutex_lock(&WorkerMutex);

				if (work->flags & WORKERF_SUCCESS) {
					pkg->flags |= PKGF_SUCCESS;
					work->state = WORKER_DONE;
				} else if (work->flags & WORKERF_FAILURE) {
					pkg->flags |= PKGF_FAILURE;
					work->state = WORKER_DONE;
				} else {
					pkg->flags |= PKGF_FAILURE;
					work->state = WORKER_FAILED;
				}
				work->flags |= WORKERF_STATUS_UPDATE;
				pthread_cond_signal(&WorkerCond);
			}
			break;
		case WORKER_DONE:
			/*
			 * pkg remains attached until frontend processes the
			 * completion.  The frontend will then set the state
			 * back to idle.
			 */
			break;
		case WORKER_FAILED:
			/*
			 * A worker failure means that the worker did not
			 * send us a WMSG_CMD_SUCCESS or WMSG_CMD_FAILURE
			 * ipc before terminating.
			 *
			 * We just sit in this state until the front-end
			 * does something about it.
			 */
			break;
		case WORKER_FROZEN:
			/*
			 * A worker getting frozen is debug-related.  We
			 * just sit in this state (likely forever).
			 */
			break;
		default:
			dfatal("worker: [%03d] Unexpected state %d "
			       "for worker %d",
			       work->index, work->state, work->index);
			/* NOT REACHED */
			break;
		}

		/*
		 * The dsynth frontend will poll us approximately once
		 * a second (its variable).
		 */
		if (dowait)
			pthread_cond_wait(&work->cond, &WorkerMutex);
	}

	/*
	 * Scrap the comm socket if running, this should cause the worker
	 * process to kill its sub-programs and cleanup.
	 */
	if (work->state == WORKER_RUNNING) {
		pthread_mutex_unlock(&WorkerMutex);
		close(work->fds[0]);
		while (waitpid(work->pid, &status, 0) < 0 &&
		       errno == EINTR);
		pthread_mutex_lock(&WorkerMutex);
	}

	/*
	 * Final handshake
	 */
	work->state = WORKER_EXITING;
	pthread_cond_signal(&WorkerCond);
	pthread_mutex_unlock(&WorkerMutex);

	return NULL;
}

/*
 * Install all the binary packages (we have already built them) that
 * the current work package depends on, without duplicates, in a script
 * which will be run from within the specified work jail.
 *
 * Locked by WorkerMutex (global)
 */
static int
childInstallPkgDeps(worker_t *work)
{
	char *buf;
	FILE *fp;

	if (PKGLIST_EMPTY(&work->pkg->idepon_list))
		return 0;

	asprintf(&buf, "%s/tmp/dsynth_install_pkgs", work->basedir);
	fp = fopen(buf, "w");
	ddassert(fp != NULL);
	fprintf(fp, "#!/bin/sh\n");
	fprintf(fp, "#\n");
	fchmod(fileno(fp), 0755);

	childInstallPkgDeps_recurse(fp, &work->pkg->idepon_list, 0, 1, 0);
	childInstallPkgDeps_recurse(fp, &work->pkg->idepon_list, 1, 1, 0);
	fprintf(fp, "\nexit 0\n");
	fclose(fp);
	freestrp(&buf);

	return 1;
}

/*
 * Recursive child install dependencies.
 *
 * first_one_only is only specified if the pkg the list comes from
 * is a generic unflavored package that has flavors, telling us to
 * dive the first flavor only.
 *
 * However, in nearly all cases this flag will now be zero because
 * this code now dives the first flavor when encountering a dummy node
 * and clears nfirst on success.  Hence if you are asking why 'nfirst'
 * is set to 1, and then zero, instead of just being removed entirely,
 * it is because there might still be an edge case here.
 */
static size_t
childInstallPkgDeps_recurse(FILE *fp, pkglink_t *list, int undoit,
			    int depth, int first_one_only)
{
	pkglink_t *link;
	pkg_t *pkg;
	size_t tot = 0;
	int ndepth;
	int nfirst;

	PKGLIST_FOREACH(link, list) {
		pkg = link->pkg;

		/*
		 * We don't want to mess up our depth test just below if
		 * a DUMMY node had to be inserted.  The nodes under the
		 * dummy node.
		 *
		 * The elements under a dummy node represent all the flabor,
		 * a dependency that directly references a dummy node only
		 * uses the first flavor (first_one_only / nfirst).
		 */
		ndepth = (pkg->flags & PKGF_DUMMY) ? depth : depth + 1;
		nfirst = (pkg->flags & PKGF_DUMMY) ? 1 : 0;

		/*
		 * We only need all packages for the top-level dependencies.
		 * The deeper ones only need DEP_TYPE_LIB and DEP_TYPE_RUN
		 * (types greater than DEP_TYPE_BUILD) since they are already
		 * built.
		 */
		if (depth > 1 && link->dep_type <= DEP_TYPE_BUILD) {
			if (first_one_only)
				break;
			continue;
		}

		/*
		 * If this is a dummy node with no package, the originator
		 * is requesting a flavored package.  We select the default
		 * flavor which we presume is the first one.
		 */
		if (pkg->pkgfile == NULL && (pkg->flags & PKGF_DUMMY)) {
			pkg_t *spkg = pkg->idepon_list.next->pkg;

			if (spkg) {
				if (fp) {
					fprintf(fp,
						"echo 'UNFLAVORED %s -> use "
						"%s'\n",
						pkg->portdir,
						spkg->portdir);
				}
				pkg = spkg;
				nfirst = 0;
			} else {
				if (fp) {
					fprintf(fp,
						"echo 'CANNOT FIND DEFAULT "
						"FLAVOR FOR %s'\n",
						pkg->portdir);
				}
			}
		}

		if (undoit) {
			if (pkg->dsynth_install_flg == 1) {
				pkg->dsynth_install_flg = 0;
				tot += childInstallPkgDeps_recurse(fp,
							    &pkg->idepon_list,
							    undoit,
							    ndepth, nfirst);
			}
			if (first_one_only)
				break;
			continue;
		}

		if (pkg->dsynth_install_flg) {
			if (DebugOpt >= 2 && pkg->pkgfile && fp) {
				fprintf(fp, "echo 'AlreadyHave %s'\n",
					pkg->pkgfile);
			}
			if (first_one_only)
				break;
			continue;
		}

		tot += childInstallPkgDeps_recurse(fp, &pkg->idepon_list,
						   undoit, ndepth, nfirst);
		if (pkg->dsynth_install_flg) {
			if (first_one_only)
				break;
			continue;
		}
		pkg->dsynth_install_flg = 1;

		/*
		 * Generate package installation command
		 */
		if (fp && pkg->pkgfile) {
			fprintf(fp, "echo 'Installing /packages/All/%s'\n",
				pkg->pkgfile);
			fprintf(fp, "pkg install -q -U -y /packages/All/%s "
				"|| exit 1\n",
				pkg->pkgfile);
		} else if (fp) {
			fprintf(fp, "echo 'CANNOT FIND PKG FOR %s'\n",
				pkg->portdir);
		}

		if (pkg->pkgfile) {
			struct stat st;
			char *path;
			char *ptr;

			asprintf(&path, "%s/%s", RepositoryPath, pkg->pkgfile);
			ptr = strrchr(pkg->pkgfile, '.');
			if (stat(path, &st) == 0) {
				if (strcmp(ptr, ".tar") == 0)
					tot += st.st_size;
				else if (strcmp(ptr, ".tgz") == 0)
					tot += st.st_size * 3;
				else if (strcmp(ptr, ".txz") == 0)
					tot += st.st_size * 5;
				else if (strcmp(ptr, ".tzst") == 0)
					tot += st.st_size * 5;
				else if (strcmp(ptr, ".tbz") == 0)
					tot += st.st_size * 3;
				else
					tot += st.st_size * 2;
			}
			free(path);
		}
		if (first_one_only)
			break;
	}
	return tot;
}

/*
 * Worker process interactions.
 *
 * The worker process is responsible for managing the build of a single
 * package.  It is exec'd by the master dsynth and only loads the
 * configuration.
 *
 * This process does not run in the chroot.  It will become the reaper for
 * all sub-processes and it will enter the chroot to execute various phases.
 * It catches SIGINTR, SIGHUP, and SIGPIPE and will iterate, terminate, and
 * reap all sub-process upon kill or exit.
 *
 * The command line forwarded to this function is:
 *
 *	WORKER slot# socketfd portdir/subdir
 *
 * TERM=dumb
 * USER=root
 * HOME=/root
 * LANG=C
 * SSL_NO_VERIFY_PEER=1
 * USE_PACKAGE_DEPENDS_ONLY=1
 * PORTSDIR=/xports
 * PORT_DBDIR=/options		For ports options
 * PACKAGE_BUILDING=yes		Don't build packages that aren't legally
 *				buildable for a binary repo.
 * PKG_DBDIR=/var/db/pkg
 * PKG_CACHEDIR=/var/cache/pkg
 * PKG_CREATE_VERBOSE=yes	Ensure periodic output during packaging
 * (custom environment)
 * PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
 * UNAME_s=DragonFly		(example)
 * UNAME_v=DragonFly 5.7-SYNTH	(example)
 * UNAME_p=x86_64		(example)
 * UNAME_m=x86_64		(example)
 * UNAME_r=5.7-SYNTH		(example)
 * NO_DEPENDS=yes		(conditional based on phase)
 * DISTDIR=/distfiles
 * WRKDIRPREFIX=/construction
 * BATCH=yes
 * MAKE_JOBS_NUMBER=n
 *
 * SETUP:
 *	ldconfig -R
 *	/usr/local/sbin/pkg-static install /packages/All/<the pkg pkg>
 *	/usr/local/sbin/pkg-static install /packages/All/<pkg>
 *			(for all dependencies)
 *
 * PHASES: 		make -C path FLAVOR=flavor <phase>
 *	check-sanity
 *	pkg-depends
 *	fetch-depends
 *	fetch
 *	checksum
 *	extract-depends
 *	extract
 *	patch-depends
 *	patch
 *	build-depends
 *	lib-depends
 *	configure
 *	build
 *	run-depends
 *	stage
 *	test		(skipped)
 *	check-plist	('dsynth test blahblah' or 'dsynth -D everything' only)
 *	package		 e.g. /construction/lang/perl5.28/pkg/perl5-5.28.2.txz
 *	install		(skipped)
 *	deinstall	(skipped)
 */
void
WorkerProcess(int ac, char **av)
{
	wmsg_t wmsg;
	int fd;
	int slot;
	int tmpfd;
	int pkgpkg = 0;
	int status;
	int len;
	int do_install_phase;
	char *portdir;
	char *pkgfile;
	char *flavor;
	char *buf;
	worker_t *work;
	bulk_t *bulk;
	pkg_t pkg;
	buildenv_t *benv;
	FILE *fp;

	/*
	 * Parse arguments
	 */
	if (ac != 6) {
		dlog(DLOG_ALL, "WORKER PROCESS %d- bad arguments\n", getpid());
		exit(1);
	}
	slot = strtol(av[1], NULL, 0);
	setNumaDomain(slot);

	fd = strtol(av[2], NULL, 0);	/* master<->slave messaging */
	portdir = av[3];
	pkgfile = av[4];

	flavor = strchr(portdir, '@');
	if (flavor) {
		*flavor++ = 0;
		asprintf(&buf, "@%s", flavor);
		WorkerFlavorPrt = buf;
		buf = NULL;	/* safety */
	}
	WorkerProcFlags = strtol(av[5], NULL, 0);

	if (WorkerProcFlags & WORKER_PROC_FETCHONLY)
		FetchOnlyOpt = 1;
	if (WorkerProcFlags & WORKER_PROC_PKGV17)
		PkgVersionPkgSuffix = 1;

	bzero(&wmsg, sizeof(wmsg));

	setproctitle("[%02d] WORKER STARTUP  %s%s",
		     slot, portdir, WorkerFlavorPrt);

	if (strcmp(portdir, "ports-mgmt/pkg") == 0)
		pkgpkg = 1;

	signal(SIGTERM, phaseTerminateSignal);
	signal(SIGINT, phaseTerminateSignal);
	signal(SIGHUP, phaseTerminateSignal);

	/*
	 * Set up the environment
	 */
	setenv("TERM", "dumb", 1);
	setenv("USER", "root", 1);
	setenv("HOME", "/root", 1);
	setenv("LANG", "C", 1);
	setenv("SSL_NO_VERIFY_PEER", "1", 1);

	/*
	 * NOTE: PKG_SUFX - pkg versions older than 1.17
	 *	 PKG_COMPRESSION_FORMAT - pkg versions >= 1.17
	 *
	 *	 Avoid WARNING messages in the logs by omitting
	 *	 PKG_SUFX when we know the pkg version is >= 1.17.
	 */
	addbuildenv("USE_PACKAGE_DEPENDS_ONLY", "yes", BENV_MAKECONF);
	addbuildenv("PORTSDIR", "/xports", BENV_MAKECONF);
	addbuildenv("PORT_DBDIR", "/options", BENV_MAKECONF);
	addbuildenv("PKG_DBDIR", "/var/db/pkg", BENV_MAKECONF);
	addbuildenv("PKG_CACHEDIR", "/var/cache/pkg", BENV_MAKECONF);
	addbuildenv("PKG_COMPRESSION_FORMAT", UsePkgSufx, BENV_MAKECONF);
	if (PkgVersionPkgSuffix == 0)
		addbuildenv("PKG_SUFX", UsePkgSufx, BENV_MAKECONF);

	/*
	 * We are exec'ing the worker process so various bits of global
	 * state that we want to inherit have to be passed in.
	 */
	if (WorkerProcFlags & WORKER_PROC_DEVELOPER)
		addbuildenv("DEVELOPER", "1", BENV_MAKECONF);

	/*
	 * CCache is a horrible unreliable hack but... leave the
	 * mechanism in-place in case someone has a death wish.
	 */
	if (UseCCache) {
		addbuildenv("WITH_CCACHE_BUILD", "yes", BENV_MAKECONF);
		addbuildenv("CCACHE_DIR", "/ccache", BENV_MAKECONF);
	}

	addbuildenv("UID", "0", BENV_MAKECONF);
	addbuildenv("ARCH", ArchitectureName, BENV_MAKECONF);

	/*
	 * Always honor either the operating system detection or the
	 * operating system selection in the config file.
	 */
	addbuildenv("OPSYS", OperatingSystemName, BENV_MAKECONF);

#ifdef __DragonFly__
	addbuildenv("DFLYVERSION", VersionFromParamHeader, BENV_MAKECONF);
	addbuildenv("OSVERSION", "9999999", BENV_MAKECONF);
#else
#error "Need OS-specific data to generate make.conf"
#endif

	addbuildenv("OSREL", ReleaseName, BENV_MAKECONF);
	addbuildenv("_OSRELEASE", VersionOnlyName, BENV_MAKECONF);

	setenv("PATH",
	       "/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin",
	       1);

	setenv("UNAME_s", OperatingSystemName, 1);
	setenv("UNAME_v", VersionName, 1);
	setenv("UNAME_p", ArchitectureName, 1);
	setenv("UNAME_m", MachineName, 1);
	setenv("UNAME_r", ReleaseName, 1);

	addbuildenv("DISTDIR", "/distfiles", BENV_MAKECONF);
	addbuildenv("WRKDIRPREFIX", "/construction", BENV_MAKECONF);
	addbuildenv("BATCH", "yes", BENV_MAKECONF);

	/*
	 * Special consideration
	 *
	 * PACKAGE_BUILDING	- Disallow packaging ports which do not allow
	 *			  for binary distribution.
	 *
	 * PKG_CREATE_VERBOSE	- Ensure periodic output during the packaging
	 *			  process to avoid a watchdog timeout.
	 *
	 */
	addbuildenv("PACKAGE_BUILDING", "yes", BENV_MAKECONF);
	addbuildenv("PKG_CREATE_VERBOSE", "yes", BENV_MAKECONF);
	asprintf(&buf, "%d", MaxJobs);
	addbuildenv("MAKE_JOBS_NUMBER", buf, BENV_MAKECONF);
	freestrp(&buf);

	if (flavor)
		setenv("FLAVOR", flavor, 1);

	/*
	 * Become the reaper
	 */
	if (procctl(P_PID, getpid(), PROC_REAP_ACQUIRE, NULL) < 0)
		dfatal_errno("procctl() - Cannot become reaper");

	/*
	 * Initialize a worker structure
	 */
	DoInitBuild(slot);

	bzero(&pkg, sizeof(pkg));
	pkg.portdir = portdir;		/* sans flavor */
	pkg.pkgfile = pkgfile;
	if (strchr(portdir, '/'))
		len = strchr(portdir, '/') - portdir;
	else
		len = 0;

	/*
	 * Setup the logfile
	 */
	asprintf(&pkg.logfile,
		 "%s/%*.*s___%s%s%s.log",
		 LogsPath, len, len, portdir,
		 ((portdir[len] == '/') ? portdir + len + 1 : portdir + len),
		 (flavor ? "@" : ""),
		 (flavor ? flavor : ""));
	tmpfd = open(pkg.logfile, O_RDWR|O_CREAT|O_TRUNC, 0666);
	if (tmpfd >= 0) {
		if (DebugOpt >= 2) {
			dlog(DLOG_ALL, "[%03d] %s LOGFILE %s\n",
			     slot, pkg.portdir, pkg.logfile);
		}
		close(tmpfd);
	} else {
		dlog(DLOG_ALL, "[%03d] LOGFILE %s (create failed)\n",
		     slot, pkg.logfile);
	}

	/*
	 * Setup the work structure.  Because this is an exec'd sub-process,
	 * there is only one work structure.
	 */
	work = &WorkerAry[0];
	work->flavor = flavor;
	work->fds[0] = fd;
	work->pkg = &pkg;
	work->start_time = time(NULL);

	/*
	 * Do mounts
	 */
	SigWork = work;
	setproctitle("[%02d] WORKER MOUNTS   %s%s",
		     slot, portdir, WorkerFlavorPrt);
	DoWorkerMounts(work);

	/*
	 * Generate an /etc/make.conf in the build base
	 */
	asprintf(&buf, "%s/etc/make.conf", work->basedir);
	fp = fopen(buf, "w");
	dassert_errno(fp, "Unable to create %s\n", buf);
	for (benv = BuildEnv; benv; benv = benv->next) {
		if (benv->type & BENV_PKGLIST)
			continue;
		if ((benv->type & BENV_CMDMASK) == BENV_MAKECONF) {
			if (DebugOpt >= 2) {
				dlog(DLOG_ALL, "[%03d] ENV %s=%s\n",
				     slot, benv->label, benv->data);
			}
			fprintf(fp, "%s=%s\n", benv->label, benv->data);
		}
	}
	fclose(fp);
	freestrp(&buf);

	/*
	 * Set up our hooks
	 */
	if (UsingHooks)
		initbulk(childHookRun, MaxBulk);

	/*
	 * Start phases
	 */
	wmsg.cmd = WMSG_CMD_INSTALL_PKGS;
	ipcwritemsg(fd, &wmsg);
	status = ipcreadmsg(fd, &wmsg);
	if (status < 0 || wmsg.cmd != WMSG_RES_INSTALL_PKGS)
		dfatal("pkg installation handshake failed");
	do_install_phase = wmsg.status;
	if (FetchOnlyOpt)
		do_install_phase = 0;

	wmsg.cmd = WMSG_CMD_STATUS_UPDATE;
	wmsg.phase = PHASE_INSTALL_PKGS;
	wmsg.lines = 0;

	status = ipcwritemsg(fd, &wmsg);

	if (pkgpkg && FetchOnlyOpt == 0) {
		dophase(work, &wmsg,
			WDOG5, PHASE_PACKAGE, "package");
	} else {
		/*
		 * Dump as much information of the build process as possible.
		 * Will help troubleshooting port build breakages.
		 * Only enabled when DEVELOPER is set.
		 *
		 * This sort of mimics what synth did.
		 */
		if (WorkerProcFlags & WORKER_PROC_DEVELOPER) {
			dophase(work, &wmsg,
			    WDOG2, PHASE_DUMP_ENV, "Environment");
			dophase(work, &wmsg,
			    WDOG2, PHASE_SHOW_CONFIG, "showconfig");
			dophase(work, &wmsg,
			    WDOG2, PHASE_DUMP_VAR, "CONFIGURE_ENV");
			dophase(work, &wmsg,
			    WDOG2, PHASE_DUMP_VAR, "CONFIGURE_ARGS");
			dophase(work, &wmsg,
			    WDOG2, PHASE_DUMP_VAR, "MAKE_ENV");
			dophase(work, &wmsg,
			    WDOG2, PHASE_DUMP_VAR, "MAKE_ARGS");
			dophase(work, &wmsg,
			    WDOG2, PHASE_DUMP_VAR, "PLIST_SUB");
			dophase(work, &wmsg,
			    WDOG2, PHASE_DUMP_VAR, "SUB_LIST");
			dophase(work, &wmsg,
			    WDOG2, PHASE_DUMP_MAKECONF, "/etc/make.conf");
		}

		if (do_install_phase) {
			dophase(work, &wmsg,
				WDOG4, PHASE_INSTALL_PKGS, "setup");
		}
		dophase(work, &wmsg,
			WDOG2, PHASE_CHECK_SANITY, "check-sanity");
		if (FetchOnlyOpt == 0) {
			dophase(work, &wmsg,
				WDOG2, PHASE_PKG_DEPENDS, "pkg-depends");
		}
		dophase(work, &wmsg,
			WDOG7, PHASE_FETCH_DEPENDS, "fetch-depends");
		dophase(work, &wmsg,
			WDOG7, PHASE_FETCH, "fetch");
		dophase(work, &wmsg,
			WDOG2, PHASE_CHECKSUM, "checksum");
		if (FetchOnlyOpt == 0) {
			dophase(work, &wmsg,
				WDOG3, PHASE_EXTRACT_DEPENDS, "extract-depends");
			dophase(work, &wmsg,
				WDOG3, PHASE_EXTRACT, "extract");
			dophase(work, &wmsg,
				WDOG2, PHASE_PATCH_DEPENDS, "patch-depends");
			dophase(work, &wmsg,
				WDOG2, PHASE_PATCH, "patch");
			dophase(work, &wmsg,
				WDOG5, PHASE_BUILD_DEPENDS, "build-depends");
			dophase(work, &wmsg,
				WDOG5, PHASE_LIB_DEPENDS, "lib-depends");
			dophase(work, &wmsg,
				WDOG3, PHASE_CONFIGURE, "configure");
			dophase(work, &wmsg,
				WDOG9, PHASE_BUILD, "build");
			dophase(work, &wmsg,
				WDOG5, PHASE_RUN_DEPENDS, "run-depends");
			dophase(work, &wmsg,
				WDOG5, PHASE_STAGE, "stage");
#if 0
			dophase(work, &wmsg,
				WDOG5, PHASE_TEST, "test");
#endif
			if (WorkerProcFlags & WORKER_PROC_CHECK_PLIST) {
				dophase(work, &wmsg,
					WDOG1, PHASE_CHECK_PLIST, "check-plist");
			}
			dophase(work, &wmsg,
				WDOG5, PHASE_PACKAGE, "package");

			if (WorkerProcFlags & WORKER_PROC_INSTALL) {
				dophase(work, &wmsg,
				    WDOG5, PHASE_INSTALL, "install");
			}

			if (WorkerProcFlags & WORKER_PROC_DEINSTALL) {
				dophase(work, &wmsg,
				    WDOG5, PHASE_DEINSTALL, "deinstall");
			}
		}
	}

	if (MasterPtyFd >= 0) {
		close(MasterPtyFd);
		MasterPtyFd = -1;
	}

	setproctitle("[%02d] WORKER CLEANUP  %s%s",
		     slot, portdir, WorkerFlavorPrt);

	/*
	 * Copy the package to the repo.
	 */
	if (work->accum_error == 0 && FetchOnlyOpt == 0) {
		char *b1;
		char *b2;

		asprintf(&b1, "%s/construction/%s/pkg/%s",
			 work->basedir, pkg.portdir, pkg.pkgfile);
		asprintf(&b2, "%s/%s", RepositoryPath, pkg.pkgfile);
		if (copyfile(b1, b2)) {
			++work->accum_error;
			dlog(DLOG_ALL, "[%03d] %s Unable to copy %s to %s\n",
			     work->index, pkg.portdir, b1, b2);
		}
		free(b1);
		free(b2);
	}

	/*
	 * Unmount, unless we are in DebugStopMode.
	 */
	if ((WorkerProcFlags & WORKER_PROC_DEBUGSTOP) == 0)
		DoWorkerUnmounts(work);

	/*
	 * Send completion status to master dsynth worker thread.
	 */
	if (work->accum_error) {
		wmsg.cmd = WMSG_CMD_FAILURE;
	} else {
		wmsg.cmd = WMSG_CMD_SUCCESS;
	}
	ipcwritemsg(fd, &wmsg);
	if (WorkerProcFlags & WORKER_PROC_DEBUGSTOP) {
		wmsg.cmd = WMSG_CMD_FREEZEWORKER;
		ipcwritemsg(fd, &wmsg);
	}
	if (UsingHooks) {
		while ((bulk = getbulk()) != NULL)
			freebulk(bulk);
		donebulk();
	}
}

static int
check_dns(void)
{
	char check_domains[4][24] = {
		"www.dragonflybsd.org",
		"www.freebsd.org",
		"www.openbsd.org",
		"www.netbsd.org",
	};
	int failures = 0;

	for (int i = 0; i < 4; i++)
		if (gethostbyname (check_domains[i]) == NULL)
			failures++;
	if (failures > 1)
		return -1;

	return 0;
}

static void
dophase(worker_t *work, wmsg_t *wmsg, int wdog, int phaseid, const char *phase)
{
	pkg_t *pkg = work->pkg;
	char buf[1024];
	pid_t pid;
	int status;
	int ms;
	pid_t wpid;
	int wpid_reaped;
	int fdlog;
	time_t start_time;
	time_t last_time;
	time_t next_time;
	time_t wdog_time;
	FILE *fp;

	if (work->accum_error)
		return;
	setproctitle("[%02d] WORKER %-8.8s %s%s",
		     work->index, phase, pkg->portdir, WorkerFlavorPrt);
	wmsg->phase = phaseid;
	if (ipcwritemsg(work->fds[0], wmsg) < 0) {
		dlog(DLOG_ALL, "[%03d] %s Lost Communication with dsynth, "
		     "aborting worker\n",
		     work->index, pkg->portdir);
		++work->accum_error;
		return;
	}

	/*
	 * Execute the port make command in chroot on a pty.
	 */
	fflush(stdout);
	fflush(stderr);
	if (MasterPtyFd >= 0) {
		int slavefd;

		/*
		 * NOTE: We can't open the slave in the child because the
		 *	 master may race a disconnection test.  If we open
		 *	 it in the parent our close() will flush any pending
		 *	 output not read by the master (which is the same
		 *	 parent process) and deadlock.
		 *
		 *	 Solve this by hand-shaking the slave tty to give
		 *	 the master time to close its slavefd (after this
		 *	 section).
		 *
		 *	 Leave the tty defaults intact, which also likely
		 *	 means it will be in line-buffered mode, so handshake
		 *	 with a full line.
		 *
		 * TODO: Our handshake probably echos back to the master pty
		 *	 due to tty echo, and ends up in the log, so just
		 *	 pass through a newline.
		 */
		slavefd = open(ptsname(MasterPtyFd), O_RDWR);
		dassert_errno(slavefd >= 0, "Cannot open slave pty");

		/*
		 * Now do the fork.
		 */
		pid = fork();
		if (pid == 0) {
			login_tty(slavefd);
			/* login_tty() closes slavefd */
		} else {
			close(slavefd);
		}
	} else {
		/*
		 * Initial MasterPtyFd for the slot, just use forkpty().
		 */
		pid = forkpty(&MasterPtyFd, NULL, NULL, NULL);
	}

	/*
	 * The slave must make sure the master has time to close slavefd
	 * in the re-use case before going its merry way.  The master needs
	 * to set terminal modes and the window as well.
	 */
	if (pid == 0) {
		/*
		 * Slave nices itself and waits for handshake
		 */
		char ttybuf[2];

		/*
		 * Self-nice to be nice (ignore any error)
		 */
		if (NiceOpt)
			setpriority(PRIO_PROCESS, 0, NiceOpt);

		read(0, ttybuf, 1);
	} else {
		/*
		 * We are going through a pty, so set the tty modes to
		 * Set tty modes so we do not get ^M's in the log files.
		 *
		 * This isn't fatal if it doesn't work.  Remember that
		 * our output goes through the pty to the management
		 * process which will log it.
		 */
		struct termios tio;
		struct winsize win;

		if (tcgetattr(MasterPtyFd, &tio) == 0) {
			tio.c_oflag |= OPOST | ONOCR;
			tio.c_oflag &= ~(OCRNL | ONLCR);
			tio.c_iflag |= ICRNL;
			tio.c_iflag &= ~(INLCR | IGNCR);
			if (tcsetattr(MasterPtyFd, TCSANOW, &tio)) {
				printf("tcsetattr failed: %s\n",
				       strerror(errno));
			}

			/*
			 * Give the tty a non-zero columns field.
			 * This fixes at least one port (textproc/po4a)
			 */
			if (ioctl(MasterPtyFd, TIOCGWINSZ, &win) == 0) {
				win.ws_col = 80;
				ioctl(MasterPtyFd, TIOCSWINSZ, &win);
			} else {
				printf("TIOCGWINSZ failed: %s\n",
				       strerror(errno));
			}

		} else {
			printf("tcgetattr failed: %s\n", strerror(errno));
		}

		/*
		 * Master issues handshake
		 */
		write(MasterPtyFd, "\n", 1);
	}

	if (pid == 0) {
		/*
		 * Additional phase-specific environment variables
		 *
		 * - Do not try to process missing depends outside of the
		 *   depends phases.  Also relies on USE_PACKAGE_DEPENDS_ONLY
		 *   in the make.conf.
		 */
		switch(phaseid) {
		case PHASE_CHECK_SANITY:
		case PHASE_FETCH:
		case PHASE_CHECKSUM:
		case PHASE_EXTRACT:
		case PHASE_PATCH:
		case PHASE_CONFIGURE:
		case PHASE_STAGE:
		case PHASE_TEST:
		case PHASE_CHECK_PLIST:
		case PHASE_INSTALL:
		case PHASE_DEINSTALL:
			break;
		case PHASE_PKG_DEPENDS:
		case PHASE_FETCH_DEPENDS:
		case PHASE_EXTRACT_DEPENDS:
		case PHASE_PATCH_DEPENDS:
		case PHASE_BUILD_DEPENDS:
		case PHASE_LIB_DEPENDS:
		case PHASE_RUN_DEPENDS:
			break;
		default:
			setenv("NO_DEPENDS", "1", 1);
			break;
		}

		/*
		 * Clean-up, chdir, and chroot.
		 */
		closefrom(3);
		if (chdir(work->basedir) < 0)
			dfatal_errno("chdir in phase initialization");
		if (chroot(work->basedir) < 0)
			dfatal_errno("chroot in phase initialization");

		/* Explicitly fail when DNS is not working */
		if (check_dns() != 0)
			dfatal("DNS resolution not working");

		/*
		 * We have a choice here on how to handle stdin (fd 0).
		 * We can leave it connected to the pty in which case
		 * the build will just block if it tries to ask a
		 * question (and the watchdog will kill it, eventually),
		 * or we can try to EOF the pty, or we can attach /dev/null
		 * to descriptor 0.
		 */
		if (NullStdinOpt) {
			int fd;

			fd = open("/dev/null", O_RDWR);
			dassert_errno(fd >= 0, "cannot open /dev/null");
			if (fd != 0) {
				dup2(fd, 0);
				close(fd);
			}
		}

		/*
		 * Execute the appropriate command.
		 */
		switch(phaseid) {
		case PHASE_INSTALL_PKGS:
			snprintf(buf, sizeof(buf), "/tmp/dsynth_install_pkgs");
			execl(buf, buf, NULL);
			break;
		case PHASE_DUMP_ENV:
			snprintf(buf, sizeof(buf), "/usr/bin/env");
			execl(buf, buf, NULL);
			break;
		case PHASE_DUMP_VAR:
			snprintf(buf, sizeof(buf), "/xports/%s", pkg->portdir);
			execl(MAKE_BINARY, MAKE_BINARY, "-C", buf, "-V", phase,
			    NULL);
			break;
		case PHASE_DUMP_MAKECONF:
			snprintf(buf, sizeof(buf), "/bin/cat");
			execl(buf, buf, "/etc/make.conf", NULL);
			break;
		case PHASE_SHOW_CONFIG:
			/* fall-through */
		default:
			snprintf(buf, sizeof(buf), "/xports/%s", pkg->portdir);
			execl(MAKE_BINARY, MAKE_BINARY, "-C", buf, phase, NULL);
			break;
		}
		_exit(1);
	}
	fcntl(MasterPtyFd, F_SETFL, O_NONBLOCK);

	if (pid < 0) {
		dlog(DLOG_ALL, "[%03d] %s Fork Failed: %s\n",
		     work->index, pkg->logfile, strerror(errno));
		++work->accum_error;
		return;
	}

	SigPid = pid;

	fdlog = open(pkg->logfile, O_RDWR|O_CREAT|O_APPEND, 0644);
	if (fdlog < 0) {
		dlog(DLOG_ALL, "[%03d] %s Cannot open logfile '%s': %s\n",
		     work->index, pkg->portdir,
		     pkg->logfile, strerror(errno));
	}

	snprintf(buf, sizeof(buf),
		 "----------------------------------------"
		 "---------------------------------------\n"
		 "-- Phase: %s\n"
		 "----------------------------------------"
		 "---------------------------------------\n",
		 phase);
	write(fdlog, buf, strlen(buf));

	start_time = time(NULL);
	last_time = start_time;
	wdog_time = start_time;
	wpid_reaped = 0;

	status = 0;
	for (;;) {
		ms = mptylogpoll(MasterPtyFd, fdlog, wmsg, &wdog_time);
		if (ms == MPTY_FAILED) {
			dlog(DLOG_ALL,
			     "[%03d] %s lost pty in phase %s, terminating\n",
			     work->index, pkg->portdir, phase);
			break;
		}
		if (ms == MPTY_EOF)
			break;

		/*
		 * Generally speaking update status once a second.
		 * This also allows us to detect if the management
		 * dsynth process has gone away.
		 */
		next_time = time(NULL);
		if (next_time != last_time) {
			double dload[3];
			double dv;
			int wdog_scaled;

			/*
			 * Send status update to the worker management thread
			 * in the master dsynth process.  Remember, *WE* are
			 * the worker management process sub-fork.
			 */
			if (ipcwritemsg(work->fds[0], wmsg) < 0)
				break;
			last_time = next_time;

			/*
			 * Watchdog scaling
			 */
			getloadavg(dload, 3);
			adjloadavg(dload);
			dv = dload[2] / NumCores;
			if (dv < (double)NumCores) {
				wdog_scaled = wdog;
			} else {
				if (dv > 4.0 * NumCores)
					dv = 4.0 * NumCores;
				wdog_scaled = wdog * dv / NumCores;
			}

			/*
			 * Watchdog
			 */
			if (next_time - wdog_time >= wdog_scaled * 60) {
				snprintf(buf, sizeof(buf),
					 "\n--------\n"
					 "WATCHDOG TIMEOUT FOR %s in %s "
					 "after %d minutes\n"
					 "Killing pid %d\n"
					 "--------\n",
					 pkg->portdir, phase, wdog_scaled, pid);
				if (fdlog >= 0)
					write(fdlog, buf, strlen(buf));
				dlog(DLOG_ALL,
				     "[%03d] %s WATCHDOG TIMEOUT in %s "
				     "after %d minutes (%d min scaled)\n",
				     work->index, pkg->portdir, phase,
				     wdog, wdog_scaled);
				kill(pid, SIGKILL);
				++work->accum_error;
				break;
			}
		}

		/*
		 * Check process exit.  Normally the pty will EOF
		 * but if background processes remain we need to
		 * check here to see if our primary exec is done,
		 * so we can break out and reap those processes.
		 *
		 * Generally reap any other processes we have inherited
		 * while we are here.
		 */
		do {
			wpid = wait3(&status, WNOHANG, NULL);
		} while (wpid > 0 && wpid != pid);
		if (wpid == pid && WIFEXITED(status)) {
			wpid_reaped = 1;
			break;
		}
	}

	next_time = time(NULL);

	setproctitle("[%02d] WORKER EXITREAP %s%s",
		     work->index, pkg->portdir, WorkerFlavorPrt);

	/*
	 * We usually get here due to a mpty EOF, but not always as there
	 * could be persistent processes still holding the slave.  Finish
	 * up getting the exit status for the main process we are waiting
	 * on and clean out any data left on the MasterPtyFd (as it could
	 * be blocking the exit).
	 */
	while (wpid_reaped == 0) {
		(void)mptylogpoll(MasterPtyFd, fdlog, wmsg, &wdog_time);
		wpid = waitpid(pid, &status, WNOHANG);
		if (wpid == pid && WIFEXITED(status)) {
			wpid_reaped = 1;
			break;
		}
		if (wpid < 0 && errno != EINTR) {
			break;
		}

		/*
		 * Safety.  The normal phase waits until the fork/exec'd
		 * pid finishes, causing a pty EOF on exit (the slave
		 * descriptor is closed by the kernel on exit so the
		 * process should already have exited).
		 *
		 * However, it is also possible to get here if the pty fails
		 * for some reason.  In this case, make sure that the process
		 * is killed.
		 */
		kill(pid, SIGKILL);
	}

	/*
	 * Clean out anything left on the pty but don't wait around
	 * because there could be background processes preventing the
	 * slave side from closing.
	 */
	while (mptylogpoll(MasterPtyFd, fdlog, wmsg, &wdog_time) == MPTY_DATA)
		;

	/*
	 * Report on the exit condition.  If the pid was somehow lost
	 * (probably due to someone gdb'ing the process), assume an error.
	 */
	if (wpid_reaped) {
		if (WEXITSTATUS(status)) {
			dlog(DLOG_ALL | DLOG_FILTER,
			     "[%03d] %s Build phase '%s' failed exit %d\n",
			     work->index, pkg->portdir, phase,
			     WEXITSTATUS(status));
			++work->accum_error;
		}
	} else {
		dlog(DLOG_ALL, "[%03d] %s Build phase '%s' failed - lost pid\n",
		     work->index, pkg->portdir, phase);
		++work->accum_error;
	}

	/*
	 * Kill any processes still running (sometimes processes end up in
	 * the background during a dports build), and clean up any other
	 * children that we have inherited.
	 */
	phaseReapAll();

	/*
	 * After the extraction phase add the space used by /construction
	 * to the memory use.  This helps us reduce the amount of paging
	 * we do due to extremely large package extractions (languages,
	 * chromium, etc).
	 *
	 * (dsynth already estimated the space used by the package deps
	 * up front, but this will help us further).
	 */
	if (work->accum_error == 0 && phaseid == PHASE_EXTRACT) {
		struct statfs sfs;
		char *b1;

		asprintf(&b1, "%s/construction", work->basedir);
		if (statfs(b1, &sfs) == 0) {
			wmsg->memuse = (sfs.f_blocks - sfs.f_bfree) *
				       sfs.f_bsize;
			ipcwritemsg(work->fds[0], wmsg);
		}
	}

	/*
	 * Update log
	 */
	if (fdlog >= 0) {
		struct stat st;
		int h;
		int m;
		int s;

		last_time = next_time - start_time;
		s = last_time % 60;
		m = last_time / 60 % 60;
		h = last_time / 3600;

		fp = fdopen(fdlog, "a");
		if (fp == NULL) {
			dlog(DLOG_ALL, "[%03d] %s Cannot fdopen fdlog: %s %d\n",
			     work->index, pkg->portdir,
			     strerror(errno), fstat(fdlog, &st));
			close(fdlog);
			goto skip;
		}

		fprintf(fp, "\n");
		if (work->accum_error) {
			fprintf(fp, "FAILED %02d:%02d:%02d\n", h, m, s);
		} else {
			if (phaseid == PHASE_EXTRACT && wmsg->memuse) {
				fprintf(fp, "Extracted Memory Use: %6.2fM\n",
					wmsg->memuse / (1024.0 * 1024.0));
			}
			fprintf(fp, "SUCCEEDED %02d:%02d:%02d\n", h, m, s);
		}
		last_time = next_time - work->start_time;
		s = last_time % 60;
		m = last_time / 60 % 60;
		h = last_time / 3600;
		if (phaseid == PHASE_PACKAGE) {
			fprintf(fp, "TOTAL TIME %02d:%02d:%02d\n", h, m, s);
		}
		fprintf(fp, "\n");
		fclose(fp);
skip:
		;
	}

}

static void
phaseReapAll(void)
{
	struct reaper_status rs;
	int status;

	while (procctl(P_PID, getpid(), PROC_REAP_STATUS, &rs) == 0) {
		if ((rs.flags & PROC_REAP_ACQUIRE) == 0)
			break;
		if (rs.pid_head < 0)
			break;
		if (kill(rs.pid_head, SIGKILL) == 0) {
			while (waitpid(rs.pid_head, &status, 0) < 0)
				;
		}
	}
	while (wait3(&status, 0, NULL) > 0)
		;
}

static void
phaseTerminateSignal(int sig __unused)
{
	if (CopyFileFd >= 0)
		close(CopyFileFd);
	if (MasterPtyFd >= 0)
		close(MasterPtyFd);
	if (SigPid > 1)
		kill(SigPid, SIGKILL);
	phaseReapAll();
	if (SigWork)
		DoWorkerUnmounts(SigWork);
	exit(1);
}

static
char *
buildskipreason(pkglink_t *parent, pkg_t *pkg)
{
	pkglink_t *link;
	pkg_t *scan;
	char *reason = NULL;
	char *ptr;
	size_t tot;
	size_t len;
	pkglink_t stack;

	if ((pkg->flags & PKGF_NOBUILD_I) && pkg->ignore)
		asprintf(&reason, "%s ", pkg->ignore);

	tot = 0;
	PKGLIST_FOREACH(link, &pkg->idepon_list) {
#if 0
		if (link->dep_type > DEP_TYPE_BUILD)
			continue;
#endif
		scan = link->pkg;
		if (scan == NULL)
			continue;
		if ((scan->flags & (PKGF_ERROR | PKGF_NOBUILD)) == 0)
			continue;
		if (scan->flags & PKGF_NOBUILD) {
			stack.pkg = scan;
			stack.next = parent;
			ptr = buildskipreason(&stack, scan);
			len = strlen(scan->portdir) + strlen(ptr) + 8;
			reason = realloc(reason, tot + len);
			snprintf(reason + tot, len, "%s->%s",
				 scan->portdir, ptr);
			free(ptr);
		} else {
			len = strlen(scan->portdir) + 8;
			reason = realloc(reason, tot + len);
			snprintf(reason + tot, len, "%s", scan->portdir);
		}

		/*
		 * Don't try to print the entire graph
		 */
		if (parent)
			break;
		tot += strlen(reason + tot);
		reason[tot++] = ' ';
		reason[tot] = 0;
	}
	return (reason);
}

/*
 * Count number of packages that would be skipped due to the
 * specified package having failed.
 *
 * Call with mode 1 to count, and mode 0 to clear the
 * cumulative rscan flag (used to de-duplicate the count).
 *
 * Must be serialized.
 */
static int
buildskipcount_dueto(pkg_t *pkg, int mode)
{
	pkglink_t *link;
	pkg_t *scan;
	int total;

	total = 0;
	PKGLIST_FOREACH(link, &pkg->deponi_list) {
		scan = link->pkg;
		if (scan == NULL || scan->rscan == mode)
			continue;
		scan->rscan = mode;
		++total;
		total += buildskipcount_dueto(scan, mode);
	}
	return total;
}

/*
 * The master ptyfd is in non-blocking mode.  Drain up to 1024 bytes
 * and update wmsg->lines and *wdog_timep as appropriate.
 *
 * This function will poll, stalling up to 1 second.
 */
static int
mptylogpoll(int ptyfd, int fdlog, wmsg_t *wmsg, time_t *wdog_timep)
{
	struct pollfd pfd;
	char buf[1024];
	ssize_t r;

	pfd.fd = ptyfd;
	pfd.events = POLLIN;
	pfd.revents = 0;

	poll(&pfd, 1, 1000);
	if (pfd.revents) {
		r = read(ptyfd, buf, sizeof(buf));
		if (r > 0) {
			*wdog_timep = time(NULL);
			if (r > 0 && fdlog >= 0)
				write(fdlog, buf, r);
			while (--r >= 0) {
				if (buf[r] == '\n')
					++wmsg->lines;
			}
			return MPTY_DATA;
		} else if (r < 0) {
			if (errno != EINTR && errno != EAGAIN)
				return MPTY_FAILED;
			return MPTY_AGAIN;
		} else if (r == 0) {
			return MPTY_EOF;
		}
	}
	return MPTY_AGAIN;
}

/*
 * Copy a (package) file from (src) to (dst), use an intermediate file and
 * rename to ensure that interruption does not leave us with a corrupt
 * package file.
 *
 * This is called by the WORKER process.
 *
 * (dsynth management thread -> WORKER process -> sub-processes)
 */
#define COPYBLKSIZE	32768

int
copyfile(char *src, char *dst)
{
	char *tmp;
	char *buf;
	int fd1;
	int fd2;
	int error = 0;
	int mask;
	ssize_t r;

	asprintf(&tmp, "%s.new", dst);
	buf = malloc(COPYBLKSIZE);

	mask = sigsetmask(sigmask(SIGTERM)|sigmask(SIGINT)|sigmask(SIGHUP));
	fd1 = open(src, O_RDONLY|O_CLOEXEC);
	fd2 = open(tmp, O_RDWR|O_CREAT|O_TRUNC|O_CLOEXEC, 0644);
	CopyFileFd = fd1;
	sigsetmask(mask);
	while ((r = read(fd1, buf, COPYBLKSIZE)) > 0) {
		if (write(fd2, buf, r) != r)
			error = 1;
	}
	if (r < 0)
		error = 1;
	mask = sigsetmask(sigmask(SIGTERM)|sigmask(SIGINT)|sigmask(SIGHUP));
	CopyFileFd = -1;
	close(fd1);
	close(fd2);
	sigsetmask(mask);
	if (error) {
		remove(tmp);
	} else {
		if (rename(tmp, dst)) {
			error = 1;
			remove(tmp);
		}
	}

	freestrp(&buf);
	freestrp(&tmp);

	return error;
}

/*
 * doHook()
 *
 * primary process (threaded) - run_start, run_end, pkg_ignored, pkg_skipped
 * worker process  (threaded) - pkg_sucess, pkg_failure
 *
 * If waitfor is non-zero this hook will be serialized.
 */
static void
doHook(pkg_t *pkg, const char *id, const char *path, int waitfor)
{
	if (path == NULL)
		return;
	while (waitfor && getbulk() != NULL)
		;
	if (pkg)
		queuebulk(pkg->portdir, id, path, pkg->pkgfile);
	else
		queuebulk(NULL, id, path, NULL);
	while (waitfor && getbulk() != NULL)
		;
}

/*
 * Execute hook (backend)
 *
 * s1 - portdir
 * s2 - id
 * s3 - script path
 * s4 - pkgfile		(if applicable)
 */
static void
childHookRun(bulk_t *bulk)
{
	const char *cav[MAXCAC];
	buildenv_t benv[MAXCAC];
	char buf1[128];
	char buf2[128];
	char buf3[128];
	char buf4[128];
	FILE *fp;
	char *ptr;
	size_t len;
	pid_t pid;
	int cac;
	int bi;

	cac = 0;
	bi = 0;
	bzero(benv, sizeof(benv));

	cav[cac++] = bulk->s3;

	benv[bi].label = "PROFILE";
	benv[bi].data = Profile;
	++bi;

	benv[bi].label = "DIR_PACKAGES";
	benv[bi].data = PackagesPath;
	++bi;

	benv[bi].label = "DIR_REPOSITORY";
	benv[bi].data = RepositoryPath;
	++bi;

	benv[bi].label = "DIR_PORTS";
	benv[bi].data = DPortsPath;
	++bi;

	benv[bi].label = "DIR_OPTIONS";
	benv[bi].data = OptionsPath;
	++bi;

	benv[bi].label = "DIR_DISTFILES";
	benv[bi].data = DistFilesPath;
	++bi;

	benv[bi].label = "DIR_LOGS";
	benv[bi].data = LogsPath;
	++bi;

	benv[bi].label = "DIR_BUILDBASE";
	benv[bi].data = BuildBase;
	++bi;

	if (strcmp(bulk->s2, "hook_run_start") == 0) {
		snprintf(buf1, sizeof(buf1), "%d", BuildTotal);
		benv[bi].label = "PORTS_QUEUED";
		benv[bi].data = buf1;
		++bi;
	} else if (strcmp(bulk->s2, "hook_run_end") == 0) {
		snprintf(buf1, sizeof(buf1), "%d", BuildSuccessCount);
		benv[bi].label = "PORTS_BUILT";
		benv[bi].data = buf1;
		++bi;
		snprintf(buf2, sizeof(buf2), "%d", BuildFailCount);
		benv[bi].label = "PORTS_FAILED";
		benv[bi].data = buf2;
		++bi;
		snprintf(buf3, sizeof(buf3), "%d", BuildIgnoreCount);
		benv[bi].label = "PORTS_IGNORED";
		benv[bi].data = buf3;
		++bi;
		snprintf(buf4, sizeof(buf4), "%d", BuildSkipCount);
		benv[bi].label = "PORTS_SKIPPED";
		benv[bi].data = buf4;
		++bi;
	} else {
		/*
		 * success, failure, ignored, skipped
		 */
		benv[bi].label = "RESULT";
		if (strcmp(bulk->s2, "hook_pkg_success") == 0) {
			benv[bi].data = "success";
		} else if (strcmp(bulk->s2, "hook_pkg_failure") == 0) {
			benv[bi].data = "failure";
		} else if (strcmp(bulk->s2, "hook_pkg_ignored") == 0) {
			benv[bi].data = "ignored";
		} else if (strcmp(bulk->s2, "hook_pkg_skipped") == 0) {
			benv[bi].data = "skipped";
		} else {
			dfatal("Unknown hook id: %s", bulk->s2);
			/* NOT REACHED */
		}
		++bi;

		/*
		 * For compatibility with synth:
		 *
		 * ORIGIN does not include any @flavor, thus it is suitable
		 * for finding the actual port directory/subdirectory.
		 *
		 * FLAVOR is set to ORIGIN if there is no flavor, otherwise
		 * it is set to only the flavor sans the '@'.
		 */
		if ((ptr = strchr(bulk->s1, '@')) != NULL) {
			snprintf(buf1, sizeof(buf1), "%*.*s",
				 (int)(ptr - bulk->s1),
				 (int)(ptr - bulk->s1),
				 bulk->s1);
			benv[bi].label = "ORIGIN";
			benv[bi].data = buf1;
			++bi;
			benv[bi].label = "FLAVOR";
			benv[bi].data = ptr + 1;
			++bi;
		} else {
			benv[bi].label = "ORIGIN";
			benv[bi].data = bulk->s1;
			++bi;
			benv[bi].label = "FLAVOR";
			benv[bi].data = bulk->s1;
			++bi;
		}
		benv[bi].label = "PKGNAME";
		benv[bi].data = bulk->s4;
		++bi;
	}

	benv[bi].label = NULL;
	benv[bi].data = NULL;

	fp = dexec_open(bulk->s1, cav, cac, &pid, benv, 0, 0);
	while ((ptr = fgetln(fp, &len)) != NULL)
		;

	if (dexec_close(fp, pid)) {
		dlog(DLOG_ALL,
		     "[XXX] %s SCRIPT %s (%s)\n",
		     bulk->s1, bulk->s2, bulk->s3);
	}
}

/*
 * Adjusts dload[0] by adding in t_pw (processes waiting on page-fault).
 * We don't want load reductions due to e.g. thrashing to cause dsynth
 * to increase the dynamic limit because it thinks the load is low.
 *
 * This has a desirable property.  If the system pager cannot keep up
 * with process demand t_pw will spike while loadavg will only drop
 * slowly, resulting in a high adjusted load calculation that causes
 * dsynth to quickly clamp-down the limit.  If the condition alleviates,
 * the limit will then rise slowly again, possibly even before existing
 * jobs are retired to meet the clamp-down from the original spike.
 */
static void
adjloadavg(double *dload)
{
#if defined(__DragonFly__)
	struct vmtotal total;
	size_t size;

	size = sizeof(total);
	if (sysctlbyname("vm.vmtotal", &total, &size, NULL, 0) == 0) {
		dload[0] += (double)total.t_pw;
	}
#else
	dload[0] += 0.0;	/* just avoid compiler 'unused' warnings */
#endif
}

/*
 * The list of pkgs has already been flagged PKGF_PACKAGED if a pkg
 * file exists.  Check if the ports directory contents for such packages
 * has changed by comparing against a small DBM database that we maintain.
 *
 * Force-clear PKGF_PACKAGED if the ports directory content has changed.
 *
 * If no DBM database entry is present, update the entry and assume that
 * the package does not need to be rebuilt (allows the .dbm file to be
 * manually deleted without forcing a complete rebuild).
 */
static
void
check_packaged(const char *dbmpath, pkg_t *pkgs)
{
	pkg_t *scan;
	datum key;
	datum data;
	char *buf;

	if (CheckDBM == NULL) {
		dlog(DLOG_ABN, "[XXX] Unable to open/create dbm %s\n", dbmpath);
		return;
	}
	for (scan = pkgs; scan; scan = scan->bnext) {
		if ((scan->flags & PKGF_PACKAGED) == 0)
			continue;
		key.dptr = scan->portdir;
		key.dsize = strlen(scan->portdir);
		data = dbm_fetch(CheckDBM, key);
		if (data.dptr && data.dsize == sizeof(uint32_t) &&
		    *(uint32_t *)data.dptr != scan->crc32) {
			scan->flags &= ~PKGF_PACKAGED;
			asprintf(&buf, "%s/%s", RepositoryPath, scan->pkgfile);
			if (OverridePkgDeleteOpt >= 2) {
				scan->flags |= PKGF_PACKAGED;
				dlog(DLOG_ALL,
				     "[XXX] %s DELETE-PACKAGE %s "
				     "(port content changed CRC %08x/%08x "
				     "OVERRIDE, NOT DELETED)\n",
				     scan->portdir, buf,
				     *(uint32_t *)data.dptr, scan->crc32);
			} else if (remove(buf) < 0) {
				dlog(DLOG_ALL,
				     "[XXX] %s DELETE-PACKAGE %s (failed)\n",
				     scan->portdir, buf);
			} else {
				dlog(DLOG_ALL,
				     "[XXX] %s DELETE-PACKAGE %s "
				     "(port content changed CRC %08x/%08x)\n",
				     scan->portdir, buf,
				     *(uint32_t *)data.dptr, scan->crc32);
			}
			freestrp(&buf);
		} else if (data.dptr == NULL) {
			data.dptr = &scan->crc32;
			data.dsize = sizeof(scan->crc32);
			dbm_store(CheckDBM, key, data, DBM_REPLACE);
		}
	}
}
