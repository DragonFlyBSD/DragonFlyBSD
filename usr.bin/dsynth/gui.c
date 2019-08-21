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

#include <curses.h>

/*
 * ncurses - LINES, COLS are the main things we care about
 */
static WINDOW *CWin;
static const char *Line0 = " Total -       Built -      Ignored -      "
			   "Load -      Pkg/hour -               ";
static const char *Line1 = "  Left -      Failed -      Skipped -      "
			   "Swap -       Impulse -      --:--:-- ";
static const char *LineB = "==========================================="
			   "====================================";
static const char *LineI = " ID  Duration  Build Phase      Origin     "
			   "                               Lines";

static time_t GuiStartTime;
static int LastReduce;

#define TOTAL_COL	7
#define BUILT_COL	21
#define IGNORED_COL	36
#define LOAD_COL	48
#define GPKGRATE_COL	64
#define REDUCE_COL	71

#define LEFT_COL	7
#define FAILED_COL	21
#define SKIPPED_COL	36
#define SWAP_COL	48
#define IMPULSE_COL	64
#define TIME_COL	71

#define ID_COL		1
#define DURATION_COL	5
#define BUILD_PHASE_COL	15
#define ORIGIN_COL	32
#define LINES_COL	73

void
GuiInit(void)
{
	CWin = initscr();
	GuiReset();
	GuiStartTime = time(NULL);
}

void
GuiReset(void)
{
	int i;

	werase(CWin);
	curs_set(0);
	redrawwin(CWin);
	wrefresh(CWin);
	mvwprintw(CWin, 0, 0, "%s", Line0);
	mvwprintw(CWin, 1, 0, "%s", Line1);
	mvwprintw(CWin, 2, 0, "%s", LineB);
	mvwprintw(CWin, 3, 0, "%s", LineI);
	mvwprintw(CWin, 4, 0, "%s", LineB);

	for (i = 0; i < MaxWorkers; ++i) {
		mvwprintw(CWin, 5 + i, ID_COL, "%02d", i);
		mvwprintw(CWin, 5 + i, DURATION_COL, "--:--:--");
		mvwprintw(CWin, 5 + i, BUILD_PHASE_COL, "Idle");
		mvwprintw(CWin, 5 + i, ORIGIN_COL, "%39.39s", "");
		mvwprintw(CWin, 5 + i, LINES_COL, "%6.6s", "");
	}
	wrefresh(CWin);
	LastReduce = -1;
}

#define RHISTSIZE	600	/* 10 minutes */
#define ONEHOUR		(60 * 60)

void
GuiUpdateTop(void)
{
	static int rate_history[RHISTSIZE];
	static u_int last_ti;
	u_int ti;
	int h;
	int m;
	int s;
	int pkgrate;
	int pkgimpulse;
	double dload[3];
	double dswap;
	int noswap;
	time_t t;

	/*
	 * Time
	 */

	t = time(NULL) - GuiStartTime;
	s = t % 60;
	m = t / 60 % 60;
	h = t / 60 / 60;

	/*
	 * Load and swap
	 */
	getloadavg(dload, 3);
	dswap = getswappct(&noswap) * 100.0;

	/*
	 * Rate and 10-minute impulse
	 */
	if (t > 20)
		pkgrate = (BuildSuccessCount + BuildFailCount) * ONEHOUR / t;
	else
		pkgrate = 0;
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
		pkgimpulse = 0;
	} else if (t < RHISTSIZE) {
		pkgimpulse = rate_history[ti] -
			     rate_history[(ti - t) % RHISTSIZE];
		pkgimpulse = pkgimpulse * ONEHOUR / t;
	} else {
		pkgimpulse = rate_history[ti] -
			     rate_history[(ti + 1) % RHISTSIZE];
		pkgimpulse = pkgimpulse * ONEHOUR / RHISTSIZE;
#if 0
		dlog(DLOG_ALL, "pkgimpulse %d - %d -> %d\n",
		     rate_history[ti],
		     rate_history[(ti + 1) % RHISTSIZE],
		     pkgimpulse);
#endif
	}

	mvwprintw(CWin, 0, TOTAL_COL, "%-5d", BuildTotal);
	mvwprintw(CWin, 0, BUILT_COL, "%-5d", BuildSuccessCount);
	mvwprintw(CWin, 0, IGNORED_COL, "%-5d", -1);
	if (dload[0] > 999.9)
		mvwprintw(CWin, 0, LOAD_COL, "%5.0f", dload[0]);
	else
		mvwprintw(CWin, 0, LOAD_COL, "%5.1f", dload[0]);
	mvwprintw(CWin, 0, GPKGRATE_COL, "%-5d", pkgrate);

	/*
	 * If dynamic worker reduction is active include a field,
	 * Otherwise blank the field.
	 */
	if (LastReduce != DynamicMaxWorkers) {
		LastReduce = DynamicMaxWorkers;
		if (MaxWorkers == LastReduce)
			mvwprintw(CWin, 0, REDUCE_COL, "        ");
		else
			mvwprintw(CWin, 0, REDUCE_COL, "Limit %-2d",
				  LastReduce);
	}

	mvwprintw(CWin, 1, LEFT_COL, "%-4d", BuildTotal - BuildCount);
	mvwprintw(CWin, 1, FAILED_COL, "%-4d", BuildFailCount);
	mvwprintw(CWin, 1, SKIPPED_COL, "%-4d", BuildSkipCount);
	if (noswap)
		mvwprintw(CWin, 1, SWAP_COL, "-   ");
	else
		mvwprintw(CWin, 1, SWAP_COL, "%5.1f", dswap);
	mvwprintw(CWin, 1, IMPULSE_COL, "%-5d", pkgimpulse);
	if (h > 99)
		mvwprintw(CWin, 1, TIME_COL-1, "%3d:%02d:%02d", h, m, s);
	else
		mvwprintw(CWin, 1, TIME_COL, "%02d:%02d:%02d", h, m, s);
}

void
GuiUpdate(worker_t *work)
{
	const char *phase;
	const char *origin;
	time_t t;
	int i = work->index;
	int h;
	int m;
	int s;

	phase = "Unknown";
	origin = "";

	switch(work->state) {
	case WORKER_NONE:
		phase = "None";
		/* fall through */
	case WORKER_IDLE:
		if (work->state == WORKER_IDLE)
			phase = "Idle";
		/* fall through */
	case WORKER_FAILED:
		if (work->state == WORKER_FAILED)
			phase = "Failed";
		/* fall through */
	case WORKER_EXITING:
		if (work->state == WORKER_EXITING)
			phase = "Exiting";
		mvwprintw(CWin, 5 + i, DURATION_COL, "--:--:--");
		mvwprintw(CWin, 5 + i, BUILD_PHASE_COL, "%-16.16s", phase);
		mvwprintw(CWin, 5 + i, ORIGIN_COL, "%-39.39s", "");
		mvwprintw(CWin, 5 + i, LINES_COL, "%-6.6s", "");
		return;
	case WORKER_PENDING:
		phase = "Pending";
		break;
	case WORKER_RUNNING:
		phase = "Running";
		break;
	case WORKER_DONE:
		phase = "Done";
		break;
	default:
		break;
	}

	t = time(NULL) - work->start_time;
	s = t % 60;
	m = t / 60 % 60;
	h = t / 60 / 60;

	if (work->state == WORKER_RUNNING) {
		switch(work->phase) {
		case PHASE_PENDING:
			phase = "pending";
			break;
		case PHASE_INSTALL_PKGS:
			phase = "install-pkgs";
			break;
		case PHASE_CHECK_SANITY:
			phase = "check-sanity";
			break;
		case PHASE_PKG_DEPENDS:
			phase = "pkg-depends";
			break;
		case PHASE_FETCH_DEPENDS:
			phase = "fetch-depends";
			break;
		case PHASE_FETCH:
			phase = "fetch";
			break;
		case PHASE_CHECKSUM:
			phase = "checksum";
			break;
		case PHASE_EXTRACT_DEPENDS:
			phase = "extract-depends";
			break;
		case PHASE_EXTRACT:
			phase = "extract";
			break;
		case PHASE_PATCH_DEPENDS:
			phase = "patch-depends";
			break;
		case PHASE_PATCH:
			phase = "patch";
			break;
		case PHASE_BUILD_DEPENDS:
			phase = "build-depends";
			break;
		case PHASE_LIB_DEPENDS:
			phase = "lib-depends";
			break;
		case PHASE_CONFIGURE:
			phase = "configure";
			break;
		case PHASE_BUILD:
			phase = "build";
			break;
		case PHASE_RUN_DEPENDS:
			phase = "run-depends";
			break;
		case PHASE_STAGE:
			phase = "stage";
			break;
		case PHASE_TEST:
			phase = "test";
			break;
		case PHASE_CHECK_PLIST:
			phase = "check-plist";
			break;
		case PHASE_PACKAGE:
			phase = "package";
			break;
		case PHASE_INSTALL_MTREE:
			phase = "install-mtree";
			break;
		case PHASE_INSTALL:
			phase = "install";
			break;
		case PHASE_DEINSTALL:
			phase = "deinstall";
			break;
		default:
			phase = "Run-Unknown";
			break;
		}
	}

	if (work->pkg)
		origin = work->pkg->portdir;
	else
		origin = "";

	mvwprintw(CWin, 5 + i, DURATION_COL, "%02d:%02d:%02d", h, m, s);
	mvwprintw(CWin, 5 + i, BUILD_PHASE_COL, "%-16.16s", phase);
	mvwprintw(CWin, 5 + i, ORIGIN_COL, "%-39.39s", origin);
	mvwprintw(CWin, 5 + i, LINES_COL, "%6d", work->lines);
}

void
GuiSync(void)
{
	wrefresh(CWin);
}

void
GuiDone(void)
{
	endwin();
}
