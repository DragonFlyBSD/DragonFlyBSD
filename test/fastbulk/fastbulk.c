/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
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
 */
/*
 * fastbulk.c
 *
 * fastbulk <pkgsrcdir>
 *
 * This program iterates all pkgsrc directories, runs 'bmake show-depends-dirs'
 * recursively, and builds a dependency tree on the fly.
 *
 * As the dependency tree is being built, terminal dependencies are built
 * and packaged on the fly (well, really it runs /tmp/track/dobuild inside
 * the chroot).  As these builds complete additional dependencies may be
 * satisfied and be added to the build order.  Ultimately the entire tree
 * is built.
 *
 * Only one attempt is made to build any given package, no matter how many
 * other packages depend on it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <assert.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>

struct item;

struct depn {
	struct depn *dnext;
	struct item *item;
};

struct item {
	enum { XWAITING, XDEPFAIL, XBUILD, XRUN, XDONE } status;
	struct item *hnext;	/* ItemHash next */
	struct item *bnext;	/* BuildList/RunList next */
	struct depn *dbase;	/* packages depending on us */
	char *rpath;		/* e.g. "shells/tcsh" */
	char *lpath;		/* e.g. "shells.tcsh" (log path) */
	int dcount;		/* build completion for our dependencies */
	int xcode;		/* exit code from build */
	pid_t pid;		/* running build */
};

#define ITHSIZE	1024
#define ITHMASK	(ITHSIZE - 1)

static struct item *ItemHash[ITHSIZE];
static struct item *BuildList;
static struct item **BuildListP = &BuildList;
static struct item *RunList;

static void ordered_scan(const char *bpath, const char *path, size_t blen);
static struct item *ordered_depends(const char *bpath, const char *npath);
static struct item *lookupItem(const char *npath);
static struct item *addItem(const char *npath);
static void addDepn(struct item *item, struct item *xitem);

static void addBuild(struct item *item);
static void runBuilds(const char *bpath);
static struct item *waitRunning(int flags);
static void processCompletion(struct item *item);

static const char *neednl(void);
static void usage(void);

int NParallel = 1;
int VerboseOpt;
int NRunning;
int NeedNL;

int
main(int ac, char **av)
{
	char *bpath;
	size_t blen;
	int ch;

	while ((ch = getopt(ac, av, "j:v")) != -1) {
		switch(ch) {
		case 'j':
			NParallel = strtol(optarg, NULL, 0);
			break;
		case 'v':
			VerboseOpt = 1;
			break;
		default:
			usage();
			/* NOT REACHED */
		}
	}
	ac -= optind;
	av += optind;

	if (ac != 1) {
		fprintf(stderr, "requires base directory as first argument\n");
		exit(1);
	}

	/*
	 * Base dir
	 */
	bpath = strdup(av[0]);
	blen = strlen(bpath);
	while (blen && bpath[blen-1] == '/')
		--blen;
	bpath[blen] = 0;

	/*
	 * Do recursive directory scan
	 */
	ordered_scan(bpath, bpath, strlen(bpath));

	/*
	 * Wait for all current builds to finish running, keep the pipeline
	 * full until both the BuildList and RunList have been exhausted.
	 */
	runBuilds(bpath);
	while (waitRunning(0) != NULL)
		runBuilds(bpath);
	return(0);
}

/*
 * Recursively scan the requested directory tree looking for pkgsrc
 * stuff to build.
 */
static void
ordered_scan(const char *bpath, const char *path, size_t blen)
{
	DIR *dir;
	struct dirent *den;
	char *npath;
	char *xpath;
	struct stat st;

	if ((dir = opendir(path)) != NULL) {
		while ((den = readdir(dir)) != NULL) {
			if (den->d_name[0] == '.')
				continue;
			asprintf(&npath, "%s/%s", path, den->d_name);
			asprintf(&xpath, "%s/DESCR", npath);

			if (lookupItem(npath + blen + 1) == NULL &&
			    stat(npath, &st) == 0 && S_ISDIR(st.st_mode)) {
				if (stat(xpath, &st) == 0) {
					ordered_depends(bpath,
							npath + blen + 1);
				} else {
					ordered_scan(bpath, npath, blen);
				}
			}
			free(npath);
			free(xpath);
		}
		closedir(dir);
	}
}

/*
 * Recursively execute 'bmake show-depends-dirs' to calculate all required
 * dependencies.
 */
static struct item *
ordered_depends(const char *bpath, const char *npath)
{
	struct item *item;
	struct item *xitem;
	char buf[1024];
	FILE *fp;
	char *cmd;
	int len;

	item = addItem(npath);

	/*
	 * Retrieve and process dependencies recursively.  Note that
	 * addDepn() can modify item's status.
	 *
	 * Guard the recursion by bumping dcount to prevent the item
	 * from being processed for completion while we are still adding
	 * its dependencies.  This would normally not occur but it can
	 * if pkgsrc has a broken dependency loop.
	 */
	++item->dcount;
	asprintf(&cmd, "cd %s/%s; bmake show-depends-dirs", bpath, npath);
	fp = popen(cmd, "r");
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		len = strlen(buf);
		if (len && buf[len-1] == '\n')
			buf[--len] = 0;
		xitem = lookupItem(buf);
		if (xitem == NULL)
			xitem = ordered_depends(bpath, buf);
		addDepn(item, xitem);
	}
	pclose(fp);
	free(cmd);
	--item->dcount;

	/*
	 * If the item has no dependencies left either add it to the
	 * build list or do completion processing (i.e. if some of the
	 * dependencies failed).
	 */
	if (item->dcount == 0) {
		switch(item->status) {
		case XWAITING:
			addBuild(item);
			break;
		case XDEPFAIL:
			processCompletion(item);
			break;
		default:
			assert(0);
			/* NOT REACHED */
			break;
		}
	} else {
		if (VerboseOpt)
			printf("Deferred   %s\n", item->rpath);
	}
	runBuilds(bpath);
	return (item);
}

/*
 * Item hashing and dependency helper routines, called during the
 * directory scan.
 */
static int
itemhash(const char *npath)
{
	int hv = 0xA1B5F342;
	int i;

	for (i = 0; npath[i]; ++i)
		hv = (hv << 5) ^ (hv >> 23) ^ npath[i];
	return(hv & ITHMASK);
}

static struct item *
lookupItem(const char *npath)
{
	struct item *item;

	for (item = ItemHash[itemhash(npath)]; item; item = item->hnext) {
		if (strcmp(npath, item->rpath) == 0)
			return(item);
	}
	return(NULL);
}

static struct item *
addItem(const char *npath)
{
	struct item **itemp;
	struct item *item = calloc(sizeof(*item), 1);
	int i;

	itemp = &ItemHash[itemhash(npath)];
	item->status = XWAITING;
	item->hnext = *itemp;
	item->rpath = strdup(npath);
	item->lpath = strdup(npath);
	*itemp = item;
	for (i = 0; item->lpath[i]; ++i) {
		if (item->lpath[i] == '/')
			item->lpath[i] = '.';
	}

	return(item);
}

/*
 * Add a reverse dependency from the deepest point (xitem) to the
 * packages that depend on xitem (item in this case).
 *
 * Caller will check dcount after it is through adding dependencies.
 */
static void
addDepn(struct item *item, struct item *xitem)
{
	struct depn *depn = calloc(sizeof(*depn), 1);
	char *logpath3;
	FILE *fp;

	depn->item = item;
	depn->dnext = xitem->dbase;
	xitem->dbase = depn;
	if (xitem->status == XDONE) {
		if (xitem->xcode) {
			assert(item->status == XWAITING ||
			       item->status == XDEPFAIL);
			item->xcode = xitem->xcode;
			item->status = XDEPFAIL;
			asprintf(&logpath3,
				 "/tmp/logs/bad/%s", item->lpath);
			fp = fopen(logpath3, "a");
			fprintf(fp, "Dependency failed: %s\n",
				xitem->rpath);
			fclose(fp);
			free(logpath3);
		}
	} else {
		++item->dcount;
	}
}

/*
 * Add the item to the build request list.  This routine is called
 * after all build dependencies have been satisfied for the item.
 * runBuilds() will pick items off of BuildList to keep the parallel
 * build pipeline full.
 */
static void
addBuild(struct item *item)
{
	printf("%sBuildOrder %s\n", neednl(), item->rpath);
	*BuildListP = item;
	BuildListP = &item->bnext;
	item->status = XBUILD;
}

/*
 * Start new builds from the build list and handle build completions,
 * which can potentialy add new items to the build list.
 *
 * This routine will maintain up to NParallel builds.  A new build is
 * only started once its dependencies have completed successfully so
 * when the bulk build starts it typically takes a little while before
 * fastbulk can keep the parallel pipeline full.
 */
static void
runBuilds(const char *bpath)
{
	struct item *item;
	char *logpath;
	FILE *fp;
	int fd;

	/*
	 * Try to maintain up to NParallel builds
	 */
	while (NRunning < NParallel && BuildList) {
		item = BuildList;
		if ((BuildList = item->bnext) == NULL)
			BuildListP = &BuildList;
		printf("%sBuildStart %s\n", neednl(), item->rpath);

		/*
		 * When [re]running a build remove any bad log from prior
		 * attempts.
		 */
		asprintf(&logpath, "/tmp/logs/bad/%s", item->lpath);
		remove(logpath);
		free(logpath);

		asprintf(&logpath, "/tmp/logs/run/%s", item->lpath);

		item->status = XRUN;

		item->pid = fork();
		if (item->pid == 0) {
			/*
			 * Child process - setup the log file and exec
			 */
			if (chdir(bpath) < 0)
				_exit(99);
			if (chdir(item->rpath) < 0)
				_exit(99);

			fd = open(logpath, O_RDWR|O_CREAT|O_TRUNC, 0666);
			if (fd != 1)
				dup2(fd, 1);
			if (fd != 2)
				dup2(fd, 2);
			if (fd != 1 && fd != 2)
				close(fd);
			fd = open("/dev/null", O_RDWR);
			if (fd != 0) {
				dup2(fd, 0);
				close(fd);
			}

			/*
			 * we tack a 'clean' on to the repackage to clean
			 * the work directory on success.  If a failure
			 * occurs we leave the work directory intact.
			 *
			 * leaving work directories around when doing a
			 * bulk build can fill up the filesystem very fast.
			 */
			execl("/tmp/track/dobuild", "dobuild",
				item->lpath, NULL);
			_exit(99);
		} else if (item->pid < 0) {
			/*
			 * Parent fork() failed, log the problem and
			 * do completion processing.
			 */
			item->xcode = -98;
			fp = fopen(logpath, "a");
			fprintf(fp, "fastbulk: Unable to fork/exec bmake\n");
			fclose(fp);
			processCompletion(item);
		} else {
			/*
			 * Parent is now tracking the running child,
			 * add the item to the RunList.
			 */
			item->bnext = RunList;
			RunList = item;
			++NRunning;
		}
		free(logpath);
	}

	/*
	 * Process any completed builds (non-blocking)
	 */
	while (waitRunning(WNOHANG) != NULL)
		;
}

/*
 * Wait for a running build to finish and process its completion.
 * Return the build or NULL if no builds are pending.
 *
 * The caller should call runBuilds() in the loop to keep the build
 * pipeline full until there is nothing left in the build list.
 */
static struct item *
waitRunning(int flags)
{
	struct item *item;
	struct item **itemp;
	pid_t pid;
	int status;

	if (RunList == NULL)
		return(NULL);
	while ((pid = wait3(&status, flags, NULL)) < 0 && flags == 0)
		;

	/*
	 * NOTE! The pid may be associated with one of our popen()'s
	 *	 so just ignore it if we cannot find it.
	 */
	if (pid > 0) {
		status = WEXITSTATUS(status);
		itemp = &RunList;
		while ((item = *itemp) != NULL) {
			if (item->pid == pid)
				break;
			itemp = &item->bnext;
		}
		if (item) {
			*itemp = item->bnext;
			item->bnext = NULL;
			item->xcode = status;
			--NRunning;
			processCompletion(item);
		}
	} else {
		item = NULL;
	}
	return (item);
}

/*
 * Process the build completion for an item.
 */
static void
processCompletion(struct item *item)
{
	struct depn *depn;
	struct item *xitem;
	char *logpath1;
	char *logpath2;
	char *logpath3;
	FILE *fp;

	/*
	 * If XRUN we have to move the logfile to the correct directory.
	 * (If XDEPFAIL the logfile is already in the correct directory).
	 */
	if (item->status == XRUN) {
		asprintf(&logpath1, "/tmp/logs/run/%s", item->lpath);
		asprintf(&logpath2, "/tmp/logs/%s/%s",
			 (item->xcode ? "bad" : "good"), item->lpath);
		rename(logpath1, logpath2);
		free(logpath1);
		free(logpath2);
	}

	printf("%sFinish %-3d %s\n", neednl(), item->xcode, item->rpath);
	assert(item->status == XRUN || item->status == XDEPFAIL);
	item->status = XDONE;
	for (depn = item->dbase; depn; depn = depn->dnext) {
		xitem = depn->item;
		assert(xitem->dcount > 0);
		--xitem->dcount;
		if (xitem->status == XWAITING || xitem->status == XDEPFAIL) {
			/*
			 * If our build went well add items dependent
			 * on us to the build, otherwise fail the items
			 * dependent on us.
			 */
			if (item->xcode) {
				xitem->xcode = item->xcode;
				xitem->status = XDEPFAIL;
				asprintf(&logpath3,
					 "/tmp/logs/bad/%s", xitem->lpath);
				fp = fopen(logpath3, "a");
				fprintf(fp, "Dependency failed: %s\n",
					item->rpath);
				fclose(fp);
				free(logpath3);
			}
			if (xitem->dcount == 0) {
				if (xitem->status == XWAITING)
					addBuild(xitem);
				else
					processCompletion(xitem);
			}
		} else if (xitem->status == XDONE && xitem->xcode) {
			/*
			 * The package depending on us has already run
			 * (this case should not occur).
			 *
			 * Add this dependency failure to its log file
			 * (which has already been renamed).
			 */
			asprintf(&logpath3,
				 "/tmp/logs/bad/%s", xitem->lpath);
			fp = fopen(logpath3, "a");
			fprintf(fp, "Dependency failed: %s\n",
				item->rpath);
			fclose(fp);
			free(logpath3);
		}
	}
}

static const char *
neednl(void)
{
	if (NeedNL) {
		NeedNL = 0;
		return("\n");
	} else {
		return("");
	}
}

static void
usage(void)
{
	fprintf(stderr, "fastbulk [-j parallel] /usr/pkgsrc\n");
	exit(1);
}
