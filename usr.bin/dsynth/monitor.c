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
#include <sys/mman.h>

typedef struct MonitorFile {
	char	version[32];
	int32_t maxworkers;
	int32_t maxjobs;
	topinfo_t info;

	char	packagespath[128];
	char	repositorypath[128];
	char	optionspath[128];
	char	distfilespath[128];
	char	buildbase[128];
	char	logspath[128];
	char	systempath[128];
	char	profile[64];

	struct {
		worker_t work;
		char portdir[128];
	} slots[MAXWORKERS];
} monitor_file_t;

#define SNPRINTF(buf, ctl, ...)		\
	snprintf((buf), sizeof(buf), ctl, ## __VA_ARGS__)

static int StatsFd = -1;
static int LockFd = -1;
static monitor_file_t *RStats;

static void
MonitorInit(void)
{
	struct stat st;

	mkdir(StatsBase, 0755);
	if (stat(StatsBase, &st) < 0)
		dfatal_errno("Cannot create %s");
	if (st.st_uid && st.st_uid != getuid())
		dfatal("%s not owned by current uid", StatsBase);
	StatsFd = open(StatsFilePath, O_RDWR|O_CREAT|O_CLOEXEC, 0644);
	if (StatsFd < 0)
		dfatal_errno("Cannot create %s", StatsFilePath);
	ftruncate(StatsFd, sizeof(monitor_file_t));

	LockFd = open(StatsLockPath, O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,
		      0666);
	if (LockFd < 0)
		dfatal_errno("Cannot create %s", StatsLockPath);

	RStats = mmap(NULL, sizeof(monitor_file_t),
		      PROT_READ|PROT_WRITE, MAP_SHARED,
		      StatsFd, 0);
	dassert_errno(RStats != MAP_FAILED, "Unable to mmap %s", StatsFilePath);
}

static void
MonitorDone(void)
{
	if (RStats) {
		if (RStats != MAP_FAILED)
			munmap(RStats, sizeof(monitor_file_t));
		RStats = NULL;
	}
	if (StatsFd >= 0) {
		close(StatsFd);
		StatsFd = -1;
	}
	if (LockFd >= 0) {
		flock(LockFd, LOCK_UN);
		close(LockFd);
		LockFd = -1;
	}
}

static void
MonitorReset(void)
{
	monitor_file_t *rs;

	rs = RStats;
	flock(LockFd, LOCK_UN);			/* may fail */

	bzero(rs, sizeof(*rs));

	SNPRINTF(rs->version, "%s", DSYNTH_VERSION);
	rs->maxworkers = MaxWorkers;
	rs->maxjobs = MaxJobs;

	SNPRINTF(rs->packagespath, "%s", PackagesPath);
	SNPRINTF(rs->repositorypath, "%s", RepositoryPath);
	SNPRINTF(rs->optionspath, "%s", OptionsPath);
	SNPRINTF(rs->distfilespath, "%s", DistFilesPath);
	SNPRINTF(rs->buildbase, "%s", BuildBase);
	SNPRINTF(rs->logspath, "%s", LogsPath);
	SNPRINTF(rs->systempath, "%s", SystemPath);
	SNPRINTF(rs->profile, "%s", Profile);

	flock(LockFd, LOCK_EX);			/* ready */
}

static void
MonitorUpdate(worker_t *work, const char *dummy __unused)
{
	monitor_file_t *rs;
	worker_t copy;
	int i = work->index;

	rs = RStats;

	copy = *work;
	copy.pkg = NULL;		/* safety */
	if (work->pkg)
		SNPRINTF(rs->slots[i].portdir, "%s", work->pkg->portdir);
	else
		SNPRINTF(rs->slots[i].portdir, "%s", "");
	rs->slots[i].work = copy;
}

static void
MonitorUpdateTop(topinfo_t *info)
{
	monitor_file_t *rs;

	rs = RStats;

	rs->info = *info;
}

static void
MonitorUpdateLogs(void)
{
}

static void
MonitorSync(void)
{
}

runstats_t MonitorRunStats = {
	.init = MonitorInit,
	.done = MonitorDone,
	.reset = MonitorReset,
	.update = MonitorUpdate,
	.updateTop = MonitorUpdateTop,
	.updateLogs = MonitorUpdateLogs,
	.sync = MonitorSync
};

/****************************************************************************
 *			MONITOR DIRECTIVE FRONTEND			    *
 *		    (independently executed dsynth app)
 ****************************************************************************
 *
 * Access the monitor file and display available information
 *
 * lkfile may be NULL
 */
void
MonitorDirective(const char *datfile, const char *lkfile)
{
	monitor_file_t *rs;
	monitor_file_t copy;
	int i;
	int running;

	bzero(&copy, sizeof(copy));

	StatsFd = open(datfile, O_RDONLY);
	if (StatsFd < 0) {
		printf("dsynth is not running\n");
		exit(1);
	}
	if (lkfile) {
		LockFd = open(lkfile, O_RDWR);
		if (LockFd < 0) {
			printf("dsynth is not running\n");
			exit(1);
		}
	}

	rs = mmap(NULL, sizeof(monitor_file_t),
		  PROT_READ, MAP_SHARED,
		  StatsFd, 0);
	dassert_errno(rs != MAP_FAILED, "Cannot mmap \"%s\"", datfile);

	/*
	 * Expect flock() to fail, if it doesn't then dsynth i
	 * not running.
	 */
	if (flock(LockFd, LOCK_SH|LOCK_NB) == 0) {
		flock(LockFd, LOCK_UN);
		printf("dsynth is not running\n");
		exit(1);
	}

	/*
	 * Display setup
	 */
	NCursesRunStats.init();
	NCursesRunStats.reset();

	/*
	 * Run until done.  When we detect completion we still give the
	 * body one more chance at the logs so the final log lines are
	 * properly displayed before exiting.
	 */
	running = 1;
	while (running) {
		/*
		 * Expect flock() to fail, if it doesn't then dsynth i
		 * not running.
		 */
		if (LockFd >= 0 && flock(LockFd, LOCK_SH|LOCK_NB) == 0) {
			flock(LockFd, LOCK_UN);
			running = 0;
		}
		if (rs->version[0])
			NCursesRunStats.updateTop(&rs->info);
		for (i = 0; rs->version[0] && i < rs->maxworkers; ++i) {
			/*
			if (bcmp(&copy.slots[i].work,
				 &rs->slots[i].work,
				 sizeof(copy.slots[i].work)) != 0) {
			*/
			{
				/*
				 * A slot changed
				 */
				copy.slots[i].work = rs->slots[i].work;
				NCursesRunStats.update(&copy.slots[i].work,
						       rs->slots[i].portdir);
			}
		}
		if (LockFd >= 0)
			NCursesRunStats.updateLogs();
		NCursesRunStats.sync();
		usleep(500000);
	}
	NCursesRunStats.done();
	printf("dsynth exited\n");
}
