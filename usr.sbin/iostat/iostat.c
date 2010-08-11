/*
 * Copyright (c) 1997, 1998  Kenneth D. Merry.
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
 * $FreeBSD: src/usr.sbin/iostat/iostat.c,v 1.17.2.2 2001/07/19 04:15:42 kris Exp $
 * $DragonFly: src/usr.sbin/iostat/iostat.c,v 1.7 2005/09/01 19:08:38 swildner Exp $
 */
/*
 * Parts of this program are derived from the original FreeBSD iostat
 * program:
 */
/*-
 * Copyright (c) 1986, 1991, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
/*
 * Ideas for the new iostat statistics output modes taken from the NetBSD
 * version of iostat:
 */
/*
 * Copyright (c) 1996 John M. Vinopal
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project
 *      by John M. Vinopal.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/user.h>
#include <sys/param.h>
#include <sys/errno.h>

#include <err.h>
#include <ctype.h>
#include <fcntl.h>
#include <kinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <devstat.h>

struct statinfo cur, last;
uint64_t tk_nin, old_tk_nin, diff_tk_nin;
uint64_t tk_nout, old_tk_nout, diff_tk_nout;
struct kinfo_cputime cp_time, old_cp_time, diff_cp_time;
double cp_time_total;
int num_devices;
struct device_selection *dev_select;
int maxshowdevs;
int dflag = 0, Dflag=0, Iflag = 0, Cflag = 0, Tflag = 0, oflag = 0, Kflag = 0;

/* local function declarations */
static void usage(void);
static void phdr(int signo);
static void devstats(int perf_select);
static void cpustats(void);

static void
usage(void)
{
	/*
	 * We also support the following 'traditional' syntax:
	 * iostat [drives] [wait [count]]
	 * This isn't mentioned in the man page, or the usage statement,
	 * but it is supported.
	 */
	fprintf(stderr, "usage: iostat [-CdhIKoT] [-c count]"
		" [-n devs]\n"
		"\t      [-t type,if,pass] [-w wait] [drives]\n");
}

int
main(int argc, char **argv)
{
	int c;
	int tflag = 0, hflag = 0, cflag = 0, wflag = 0, nflag = 0;
	int count = 0, waittime = 0;
	struct devstat_match *matches;
	int num_matches = 0;
	int hz;
	int headercount;
	long generation;
	int num_devices_specified;
	int num_selected, num_selections;
	long select_generation;
	char **specified_devices;
	devstat_select_mode select_mode;

	matches = NULL;
	maxshowdevs = 3;

	while ((c = getopt(argc, argv, "c:CdDhIKM:n:N:ot:Tw:")) != -1) {
		switch(c) {
			case 'c':
				cflag++;
				count = atoi(optarg);
				if (count < 1)
					errx(1, "count %d is < 1", count);
				break;
			case 'C':
				Cflag++;
				break;
			case 'd':
				dflag++;
				break;
			case 'D':
				Dflag++;
				break;
			case 'h':
				hflag++;
				break;
			case 'I':
				Iflag++;
				break;
			case 'K':
				Kflag++;
				break;
			case 'n':
				nflag++;
				maxshowdevs = atoi(optarg);
				if (maxshowdevs < 0)
					errx(1, "number of devices %d is < 0",
					     maxshowdevs);
				break;
			case 'o':
				oflag++;
				break;
			case 't':
				tflag++;
				if (buildmatch(optarg, &matches, 
					       &num_matches) != 0)
					errx(1, "%s", devstat_errbuf);
				break;
			case 'T':
				Tflag++;
				break;
			case 'w':
				wflag++;
				waittime = atoi(optarg);
				if (waittime < 1)
					errx(1, "wait time is < 1");
				break;
			default:
				usage();
				exit(1);
				break;
		}
	}

	argc -= optind;
	argv += optind;

	/*
	 * Make sure that the userland devstat version matches the kernel
	 * devstat version.  If not, exit and print a message informing 
	 * the user of his mistake.
	 */
	if (checkversion() < 0)
		errx(1, "%s", devstat_errbuf);

	/*
	 * Figure out how many devices we should display.
	 */
	if (nflag == 0) {
		if (oflag > 0) {
			if ((dflag > 0) && (Cflag == 0) && (Tflag == 0))
				maxshowdevs = 5;
			else if ((dflag > 0) && (Tflag > 0) && (Cflag == 0))
				maxshowdevs = 5;
			else
				maxshowdevs = 4;
		} else {
			if ((dflag > 0) && (Cflag == 0))
				maxshowdevs = 4;		
			else
				maxshowdevs = 3;
		}
	}

	/* find out how many devices we have */
	if ((num_devices = getnumdevs()) < 0)
		err(1, "can't get number of devices");

	if ((cur.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo))) ==
	     NULL)
		err(1, "devinfo malloc failed");
	if ((last.dinfo = (struct devinfo *)malloc(sizeof(struct devinfo))) ==
	     NULL)
		err(1, "devinfo malloc failed");
	bzero(cur.dinfo, sizeof(struct devinfo));
	bzero(last.dinfo, sizeof(struct devinfo));

	/*
	 * Grab all the devices.  We don't look to see if the list has
	 * changed here, since it almost certainly has.  We only look for
	 * errors.
	 */
	if (getdevs(&cur) == -1)
		errx(1, "%s", devstat_errbuf);

	num_devices = cur.dinfo->numdevs;
	generation = cur.dinfo->generation;

	/*
	 * If the user specified any devices on the command line, see if
	 * they are in the list of devices we have now.
	 */
	if ((specified_devices = (char **)malloc(sizeof(char *))) == NULL)
		err(1, "specified_devices malloc failed");
	for (num_devices_specified = 0; *argv; ++argv) {
		if (isdigit(**argv))
			break;
		num_devices_specified++;
		specified_devices = (char **)realloc(specified_devices,
						     sizeof(char *) *
						     num_devices_specified);
		specified_devices[num_devices_specified - 1] = *argv;

	}
	if (nflag == 0 && maxshowdevs < num_devices_specified)
		maxshowdevs = num_devices_specified;

	dev_select = NULL;

	if ((num_devices_specified == 0) && (num_matches == 0))
		select_mode = DS_SELECT_ADD;
	else
		select_mode = DS_SELECT_ONLY;

	/*
	 * At this point, selectdevs will almost surely indicate that the
	 * device list has changed, so we don't look for return values of 0
	 * or 1.  If we get back -1, though, there is an error.
	 */
	if (selectdevs(&dev_select, &num_selected,
		       &num_selections, &select_generation,
		       generation, cur.dinfo->devices, num_devices,
		       matches, num_matches,
		       specified_devices, num_devices_specified,
		       select_mode, maxshowdevs, hflag) == -1)
		errx(1, "%s", devstat_errbuf);

	/*
	 * Look for the traditional wait time and count arguments.
	 */
	if (*argv) {
		waittime = atoi(*argv);

		/* Let the user know he goofed, but keep going anyway */
		if (wflag != 0) 
			warnx("discarding previous wait interval, using"
			      " %d instead", waittime);
		wflag++;

		if (*++argv) {
			count = atoi(*argv);
			if (cflag != 0)
				warnx("discarding previous count, using %d"
				      " instead", count);
			cflag++;
		} else
			count = -1;
	}

	/*
	 * If the user specified a count, but not an interval, we default
	 * to an interval of 1 second.
	 */
	if ((wflag == 0) && (cflag > 0))
		waittime = 1;

	/*
	 * If the user specified a wait time, but not a count, we want to
	 * go on ad infinitum.  This can be redundant if the user uses the
	 * traditional method of specifying the wait, since in that case we
	 * already set count = -1 above.  Oh well.
	 */
	if ((wflag > 0) && (cflag == 0))
		count = -1;

	if (kinfo_get_sched_hz(&hz))
		err(1, "kinfo_get_sched_hz");
	if (kinfo_get_sched_stathz(&hz))
		err(1, "kinfo_get_sched_stathz");

	/*
	 * If the user stops the program (control-Z) and then resumes it,
	 * print out the header again.
	 */
	signal(SIGCONT, phdr);

	for (headercount = 1;;) {
		struct devinfo *tmp_dinfo;

		if (!--headercount) {
			phdr(0);
			headercount = 20;
		}
		if (kinfo_get_tty_tk_nin(&tk_nin))
			err(1, "kinfo_get_tty_tk_nin");
		if (kinfo_get_tty_tk_nout(&tk_nout))
			err(1, "kinfo_get_tty_tk_nout");
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
		case 1: {
			int retval;

			num_devices = cur.dinfo->numdevs;
			generation = cur.dinfo->generation;
			retval = selectdevs(&dev_select, &num_selected,
					    &num_selections, &select_generation,
					    generation, cur.dinfo->devices,
					    num_devices, matches, num_matches,
					    specified_devices,
					    num_devices_specified,
					    select_mode, maxshowdevs, hflag);
			switch(retval) {
			case -1:
				errx(1, "%s", devstat_errbuf);
				break;
			case 1:
				phdr(0);
				headercount = 20;
				break;
			default:
				break;
			}
			break;
		}
		default:
			break;
		}

		/*
		 * We only want to re-select devices if we're in 'top'
		 * mode.  This is the only mode where the devices selected
		 * could actually change.
		 */
		if (hflag > 0) {
			int retval;
			retval = selectdevs(&dev_select, &num_selected,
					    &num_selections, &select_generation,
					    generation, cur.dinfo->devices,
					    num_devices, matches, num_matches,
					    specified_devices,
					    num_devices_specified,
					    select_mode, maxshowdevs, hflag);
			switch(retval) {
			case -1:
				errx(1,"%s", devstat_errbuf);
				break;
			case 1:
				phdr(0);
				headercount = 20;
				break;
			default:
				break;
			}
		}

		diff_tk_nin = tk_nin - old_tk_nin;
		old_tk_nin = tk_nin;
		diff_tk_nout = tk_nout - old_tk_nout;
		old_tk_nout = tk_nout;

		diff_cp_time.cp_user = cp_time.cp_user - old_cp_time.cp_user;
		diff_cp_time.cp_nice = cp_time.cp_nice - old_cp_time.cp_nice;
		diff_cp_time.cp_sys = cp_time.cp_sys - old_cp_time.cp_sys;
		diff_cp_time.cp_intr = cp_time.cp_intr - old_cp_time.cp_intr;
		diff_cp_time.cp_idle = cp_time.cp_idle - old_cp_time.cp_idle;
		cp_time_total = diff_cp_time.cp_user + diff_cp_time.cp_nice +
		    diff_cp_time.cp_sys + diff_cp_time.cp_intr +
		    diff_cp_time.cp_idle;
		old_cp_time = cp_time;

		if (cp_time_total == 0.0)
			cp_time_total = 1.0;

		if ((dflag == 0) || (Tflag > 0))
			printf("%4.0f%5.0f", diff_tk_nin / cp_time_total * 1e6, 
				diff_tk_nout / cp_time_total * 1e6);
		devstats(hflag);
		if ((dflag == 0) || (Cflag > 0))
			cpustats();
		printf("\n");
		fflush(stdout);

		if (count >= 0 && --count <= 0)
			break;

		sleep(waittime);
	}

	exit(0);
}

static void
phdr(__unused int signo)
{
	int i;
	int printed;

	if ((dflag == 0) || (Tflag > 0))
		printf("      tty");
	for (i = 0, printed=0;(i < num_devices) && (printed < maxshowdevs);i++){
		int di;
		if ((dev_select[i].selected != 0)
		 && (dev_select[i].selected <= maxshowdevs)) {
			di = dev_select[i].position;
			if (oflag > 0)
				printf("%12.6s%d ", 
					    cur.dinfo->devices[di].device_name,
					    cur.dinfo->devices[di].unit_number);
			else
				if (Dflag > 0)
					printf("%19.6s%d            ",
						    cur.dinfo->devices[di].device_name,
						    cur.dinfo->devices[di].unit_number);
				else
					printf("%15.6s%d ",
						    cur.dinfo->devices[di].device_name,
						    cur.dinfo->devices[di].unit_number);
			printed++;
		}
	}
	if ((dflag == 0) || (Cflag > 0))
		printf("            cpu\n");
	else
		printf("\n");

	if ((dflag == 0) || (Tflag > 0))
		printf(" tin tout");

	for (i=0, printed = 0;(i < num_devices) && (printed < maxshowdevs);i++){
		if ((dev_select[i].selected != 0)
		 && (dev_select[i].selected <= maxshowdevs)) {
			if (oflag > 0) {
				if (Iflag == 0)
					printf(" sps tps  msps ");
				else
					printf(" blk xfr msps ");
			} else {
				if (Iflag == 0) {
					if (Dflag > 0)
						printf("   KB/t rtps  MBr/s wtps  MBw/s ");
					else
						printf("  KB/t tps   MB/s ");
				}
				else
					printf("  KB/t xfrs   MB ");
			}
			printed++;
		}
	}
	if ((dflag == 0) || (Cflag > 0))
		printf(" us ni sy in id\n");
	else
		printf("\n");

}

static void
devstats(int perf_select)
{
	int dn;
	long double kb_per_transfer;
	long double transfers_per_second;
	long double transfers_per_secondr, transfers_per_secondw;
	long double mb_per_second;
	long double mb_per_secondr, mb_per_secondw;
	u_int64_t total_bytes, total_transfers, total_blocks;
	long double busy_seconds;
	long double total_mb;
	long double blocks_per_second, ms_per_transaction;
	
	/*
	 * Calculate elapsed time up front, since it's the same for all
	 * devices.
	 */
	busy_seconds = compute_etime(cur.busy_time, last.busy_time);

	for (dn = 0; dn < num_devices; dn++) {
		int di;

		if (((perf_select == 0) && (dev_select[dn].selected == 0))
		 || (dev_select[dn].selected > maxshowdevs))
			continue;

		di = dev_select[dn].position;

		if (compute_stats(&cur.dinfo->devices[di],
				  &last.dinfo->devices[di], busy_seconds,
				  &total_bytes, &total_transfers,
				  &total_blocks, &kb_per_transfer,
				  &transfers_per_second, &mb_per_second,
				  &blocks_per_second, &ms_per_transaction)!= 0)
			errx(1, "%s", devstat_errbuf);
		if (compute_stats_read(&cur.dinfo->devices[di],
				&last.dinfo->devices[di], busy_seconds,
				NULL, NULL,
				NULL, NULL,
				&transfers_per_secondr, &mb_per_secondr,
				NULL, NULL)!= 0)
			errx(1, "%s", devstat_errbuf);
		if (compute_stats_write(&cur.dinfo->devices[di],
				&last.dinfo->devices[di], busy_seconds,
				NULL, NULL,
				NULL, NULL,
				&transfers_per_secondw, &mb_per_secondw,
				NULL, NULL)!= 0)
			errx(1, "%s", devstat_errbuf);

		if (perf_select != 0) {
			dev_select[dn].bytes = total_bytes;
			if ((dev_select[dn].selected == 0)
			 || (dev_select[dn].selected > maxshowdevs))
				continue;
		}

		if (Kflag) {
			int block_size = cur.dinfo->devices[di].block_size;
			total_blocks = total_blocks * (block_size ?
						       block_size : 512) / 1024;
		}

		if (oflag > 0) {
			int msdig = (ms_per_transaction < 100.0) ? 1 : 0;

			if (Iflag == 0)
				printf("%4.0Lf%4.0Lf%5.*Lf ",
				       blocks_per_second,
				       transfers_per_second,
				       msdig,
				       ms_per_transaction);
			else 
				printf("%4.1ju%4.1ju%5.*Lf ",
				       (uintmax_t)total_blocks,
				       (uintmax_t)total_transfers,
				       msdig,
				       ms_per_transaction);
		} else {
			if (Iflag == 0) 
				if (Dflag > 0) {
					printf(" %5.2Lf %4.0Lf %6.2Lf %4.0Lf %6.2Lf  ",
					       kb_per_transfer,
					       transfers_per_secondr,
					       mb_per_secondr,
					       transfers_per_secondw,
					       mb_per_secondw);
				} else {
					printf(" %5.2Lf %4.0Lf %5.2Lf ",
					       kb_per_transfer,
					       transfers_per_second,
					       mb_per_second);
				}
			else {
				total_mb = total_bytes;
				total_mb /= 1024 * 1024;

				printf(" %5.2Lf %3.1ju %5.2Lf ",
				       kb_per_transfer,
				       (uintmax_t)total_transfers,
				       total_mb);
			}
		}
	}
}

static void
cpustats(void)
{
	if (cp_time_total == 0.0)
		cp_time_total = 1.0;

	printf(" %2.0f", 100. * diff_cp_time.cp_user / cp_time_total);
	printf(" %2.0f", 100. * diff_cp_time.cp_nice / cp_time_total);
	printf(" %2.0f", 100. * diff_cp_time.cp_sys / cp_time_total);
	printf(" %2.0f", 100. * diff_cp_time.cp_intr / cp_time_total);
	printf(" %2.0f", 100. * diff_cp_time.cp_idle / cp_time_total);
}
