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
static WINDOW *CMon;
static const char *Line0 = " Total -       Built -      Ignored -      "
			   "Load -      Pkg/hour -               ";
static const char *Line1 = "  Left -      Failed -      Skipped -      "
			   "Swap -       Impulse -      --:--:-- ";
static const char *LineB = "==========================================="
			   "====================================";
static const char *LineI = " ID  Duration  Build Phase      Origin     "
			   "                               Lines";

static int LastReduce;
static monitorlog_t nclog;

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
#define LINES_COL	72

/*
 * The row that the worker list starts on, and the row that the log starts
 * on.
 */
#define WORKER_START	5
#define LOG_START	(WORKER_START + MaxWorkers + 1)

static void NCursesReset(void);

static void
NCursesInit(void)
{
	if (UseNCurses == 0)
		return;

	CWin = initscr();
	NCursesReset();

	intrflush(stdscr, FALSE);
	nonl();
	noecho();
	cbreak();

	start_color();
	use_default_colors();
	init_pair(1, COLOR_RED, -1);
	init_pair(2, COLOR_GREEN, -1);
	init_pair(3, -1, -1);
}

static void
NCursesReset(void)
{
	int i;

	if (UseNCurses == 0)
		return;

	if (CMon) {
		delwin(CMon);
		CMon = NULL;
	}

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
		mvwprintw(CWin, WORKER_START + i, ID_COL, "%02d", i);
		mvwprintw(CWin, WORKER_START + i, DURATION_COL, "--:--:--");
		mvwprintw(CWin, WORKER_START + i, BUILD_PHASE_COL, "Idle");
		mvwprintw(CWin, WORKER_START + i, ORIGIN_COL, "%38.38s", "");
		mvwprintw(CWin, WORKER_START + i, LINES_COL, "%7.7s", "");
	}
	mvwprintw(CWin, WORKER_START + MaxWorkers, 0, "%s", LineB);
	wrefresh(CWin);

	CMon = subwin(CWin, 0, 0, LOG_START, 0);
	scrollok(CMon, 1);

	bzero(&nclog, sizeof(nclog));
	nclog.fd = dlog00_fd();
	nodelay(CMon, 1);

	LastReduce = -1;
}

static void
NCursesUpdateTop(topinfo_t *info)
{
	if (UseNCurses == 0)
		return;

	mvwprintw(CWin, 0, TOTAL_COL, "%-6d", info->total);
	mvwprintw(CWin, 0, BUILT_COL, "%-6d", info->successful);
	mvwprintw(CWin, 0, IGNORED_COL, "%-6d", info->ignored);
	if (info->dload[0] > 999.9)
		mvwprintw(CWin, 0, LOAD_COL, "%5.0f", info->dload[0]);
	else
		mvwprintw(CWin, 0, LOAD_COL, "%5.1f", info->dload[0]);
	mvwprintw(CWin, 0, GPKGRATE_COL, "%-6d", info->pkgrate);

	/*
	 * If dynamic worker reduction is active include a field,
	 * Otherwise blank the field.
	 */
	if (LastReduce != info->dynmaxworkers) {
		LastReduce = info->dynmaxworkers;
		if (MaxWorkers == LastReduce)
			mvwprintw(CWin, 0, REDUCE_COL, "       ");
		else
			mvwprintw(CWin, 0, REDUCE_COL, "Lim %-3d",
				  LastReduce);
	}

	mvwprintw(CWin, 1, LEFT_COL, "%-6d", info->remaining);
	mvwprintw(CWin, 1, FAILED_COL, "%-6d", info->failed);
	mvwprintw(CWin, 1, SKIPPED_COL, "%-6d", info->skipped);
	if (info->noswap)
		mvwprintw(CWin, 1, SWAP_COL, "-   ");
	else
		mvwprintw(CWin, 1, SWAP_COL, "%5.1f", info->dswap);
	mvwprintw(CWin, 1, IMPULSE_COL, "%-6d", info->pkgimpulse);
	if (info->h > 99)
		mvwprintw(CWin, 1, TIME_COL-1, "%3d:%02d:%02d",
			  info->h, info->m, info->s);
	else
		mvwprintw(CWin, 1, TIME_COL, "%02d:%02d:%02d",
			  info->h, info->m, info->s);
}

static void
NCursesUpdateLogs(void)
{
	char *ptr;
	char c;
	ssize_t n;
	int w;

	if (UseNCurses == 0)
		return;

	for (;;) {
		n = readlogline(&nclog, &ptr);
		if (n < 0)
			break;
		if (n == 0)
			continue;

		/*
		 * Scroll down
		 */
		if (n > COLS)
			w = COLS;
		else
			w = n;
		c = ptr[w];
		ptr[w] = 0;

		/*
		 * Filter out these logs from the display (they remain in
		 * the 00*.log file) to reduce clutter.
		 */
		if (strncmp(ptr, "[XXX] Load=", 11) != 0) {
			/*
			 * Output possibly colored log line
			 */
			wscrl(CMon, -1);
			if (strstr(ptr, "] SUCCESS ")) {
				wattrset(CMon, COLOR_PAIR(2));
			} else if (strstr(ptr, "] FAILURE ")) {
				wattrset(CMon, COLOR_PAIR(1));
			}
			mvwprintw(CMon, 0, 0, "%s", ptr);
			wattrset(CMon, COLOR_PAIR(3));
		}
		ptr[w] = c;
	}
}

static void
NCursesUpdate(worker_t *work, const char *portdir)
{
	const char *phase;
	const char *origin;
	time_t t;
	int i = work->index;
	int h;
	int m;
	int s;

	if (UseNCurses == 0)
		return;

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
		mvwprintw(CWin, WORKER_START + i, DURATION_COL,
			  "--:--:--");
		mvwprintw(CWin, WORKER_START + i, BUILD_PHASE_COL,
			  "%-16.16s", phase);
		mvwprintw(CWin, WORKER_START + i, ORIGIN_COL,
			  "%-38.38s", "");
		mvwprintw(CWin, WORKER_START + i, LINES_COL,
			  "%-7.7s", "");
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
	case WORKER_FROZEN:
		phase = "FROZEN";
		break;
	default:
		break;
	}

	t = time(NULL) - work->start_time;
	s = t % 60;
	m = t / 60 % 60;
	h = t / 60 / 60;

	if (work->state == WORKER_RUNNING)
		phase = getphasestr(work->phase);

	/*
	 * When called from the monitor frontend portdir has to be passed
	 * in directly because work->pkg is not mapped.
	 */
	if (portdir)
		origin = portdir;
	else if (work->pkg)
		origin = work->pkg->portdir;
	else
		origin = "";

	mvwprintw(CWin, WORKER_START + i, DURATION_COL,
		  "%02d:%02d:%02d", h, m, s);
	mvwprintw(CWin, WORKER_START + i, BUILD_PHASE_COL,
		  "%-16.16s", phase);
	mvwprintw(CWin, WORKER_START + i, ORIGIN_COL,
		  "%-38.38s", origin);
	if (work->lines > 9999999) {
		mvwprintw(CWin, WORKER_START + i, LINES_COL,
			  "%7s", "*MANY*%d", work->lines % 10);
	} else {
		mvwprintw(CWin, WORKER_START + i, LINES_COL,
			  "%7d", work->lines);
	}
}

static void
NCursesSync(void)
{
	int c;

	if (UseNCurses == 0)
		return;

	while ((c = wgetch(CMon)) != ERR) {
		if (c == KEY_RESIZE)
			NCursesReset();
	}
	wrefresh(CWin);
	wrefresh(CMon);
}

static void
NCursesDone(void)
{
	if (UseNCurses == 0)
		return;

	endwin();
}

runstats_t NCursesRunStats = {
	.init = NCursesInit,
	.done = NCursesDone,
	.reset = NCursesReset,
	.update = NCursesUpdate,
	.updateTop = NCursesUpdateTop,
	.updateLogs = NCursesUpdateLogs,
	.sync = NCursesSync
};
