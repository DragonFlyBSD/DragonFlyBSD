/*
 * Copyright (c) 1998 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/usr.bin/systat/iostat.c,v 1.9.2.1 2000/07/02 10:03:17 ps Exp $
 *
 * @(#)iostat.c	8.1 (Berkeley) 6/6/93
 */
/*
 * Copyright (c) 1980, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <err.h>
#include <devstat.h>
#include <kinfo.h>
#include <paths.h>
#include <stdlib.h>
#include <string.h>
#include "systat.h"
#include "extern.h"
#include "devs.h"

struct statinfo cur, last;
static struct kinfo_cputime cp_time;

static  int linesperregion;
static  int numbers = 0;		/* default display bar graphs */
static  int kbpt = 0;			/* default ms/seek shown */

static int barlabels(int);
static void histogram(long double, int, double);
static int numlabels(int);
static int devstats(int, int, int);
static void stat1(int, uint64_t, uint64_t);

WINDOW *
openiostat(void)
{
	return (subwin(stdscr, LINES-1-5, 0, 5, 0));
}

void
closeiostat(WINDOW *w)
{
	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
	delwin(w);
}

int
initiostat(void)
{
	if ((num_devices = getnumdevs()) < 0)
		return(0);

	cur.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo));
	last.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo));
	bzero(cur.dinfo, sizeof(struct devinfo));
	bzero(last.dinfo, sizeof(struct devinfo));
	
	/*
	 * This value for maxshowdevs (100) is bogus.  I'm not sure exactly
	 * how to calculate it, though.
	 */
	if (dsinit(100, &cur, &last, NULL) != 1)
		return(0);

	return(1);
}

void
fetchiostat(void)
{
	struct devinfo *tmp_dinfo;

	if (kinfo_get_sched_cputime(&cp_time))
		err(1, "kinfo_get_sched_cputime");
	tmp_dinfo = last.dinfo;
	last.dinfo = cur.dinfo;
	cur.dinfo = tmp_dinfo;
         
	last.busy_time = cur.busy_time;

	/*
	 * Here what we want to do is refresh our device stats.
	 * getdevs() returns 1 when the device list has changed.
	 * If the device list has changed, we want to go through
	 * the selection process again, in case a device that we
	 * were previously displaying has gone away.
	 */
	switch (getdevs(&cur)) {
	case -1:
		errx(1, "%s", devstat_errbuf);
		break;
	case 1:
		cmdiostat("refresh", NULL);
		break;
	default:
		break;
	}
	num_devices = cur.dinfo->numdevs;
	generation = cur.dinfo->generation;

}

#define	INSET	10

void
labeliostat(void)
{
	int row;

	row = 0;
	wmove(wnd, row, 0); wclrtobot(wnd);
	mvwaddstr(wnd, row++, INSET,
	    "/0   /10  /20  /30  /40  /50  /60  /70  /80  /90  /100");
	mvwaddstr(wnd, row++, 0, "cpu  user|");
	mvwaddstr(wnd, row++, 0, "     nice|");
	mvwaddstr(wnd, row++, 0, "   system|");
	mvwaddstr(wnd, row++, 0, "interrupt|");
	mvwaddstr(wnd, row++, 0, "     idle|");
	if (numbers)
		row = numlabels(row + 1);
	else
		row = barlabels(row + 1);
}

static int
numlabels(int row)
{
	int i, _col, regions, ndrives;
	char tmpstr[10];

#define COLWIDTH	17
#define DRIVESPERLINE	((wnd->_maxx - INSET) / COLWIDTH)
	for (ndrives = 0, i = 0; i < num_devices; i++)
		if (dev_select[i].selected)
			ndrives++;
	regions = howmany(ndrives, DRIVESPERLINE);
	/*
	 * Deduct -regions for blank line after each scrolling region.
	 */
	linesperregion = (wnd->_maxy - row - regions) / regions;
	/*
	 * Minimum region contains space for two
	 * label lines and one line of statistics.
	 */
	if (linesperregion < 3)
		linesperregion = 3;
	_col = INSET;
	for (i = 0; i < num_devices; i++)
		if (dev_select[i].selected) {
			if (_col + COLWIDTH >= wnd->_maxx - INSET) {
				_col = INSET, row += linesperregion + 1;
				if (row > wnd->_maxy - (linesperregion + 1))
					break;
			}
			sprintf(tmpstr, "%.6s%d", dev_select[i].device_name,
				dev_select[i].unit_number);
			mvwaddstr(wnd, row, _col + 4, tmpstr);
			mvwaddstr(wnd, row + 1, _col, "  KB/t tps  MB/s ");
			_col += COLWIDTH;
		}
	if (_col)
		row += linesperregion + 1;
	return (row);
}

static int
barlabels(int row)
{
	int i;
	char tmpstr[10];

	mvwaddstr(wnd, row++, INSET,
	    "/0   /10  /20  /30  /40  /50  /60  /70  /80  /90  /100");
	linesperregion = 2 + kbpt;
	for (i = 0; i < num_devices; i++)
		if (dev_select[i].selected) {
			if (row > wnd->_maxy - linesperregion)
				break;
			sprintf(tmpstr, "%.4s%d", dev_select[i].device_name,
				dev_select[i].unit_number);
			mvwprintw(wnd, row++, 0, "%-5.5s MB/s|",
				  tmpstr);
			mvwaddstr(wnd, row++, 0, "      tps|");
			if (kbpt)
				mvwaddstr(wnd, row++, 0, "     KB/t|");
		}
	return (row);
}


void
showiostat(void)
{
	int i, row, _col;
	struct kinfo_cputime diff_cp_time;
	uint64_t cp_total;

	diff_cp_time.cp_user = cp_time.cp_user - old_cp_time.cp_user;
	diff_cp_time.cp_nice = cp_time.cp_nice - old_cp_time.cp_nice;
	diff_cp_time.cp_sys = cp_time.cp_sys - old_cp_time.cp_sys;
	diff_cp_time.cp_intr = cp_time.cp_intr - old_cp_time.cp_intr;
	diff_cp_time.cp_idle = cp_time.cp_idle - old_cp_time.cp_idle;
	old_cp_time = cp_time;

	row = 1;
	cp_total = diff_cp_time.cp_user + diff_cp_time.cp_nice +
	    diff_cp_time.cp_sys + diff_cp_time.cp_intr + diff_cp_time.cp_idle;
	stat1(row++, diff_cp_time.cp_user, cp_total);
	stat1(row++, diff_cp_time.cp_nice, cp_total);
	stat1(row++, diff_cp_time.cp_sys,  cp_total);
	stat1(row++, diff_cp_time.cp_intr, cp_total);
	stat1(row++, diff_cp_time.cp_idle, cp_total);
	if (!numbers) {
		row += 2;
		for (i = 0; i < num_devices; i++)
			if (dev_select[i].selected) {
				if (row > wnd->_maxy - linesperregion)
					break;
				row = devstats(row, INSET, i);
			}
		return;
	}
	_col = INSET;
	wmove(wnd, row + linesperregion, 0);
	wdeleteln(wnd);
	wmove(wnd, row + 3, 0);
	winsertln(wnd);
	for (i = 0; i < num_devices; i++)
		if (dev_select[i].selected) {
			if (_col + COLWIDTH >= wnd->_maxx - INSET) {
				_col = INSET, row += linesperregion + 1;
				if (row > wnd->_maxy - (linesperregion + 1))
					break;
				wmove(wnd, row + linesperregion, 0);
				wdeleteln(wnd);
				wmove(wnd, row + 3, 0);
				winsertln(wnd);
			}
			(void) devstats(row + 3, _col, i);
			_col += COLWIDTH;
		}
}

static int
devstats(int row, int _col, int dn)
{
	long double transfers_per_second;
	long double kb_per_transfer, mb_per_second;
	long double busy_seconds;
	int di;
	
	di = dev_select[dn].position;

	busy_seconds = compute_etime(cur.busy_time, last.busy_time);

	if (compute_stats(&cur.dinfo->devices[di], &last.dinfo->devices[di],
			  busy_seconds, NULL, NULL, NULL,
			  &kb_per_transfer, &transfers_per_second,
			  &mb_per_second, NULL, NULL) != 0)
		errx(1, "%s", devstat_errbuf);

	if (numbers) {
		mvwprintw(wnd, row, _col, " %5.2Lf %3.0Lf %5.2Lf ",
			 kb_per_transfer, transfers_per_second,
			 mb_per_second);
		return(row);
	}
	wmove(wnd, row++, _col);
	histogram(mb_per_second, 50, .5);
	wmove(wnd, row++, _col);
	histogram(transfers_per_second, 50, .5);
	if (kbpt) {
		wmove(wnd, row++, _col);
		histogram(kb_per_transfer, 50, .5);
	}

	return(row);

}

static void
stat1(int row, uint64_t difference, uint64_t total)
{
	double dtime;

	if (total > 0)
		dtime = 100.0 * difference / total;
	else
		dtime = 0;
	wmove(wnd, row, INSET);
#define CPUSCALE	0.5
	histogram(dtime, 50, CPUSCALE);
}

static void
histogram(long double val, int colwidth, double scale)
{
	char buf[10];
	int k;
	int v = (int)(val * scale) + 0.5;

	if (val <= 0)
		v = 0;
	k = MIN(v, colwidth);
	if (v > colwidth) {
		snprintf(buf, sizeof(buf), "%5.2Lf", val);
		k -= strlen(buf);
		while (k--)
			waddch(wnd, 'X');
		waddstr(wnd, buf);
		return;
	}
	while (k--)
		waddch(wnd, 'X');
	wclrtoeol(wnd);
}

int
cmdiostat(const char *cmd, char *args)
{
	if (prefix(cmd, "kbpt"))
		kbpt = !kbpt;
	else if (prefix(cmd, "numbers"))
		numbers = 1;
	else if (prefix(cmd, "bars"))
		numbers = 0;
	else if (!dscmd(cmd, args, 100, &cur))
		return (0);
	wclear(wnd);
	labeliostat();
	refresh();
	return (1);
}
