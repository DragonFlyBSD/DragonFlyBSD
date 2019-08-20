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

worker_t WorkerAry[MAXWORKERS];
int BuildInitialized;
int RunningWorkers;
pthread_mutex_t WorkerMutex;
pthread_cond_t WorkerCond;

static int build_find_leaves(pkg_t *parent, pkg_t *pkg, pkg_t ***build_tailp,
			int *app, int *hasworkp, int level, int first);
static void build_clear_trav(pkg_t *pkg);
static void startbuild(pkg_t **build_listp, pkg_t ***build_tailp);
static int qsort_depi(const void *pkg1, const void *pkg2);
static int qsort_idep(const void *pkg1, const void *pkg2);
static void startworker(pkg_t *pkg, worker_t *work);
static void cleanworker(worker_t *work);
static void waitbuild(int whilematch);
static void workercomplete(worker_t *work);
static void *childBuilderThread(void *arg);
static int childInstallPkgDeps(worker_t *work);
static void childInstallPkgDeps_recurse(FILE *fp, pkglink_t *list, int undoit);
static void dophase(worker_t *work, wmsg_t *wmsg,
			int wdog, int phaseid, const char *phase);
static void phaseReapAll(void);
static void phaseTerminateSignal(int sig);
static char *buildskipreason(pkg_t *pkg);
static int copyfile(char *src, char *dst);

static worker_t *SigWork;
static int FDMaster = -1;
static pid_t SigPid;
static int DynamicMaxWorkers;

int BuildTotal;
int BuildCount;
int BuildSkipCount;
int BuildFailCount;
int BuildSuccessCount;

/*
 * Initialize the WorkerAry[]
 */
void
DoInitBuild(int slot_override)
{
	worker_t *work;
	struct stat st;
	int i;

	ddassert(slot_override < 0 || MaxWorkers == 1);

	bzero(WorkerAry, MaxWorkers * sizeof(worker_t));
	pthread_mutex_init(&WorkerMutex, NULL);

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

	BuildInitialized = 1;
	DynamicMaxWorkers = MaxWorkers;
}

/*
 * Called by the frontend to clean-up any hanging mounts.
 */
void
DoCleanBuild(void)
{
	int i;

	ddassert(BuildInitialized);

	for (i = 0; i < MaxWorkers; ++i) {
		DoWorkerUnmounts(&WorkerAry[i]);
	}
	dlogreset();
}

void
DoBuild(pkg_t *pkgs)
{
	pkg_t *build_list = NULL;
	pkg_t **build_tail = &build_list;
	pkg_t *scan;
	int haswork = 1;
	int first = 1;

	for (scan = pkgs; scan; scan = scan->bnext)
		++BuildTotal;

	/*
	 * The pkg and pkg-static binaries are needed.  If already present
	 * then assume that the template is also valid, otherwise build
	 * both.
	 */
	scan = GetPkgPkg(pkgs);

	/*
	 * Create our template.  The template will be missing pkg
	 * and pkg-static.
	 */
	if ((scan->flags & (PKGF_SUCCESS | PKGF_PACKAGED)) == 0) {
		DoCreateTemplate();
	}

	/*
	 * This will clear the screen and set-up our gui, so sleep
	 * a little first in case the user wants to see what was
	 * printed before.
	 */
	sleep(2);
	pthread_mutex_lock(&WorkerMutex);
	GuiInit();
	GuiReset();

	if ((scan->flags & (PKGF_SUCCESS | PKGF_PACKAGED)) == 0) {
		char *buf;
		int rc;

		/*
		 * Build pkg/pkg-static.
		 */
		build_list = scan;
		build_tail = &scan->build_next;
		startbuild(&build_list, &build_tail);
		waitbuild(1);

		if (scan->flags & PKGF_NOBUILD)
			dfatal("Unable to build 'pkg'");
		if (scan->flags & PKGF_ERROR)
			dfatal("Error building 'pkg'");
		if ((scan->flags & PKGF_SUCCESS) == 0)
			dfatal("Error building 'pkg'");

		/*
		 * Install pkg/pkg-static into the template
		 */
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
		free(buf);
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
				build_find_leaves(NULL, scan, &build_tail,
						  &ap, &haswork, 0, first);
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
			waitbuild(RunningWorkers);
			haswork = 1;
		}
	}
	pthread_mutex_unlock(&WorkerMutex);

	ddprintf(0, "BuildCount %d\n", BuildCount);
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
		  int *app, int *hasworkp, int level, int first)
{
	pkglink_t *link;
	pkg_t *scan;
	int idep_count = 0;
	int apsub;
	char *buf;

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

		/*
		 * ERROR includes FAILURE, which is set in numerous situations
		 * including when NOBUILD state is processed.  So check for
		 * NOBUILD state first.
		 *
		 * An ERROR in a sub-package causes a NOBUILD in packages
		 * that depend on it.
		 */
		if (scan->flags & PKGF_NOBUILD) {
			ddprintf(0, "NOBUILD - OK "
				    "(propogate failure upward)\n");
			*app |= PKGF_NOBUILD_S;
			continue;
		}
		if (scan->flags & PKGF_ERROR) {
			ddprintf(0, "ERROR - OK (propogate failure upward)\n");
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
		idep_count += build_find_leaves(pkg, scan, build_tailp,
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
		asprintf(&buf, "%s/%s", RepositoryPath, pkg->pkgfile);
		if (remove(buf) < 0) {
			dlog(DLOG_ALL,
			     "[XXX] %s DELETE-PACKAGE %s (failed)\n",
			     pkg->portdir, buf);
		} else {
			dlog(DLOG_ALL,
			     "[XXX] %s DELETE-PACKAGE %s "
			     "(due to dependencies)\n",
			     pkg->portdir, buf);
		}
		free(buf);
		buf = NULL;
		*hasworkp = 1;
	}

	/*
	 * Handle propagated flags
	 */
	if (pkg->flags & PKGF_ERROR) {
		/*
		 * This can only happen if the ERROR has already been
		 * processed and accounted for.
		 */
		ddprintf(level, "} (ERROR - %s)\n", pkg->portdir);
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
		ddprintf(level, "} (SUCCESS - %s)\n", pkg->portdir);
	} else if (pkg->flags & PKGF_DUMMY) {
		ddprintf(level, "} (DUMMY/META - SUCCESS)\n");
		pkg->flags |= PKGF_SUCCESS;
		*hasworkp = 1;
		if (first) {
			dlog(DLOG_ALL, "[XXX] %s META-ALREADY-BUILT\n",
			     pkg->portdir);
			--BuildTotal;
		} else {
			dlog(DLOG_SUCC, "[XXX] %s meta-node complete\n",
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
			dlog(DLOG_ALL, "[XXX] %s ALREADY-BUILT\n",
			     pkg->portdir);
			--BuildTotal;
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
		*hasworkp = 1;
		if (pkg->flags & PKGF_NOBUILD)
			ddprintf(level, "} (ADDLIST(NOBUILD) - %s)\n",
				 pkg->portdir);
		else
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
			     (PKGF_SUCCESS |
			      PKGF_FAILURE |
			      PKGF_ERROR | PKGF_NOBUILD |
			      PKGF_RUNNING)) == 0) {
				break;
			}
			ipkg = NULL;
			++idep_index;
		}

		pkgi = NULL;
		while (depi_index < count) {
			pkgi = depi_ary[depi_index];
			if ((pkgi->flags &
			     (PKGF_SUCCESS |
			      PKGF_FAILURE |
			      PKGF_ERROR | PKGF_NOBUILD |
			      PKGF_RUNNING)) == 0) {
				break;
			}
			pkgi = NULL;
			++depi_index;
		}

		if (ipkg == NULL && pkgi == NULL)
			break;
		ddassert(ipkg && pkgi);

		/*
		 * Handle the NOBUILD case right here, there's no point
		 * queueing it anywhere.
		 */
		if (ipkg->flags & PKGF_NOBUILD) {
			char *reason;

			ipkg->flags |= PKGF_FAILURE;
			ipkg->flags &= ~PKGF_BUILDLIST;

			++BuildSkipCount;
			++BuildCount;
			reason = buildskipreason(ipkg);
			dlog(DLOG_SKIP, "[XXX] %s skipped due to%s\n",
			     ipkg->portdir, reason);
			free(reason);
			continue;
		}
		if (pkgi->flags & PKGF_NOBUILD) {
			char *reason;

			pkgi->flags |= PKGF_FAILURE;
			pkgi->flags &= ~PKGF_BUILDLIST;

			++BuildSkipCount;
			++BuildCount;
			reason = buildskipreason(pkgi);
			dlog(DLOG_SKIP, "[XXX] %s skipped due to%s\n",
			     pkgi->portdir, reason);
			free(reason);
			continue;
		}

		/*
		 * Block while no slots are available.  waitbuild()
		 * will clean out any DONE states.
		 */
		while (RunningWorkers >= DynamicMaxWorkers) {
			waitbuild(RunningWorkers);
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
				/*GuiUpdate(work);*/
				break;
			}
			++IterateWorker;
		}
		ddassert(i != MaxWorkers);
	}
	GuiSync();

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

	return (pkg2->depi_count - pkg1->depi_count);
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
		dlog(DLOG_ALL, "[%03d] %s START\n", work->index, pkg->portdir);
		cleanworker(work);
		pkg->flags |= PKGF_RUNNING;
		work->pkg = pkg;
		pthread_cond_signal(&work->cond);
		++RunningWorkers;
		/*GuiUpdate(work);*/
		break;
	case WORKER_PENDING:
	case WORKER_RUNNING:
	case WORKER_DONE:
	case WORKER_FAILED:
	case WORKER_EXITING:
	default:
		dfatal("startworker: Unexpected state %d for worker %d",
		       work->state, work->index);
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

	/*
	 * Steady state FAILED case.
	 */
	if (work->state == WORKER_FAILED) {
		if (work->pkg == NULL)
			return;
	}

	/*
	 * Process pkg out of the worker
	 */
	pkg = work->pkg;
	if (pkg->flags & (PKGF_ERROR|PKGF_NOBUILD)) {
		pkg->flags |= PKGF_FAILURE;

		/*
		 * This NOBUILD condition XXX can occur if the package is
		 * not allowed to be built.
		 */
		if (pkg->flags & PKGF_NOBUILD) {
			char *reason;

			++BuildSkipCount;
			reason = buildskipreason(pkg);
			dlog(DLOG_SKIP, "[%03d] %s skipped due to%s\n",
			     work->index, pkg->portdir, reason);
			free(reason);
		} else {
			++BuildFailCount;
			dlog(DLOG_FAIL, "[%03d] %s build-failed\n",
			     work->index, pkg->portdir);
		}
	} else {
		time_t t;
		int h;
		int m;
		int s;

		t = time(NULL) - work->start_time;
		s = t % 60;
		m = t / 60 % 60;
		h = t / 3600;

		dlog(DLOG_SUCC,
		     "[%03d] %s\tbuild-succeded in %02d:%02d:%02d\n",
		     work->index, pkg->portdir, h, m, s);
		pkg->flags |= PKGF_SUCCESS;
		++BuildSuccessCount;
	}
	++BuildCount;
	pkg->flags &= ~PKGF_BUILDLIST;
	dlog(DLOG_ALL, "[%03d] %s FINISHED (%s)\n",
	     work->index,
	     pkg->portdir,
	     ((pkg->flags & PKGF_SUCCESS) ? "success" : "failure"));
	pkg->flags &= ~PKGF_RUNNING;
	work->pkg = NULL;
	--RunningWorkers;

	if (work->state == WORKER_FAILED) {
		dlog(DLOG_ALL, "[%03d] XXX/XXX WORKER IS IN A FAILED STATE\n",
		     work->index);
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
waitbuild(int whilematch)
{
	static time_t wblast_time;
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
			GuiUpdate(work);
		}
		GuiUpdateTop();
		GuiSync();
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
		 */
		t = time(NULL);
		if (wblast_time == 0 || wblast_time != t) {
			double min_range = 1.5 * NumCores;
			double max_range = 5.0 * NumCores;
			double dload[3];

			wblast_time = t;
			getloadavg(dload, 3);
			if (dload[0] >= min_range) {
				int reduce;

				if (dload[0] <= max_range) {
					reduce = 75 * (dload[0] - min_range) /
						 (max_range - min_range);
				} else {
					reduce = 75;	/* 75% */
				}
				reduce = MaxWorkers * reduce / 100;
				reduce = MaxWorkers - reduce;
				if (reduce < 4)
					reduce = 4;
				if (reduce > MaxWorkers)
					reduce = MaxWorkers;
				if (DynamicMaxWorkers != reduce) {
					dlog(DLOG_ALL,
					     "[XXX] Load %6.2f "
					     "Adjust MaxWorkers "
					     "%d->%d\n",
					     dload[0],
					     DynamicMaxWorkers,
					     reduce);
					DynamicMaxWorkers = reduce;
				}
			} else if (DynamicMaxWorkers != MaxWorkers) {
				dlog(DLOG_ALL,
				     "[XXX] Load %6.2f Adjust MaxWorkers "
				     "%d->%d\n",
				     dload[0], DynamicMaxWorkers, MaxWorkers);
				DynamicMaxWorkers = MaxWorkers;
			}
		}
	}
}


/*
 * Worker pthread (WorkerAry)
 *
 * This thread belongs to the dsynth master process and handled a worker slot.
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
	volatile int dowait;
	char slotbuf[8];
	char fdbuf[8];

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
				       "WORKER", slotbuf, fdbuf,
				       work->pkg->portdir, work->pkg->pkgfile,
				       NULL, envary);
				write(2, "EXECLE FAILURE\n", 15);
				_exit(1);
			}
			pthread_mutex_lock(&WorkerMutex);
			close(work->fds[1]);
			work->phase = PHASE_PENDING;
			work->lines = 0;
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
					break;
				case WMSG_CMD_SUCCESS:
					work->flags |= WORKERF_SUCCESS;
					break;
				case WMSG_CMD_FAILURE:
					work->flags |= WORKERF_FAILURE;
					break;
				default:
					break;
				}
				GuiUpdate(work);
				GuiSync();
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
		default:
			dfatal("worker: Unexpected state %d for worker %d",
			       work->state, work->index);
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

	childInstallPkgDeps_recurse(fp, &work->pkg->idepon_list, 0);
	childInstallPkgDeps_recurse(fp, &work->pkg->idepon_list, 1);
	fprintf(fp, "\nexit 0\n");
	fclose(fp);

	return 1;
}

static void
childInstallPkgDeps_recurse(FILE *fp, pkglink_t *list, int undoit)
{
	pkglink_t *link;
	pkg_t *pkg;

	PKGLIST_FOREACH(link, list) {
		pkg = link->pkg;

		if (undoit) {
			if (pkg->dsynth_install_flg == 1) {
				pkg->dsynth_install_flg = 0;
				childInstallPkgDeps_recurse(fp,
							    &pkg->idepon_list,
							    undoit);
			}
			continue;
		}
		if (pkg->dsynth_install_flg) {
			if (DebugOpt && pkg->pkgfile) {
				fprintf(fp, "echo 'AlreadyHave %s'\n",
					pkg->pkgfile);
			}
			continue;
		}

		childInstallPkgDeps_recurse(fp, &pkg->idepon_list, undoit);
		if (pkg->dsynth_install_flg)
			continue;
		pkg->dsynth_install_flg = 1;

		/*
		 * If this is a dummy node with no package, the originator
		 * is requesting a flavored package.  We select the default
		 * flavor which we presume is the first one.
		 */
		if (pkg->pkgfile == NULL && (pkg->flags & PKGF_DUMMY)) {
			pkg_t *spkg = pkg->idepon_list.next->pkg;

			if (spkg) {
				pkg = spkg;
				fprintf(fp,
					"echo 'DUMMY use %s (%p)'\n",
					pkg->portdir, pkg->pkgfile);
			} else {
				fprintf(fp,
					"echo 'CANNOT FIND DEFAULT FLAVOR "
					"FOR %s'\n",
					pkg->portdir);
			}
		}

		/*
		 * Generate package installation command
		 */
		if (pkg->pkgfile) {
			fprintf(fp, "echo 'Installing /packages/All/%s'\n",
				pkg->pkgfile);
			fprintf(fp, "pkg install -q -y /packages/All/%s "
				"|| exit 1\n",
				pkg->pkgfile);
		} else {
			fprintf(fp, "echo 'CANNOT FIND PKG FOR %s'\n",
				pkg->portdir);
		}
	}
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
 * USE_PACKAGE_DEPENDS_ONLY=1	(exec_phase_depends)
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
 * NO_DEPENDS=yes		(exec_phase)
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
 *	test
 *	check-plist
 *	package		 e.g. /construction/lang/perl5.28/pkg/perl5-5.28.2.txz
 *	install-mtree
 *	install
 *	deinstall
 */
void
WorkerProcess(int ac, char **av)
{
	wmsg_t wmsg;
	int fd;
	int slot;
	int tmpfd;
	int pkgpkg = 0;
	int status __unused;
	int len;
	int do_install_phase;
	char *portdir;
	char *pkgfile;
	char *flavor;
	char *buf;
	worker_t *work;
	pkg_t pkg;
	buildenv_t *benv;

	/*
	 * Parse arguments
	 */
	if (ac != 5) {
		dlog(DLOG_ALL, "WORKER PROCESS %d- bad arguments\n", getpid());
		exit(1);
	}
	slot = strtol(av[1], NULL, 0);
	fd = strtol(av[2], NULL, 0);	/* master<->slave messaging */
	portdir = av[3];
	pkgfile = av[4];
	flavor = strchr(portdir, '@');
	if (flavor)
		*flavor++ = 0;
	bzero(&wmsg, sizeof(wmsg));

	setproctitle("WORKER [%02d] STARTUP  %s", slot, portdir);

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
	setenv("USE_PACKAGE_DEPENDS_ONLY", "1", 1);
	setenv("PORTSDIR", "/xports", 1);
	setenv("PORT_DBDIR", "/options", 1);
	setenv("PKG_DBDIR", "/var/db/pkg", 1);
	setenv("PKG_CACHEDIR", "/var/cache/pkg", 1);

	setenv("PATH",
	       "/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin",
	       1);

	setenv("UNAME_s", OperatingSystemName, 1);
	setenv("UNAME_v", VersionName, 1);
	setenv("UNAME_p", ArchitectureName, 1);
	setenv("UNAME_m", MachineName, 1);
	setenv("UNAME_r", ReleaseName, 1);

	setenv("NO_DEPENDS", "yes", 1);
	setenv("DISTDIR", "/distfiles", 1);
	setenv("WRKDIRPREFIX", "/construction", 1);
	setenv("BATCH", "yes", 1);

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
	setenv("PACKAGE_BUILDING", "yes", 1);
	setenv("PKG_CREATE_VERBOSE", "yes", 1);

	asprintf(&buf, "%d", MaxJobs);
	setenv("MAKE_JOBS_NUMBER", buf, 1);
	free(buf);

	if (flavor)
		setenv("FLAVOR", flavor, 1);

	/*
	 * Load environment from profile
	 */
	for (benv = BuildEnv; benv; benv = benv->next) {
		if (DebugOpt >= 2) {
			dlog(DLOG_ALL, "[%03d] ENV %s=%s\n",
			     slot, benv->label, benv->data);
		}
		setenv(benv->label, benv->data, 1);
	}

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
		dlog(DLOG_ALL, "[%03d] %s LOGFILE %s\n",
		     slot, pkg.portdir, pkg.logfile);
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
	 * Do mounts and start the phases
	 */
	SigWork = work;
	setproctitle("WORKER [%02d] MOUNTS   %s", slot, portdir);
	DoWorkerMounts(work);

	wmsg.cmd = WMSG_CMD_INSTALL_PKGS;
	ipcwritemsg(fd, &wmsg);
	status = ipcreadmsg(fd, &wmsg);
	if (status < 0 || wmsg.cmd != WMSG_RES_INSTALL_PKGS)
		dfatal("pkg installation handshake failed");
	do_install_phase = wmsg.status;

	wmsg.cmd = WMSG_CMD_STATUS_UPDATE;
	wmsg.phase = PHASE_INSTALL_PKGS;
	wmsg.lines = 0;

	status = ipcwritemsg(fd, &wmsg);

	if (pkgpkg) {
		dophase(work, &wmsg,
			WDOG5, PHASE_PACKAGE, "package");
	} else {
		if (do_install_phase) {
			dophase(work, &wmsg,
				WDOG4, PHASE_INSTALL_PKGS, "setup");
		}
		dophase(work, &wmsg,
			WDOG2, PHASE_CHECK_SANITY, "check-sanity");
		dophase(work, &wmsg,
			WDOG2, PHASE_PKG_DEPENDS, "pkg-depends");
		dophase(work, &wmsg,
			WDOG7, PHASE_FETCH_DEPENDS, "fetch-depends");
		dophase(work, &wmsg,
			WDOG7, PHASE_FETCH, "fetch");
		dophase(work, &wmsg,
			WDOG2, PHASE_CHECKSUM, "checksum");
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
		dophase(work, &wmsg,
			WDOG1, PHASE_CHECK_PLIST, "check-plist");
		dophase(work, &wmsg,
			WDOG5, PHASE_PACKAGE, "package");
#if 0
		dophase(work, &wmsg,
			WDOG5, PHASE_INSTALL_MTREE, "install-mtree");
		dophase(work, &wmsg,
			WDOG5, PHASE_INSTALL, "install");
		dophase(work, &wmsg,
			WDOG5, PHASE_DEINSTALL, "deinstall");
#endif
	}

	setproctitle("WORKER [%02d] CLEANUP  %s", slot, portdir);

	/*
	 * Copy the package to the repo.
	 */
	if (work->accum_error == 0) {
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

	DoWorkerUnmounts(work);

	if (work->accum_error) {
		wmsg.cmd = WMSG_CMD_FAILURE;
	} else {
		wmsg.cmd = WMSG_CMD_SUCCESS;
	}

	status = ipcwritemsg(fd, &wmsg);
}

static void
dophase(worker_t *work, wmsg_t *wmsg, int wdog, int phaseid, const char *phase)
{
	pkg_t *pkg = work->pkg;
	char buf[1024];
	pid_t pid;
	ssize_t r;
	int status;
	int fdmaster;
	int fdlog;
	time_t start_time;
	time_t last_time;
	time_t next_time;
	time_t wdog_time;
	FILE *fp;

	if (work->accum_error)
		return;
	setproctitle("WORKER [%02d] %8.8s %s",
		     work->index, phase, pkg->portdir);
	wmsg->phase = phaseid;
	if (ipcwritemsg(work->fds[0], wmsg) < 0) {
		dlog(DLOG_ALL, "[%03d] %s Lost Communication with dsynth, "
		     "aborting worker\n",
		     work->index, pkg->portdir);
		++work->accum_error;
		return;
	}

	/*
	 * Execute the port make command in chroot on a pty
	 */
	fflush(stdout);
	fflush(stderr);
	if ((pid = forkpty(&fdmaster, NULL, NULL, NULL)) == 0) {
		closefrom(3);
		if (chdir(work->basedir) < 0)
			dfatal_errno("chdir in phase initialization");
		if (chroot(work->basedir) < 0)
			dfatal_errno("chroot in phase initialization");

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
		default:
			snprintf(buf, sizeof(buf), "/xports/%s", pkg->portdir);
			execl(MAKE_BINARY, MAKE_BINARY, "-C", buf, phase, NULL);
			break;
		}
		_exit(1);
	}

	FDMaster = fdmaster;	/* for signal handler */
	SigPid = pid;

	fdlog = open(pkg->logfile, O_RDWR|O_CREAT|O_APPEND, 0644);
	if (fdlog < 0) {
		dlog(DLOG_ALL, "[%03d] %s Cannot open logfile '%s': %s\n",
		     work->index, pkg->portdir,
		     pkg->logfile, strerror(errno));
	}
	start_time = time(NULL);
	last_time = start_time;
	wdog_time = start_time;

	status = 0;
	for (;;) {
		struct pollfd pfd;
		pid_t wpid;

		pfd.fd = fdmaster;
		pfd.events = POLLIN;
		pfd.revents = 0;

		poll(&pfd, 1, 1000);
		if (pfd.revents) {
			r = read(fdmaster, buf, sizeof(buf));
			if (r <= 0) {
				if (r < 0 && errno == EINTR)
					continue;
				break;
			}
			wdog_time = time(NULL);
			if (r > 0 && fdlog >= 0)
				write(fdlog, buf, r);
			while (--r >= 0) {
				if (buf[r] == '\n')
					++wmsg->lines;
			}
		}

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
				     "[%03d] WATCHDOG TIMEOUT FOR %s in %s "
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
		if (wpid == pid && WIFEXITED(status))
			break;
	}
	close(fdmaster);

	next_time = time(NULL);

	setproctitle("WORKER [%02d] EXITREAP %s",
		     work->index, pkg->portdir);
	kill(pid, SIGKILL);
	while (WIFEXITED(status) == 0) {
		if (waitpid(pid, &status, 0) < 0) {
			if (errno != EINTR)
				break;
		}

	}

	if (WEXITSTATUS(status)) {
		dlog(DLOG_ALL, "[%03d] %s Build phase '%s' failed\n",
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

		fprintf(fp, "--------\n");
		if (work->accum_error) {
			fprintf(fp, "PHASE %s FAILED %02d:%02d:%02d\n",
				phase, h, m, s);
		} else {
			fprintf(fp, "PHASE %s SUCCEEDED %02d:%02d:%02d\n",
				phase, h, m, s);
		}
		last_time = next_time - work->start_time;
		s = last_time % 60;
		m = last_time / 60 % 60;
		h = last_time / 3600;
		if (phaseid == PHASE_PACKAGE) {
			fprintf(fp, "TOTAL TIME %02d:%02d:%02d\n", h, m, s);
		}
		fprintf(fp, "--------\n");
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
	if (FDMaster >= 0)
		close(FDMaster);
	if (SigPid > 1)
		kill(SigPid, SIGKILL);
	phaseReapAll();
	if (SigWork)
		DoWorkerUnmounts(SigWork);
	exit(1);
}

static
char *
buildskipreason(pkg_t *pkg)
{
	pkglink_t *link;
	pkg_t *scan;
	char *reason = NULL;
	size_t tot;
	size_t len;

	tot = 0;
	PKGLIST_FOREACH(link, &pkg->idepon_list) {
		scan = link->pkg;
		if (scan == NULL)
			continue;
		if ((scan->flags & (PKGF_ERROR | PKGF_NOBUILD)) == 0)
			continue;
		len = strlen(scan->portdir);
		reason = realloc(reason, tot + len + 8);
		snprintf(reason + tot, len + 8, " %s", scan->portdir);
		tot += strlen(reason + tot);
	}
	return (reason);
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

static int
copyfile(char *src, char *dst)
{
	char *tmp;
	char *buf;
	int fd1;
	int fd2;
	int error = 0;
	ssize_t r;

	asprintf(&tmp, "%s.new", dst);
	buf = malloc(COPYBLKSIZE);

	fd1 = open(src, O_RDONLY|O_CLOEXEC);
	fd2 = open(tmp, O_RDWR|O_CREAT|O_TRUNC|O_CLOEXEC, 0644);
	while ((r = read(fd1, buf, COPYBLKSIZE)) > 0) {
		if (write(fd2, buf, r) != r)
			error = 1;
	}
	close(fd1);
	close(fd2);
	if (error) {
		remove(tmp);
	} else {
		if (rename(tmp, dst)) {
			error = 1;
			remove(tmp);
		}
	}

	free(buf);
	free(tmp);

	return error;
}
