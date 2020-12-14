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

static runstats_t *RSBase;
static runstats_t **RSTailp = &RSBase;
static time_t RSStartTime;

#define RHISTSIZE       600		/* impulse record is 10 minutes */
#define ONEHOUR		(60 * 60)

void
RunStatsInit(void)
{
	runstats_t *rs;

	RSStartTime = time(NULL);

	*RSTailp = &NCursesRunStats;
	RSTailp = &(*RSTailp)->next;

	*RSTailp = &MonitorRunStats;
	RSTailp = &(*RSTailp)->next;

	*RSTailp = &HtmlRunStats;
	RSTailp = &(*RSTailp)->next;

	for (rs = RSBase; rs; rs = rs->next)
		rs->init();
}

void
RunStatsDone(void)
{
	runstats_t *rs;

	for (rs = RSBase; rs; rs = rs->next)
		rs->done();
}

void
RunStatsReset(void)
{
	runstats_t *rs;

	for (rs = RSBase; rs; rs = rs->next)
		rs->reset();
}

void
RunStatsUpdate(worker_t *work, const char *portdir)
{
	runstats_t *rs;

	for (rs = RSBase; rs; rs = rs->next)
		rs->update(work, portdir);
}

void
RunStatsUpdateTop(int active)
{
	static int rate_history[RHISTSIZE];
	static u_int last_ti;
	topinfo_t info;
	runstats_t *rs;
	u_int ti;
	time_t t;

	/*
	 * Time
	 */
	bzero(&info, sizeof(info));
	t = time(NULL) - RSStartTime;
	info.s = t % 60;
	info.m = t / 60 % 60;
	info.h = t / 60 / 60;
	info.active = active;

	/*
	 * Easy fields
	 */
	info.total = BuildTotal;
	info.successful = BuildSuccessCount;
	info.ignored = BuildIgnoreCount;
	info.remaining = BuildTotal - BuildCount;
	info.failed = BuildFailCount;
	info.skipped = BuildSkipCount;
	info.meta = BuildMetaCount;

	/*
	 * Load and swap
	 */
	getloadavg(info.dload, 3);
	info.dswap = getswappct(&info.noswap) * 100.0;

	/*
	 * Rate and 10-minute impulse
	 */
	if (t > 20)
		info.pkgrate = (BuildSuccessCount + BuildFailCount) *
			       ONEHOUR / t;
	else
		info.pkgrate = 0;
	ti = (u_int)((unsigned long)t % RHISTSIZE);
	rate_history[ti] = BuildSuccessCount + BuildFailCount;
#if 0
	dlog(DLOG_ALL, "ti[%3d] = %d\n", ti, rate_history[ti]);
#endif
	while (last_ti != ti) {
		rate_history[last_ti] = rate_history[ti];
		last_ti = (last_ti + 1) % RHISTSIZE;
	}

	if (t < 20) {
		info.pkgimpulse = 0;
	} else if (t < RHISTSIZE) {
		info.pkgimpulse = rate_history[ti] -
				  rate_history[(ti - t) % RHISTSIZE];
		info.pkgimpulse = info.pkgimpulse * ONEHOUR / t;
	} else {
		info.pkgimpulse = rate_history[ti] -
				  rate_history[(ti + 1) % RHISTSIZE];
		info.pkgimpulse = info.pkgimpulse * ONEHOUR / RHISTSIZE;
#if 0
		dlog(DLOG_ALL, "pkgimpulse %d - %d -> %d\n",
		     rate_history[ti],
		     rate_history[(ti + 1) % RHISTSIZE],
		     info.pkgimpulse);
#endif
	}

	info.dynmaxworkers = DynamicMaxWorkers;

	/*
	 * Issue update
	 */
	for (rs = RSBase; rs; rs = rs->next)
		rs->updateTop(&info);
}

void
RunStatsUpdateLogs(void)
{
	runstats_t *rs;

	for (rs = RSBase; rs; rs = rs->next)
		rs->updateLogs();
}

void
RunStatsSync(void)
{
	runstats_t *rs;

	for (rs = RSBase; rs; rs = rs->next)
		rs->sync();
}

void
RunStatsUpdateCompletion(worker_t *work, int logid, pkg_t *pkg,
			 const char *reason, const char *skipbuf)
{
	runstats_t *rs;

	for (rs = RSBase; rs; rs = rs->next) {
		if (rs->updateCompletion)
			rs->updateCompletion(work, logid, pkg, reason, skipbuf);
	}
}
