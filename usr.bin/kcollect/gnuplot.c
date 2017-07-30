/*
 * Copyright (c) 2017 The DragonFly Project.  All rights reserved.
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

#include "kcollect.h"

void
start_gnuplot(int ac __unused, char **av __unused, const char *plotfile)
{
	OutFP = popen("gnuplot", "w");
	if (OutFP == NULL) {
		fprintf(stderr, "can't find gnuplot\n");
		exit(1);
	}

	/*
	 * If plotfile is specified allow .jpg or .JPG or .png or .PNG
	 */
	if (plotfile) {
		const char *ext;

		if ((ext = strrchr(plotfile, '.')) == NULL) {
			ext = "";
		} else {
			++ext;
		}
		if (strcmp(ext, "jpg") == 0 ||
		    strcmp(ext, "JPG") == 0) {
			fprintf(OutFP, "set terminal jpeg size %d,%d\n",
				OutputWidth, OutputHeight);
		} else if (strcmp(ext, "png") == 0 ||
			   strcmp(ext, "PNG") == 0) {
			fprintf(OutFP, "set terminal png size %d,%d\n",
				OutputWidth, OutputHeight);
		} else {
			fprintf(stderr, "plotfile must be .jpg or .png\n");
			exit(1);
		}
		fprintf(OutFP, "set output \"%s\"\n", plotfile);
	} else {
		fprintf(OutFP, "set terminal x11 persist size %d,%d\n",
			OutputWidth, OutputHeight);
	}
}

void
dump_gnuplot(kcollect_t *ary, size_t count)
{
	int plot1[] = { KCOLLECT_MEMFRE, KCOLLECT_MEMCAC,
			KCOLLECT_MEMINA, KCOLLECT_MEMACT,
			KCOLLECT_MEMWIR, KCOLLECT_LOAD };
	int plot2[] = { KCOLLECT_IDLEPCT, KCOLLECT_INTRPCT,
			KCOLLECT_SYSTPCT, KCOLLECT_USERPCT,
			KCOLLECT_SWAPPCT,
			KCOLLECT_VMFAULT, KCOLLECT_SYSCALLS, KCOLLECT_NLOOKUP };
	const char *id1[] = {
			"free", "cache",
			"inact", "active",
			"wired", "load" };
	const char *id2[] = {
			"idle", "intr", "system", "user",
			"swap",
			"faults", "syscalls", "nlookups" };
	struct tm *tmv;
	char buf[64];
	uint64_t value;
	time_t t;
	double dv;
	double smoothed_dv;
	int i;
	int j;
	int jj;
	int k;

	/*
	 * NOTE: be sure to reset any fields adjusted by the second plot,
	 *	 in case we are streaming plots with -f.
	 */
	fprintf(OutFP, "set xdata time\n");
	fprintf(OutFP, "set timefmt \"%%d-%%b-%%Y %%H:%%M:%%S\"\n");
	fprintf(OutFP, "set style fill solid 1.0\n");
	fprintf(OutFP, "set multiplot layout 2,1\n");
	fprintf(OutFP, "set key outside\n");
	fprintf(OutFP, "set lmargin 10\n");
	fprintf(OutFP, "set rmargin 25\n");
	fprintf(OutFP, "set xtics rotate\n");
	fprintf(OutFP, "set format x '%%H:%%M'\n");

	fprintf(OutFP, "set ylabel \"GB\"\n");

	fprintf(OutFP, "set yrange [0:%d]\n",
		(int)((KCOLLECT_GETSCALE(ary[0].data[KCOLLECT_MEMFRE]) +
		       999999) / 1000000000));
	fprintf(OutFP, "set autoscale y2\n");
	fprintf(OutFP, "set y2label \"Load\"\n");
	fprintf(OutFP, "set ytics nomirror\n");
	fprintf(OutFP, "set y2tics nomirror\n");

	fprintf(OutFP,
		"plot "
		"\"-\" using 1:3 title \"%s\" with boxes lw 1, "
		"\"-\" using 1:3 title \"%s\" with boxes lw 1, "
		"\"-\" using 1:3 title \"%s\" with boxes lw 1, "
		"\"-\" using 1:3 title \"%s\" with boxes lw 1, "
		"\"-\" using 1:3 title \"%s\" with boxes lw 1, "
		"\"-\" using 1:3 axes x1y2 title \"%s\" with lines lw 1\n",
		id1[0], id1[1], id1[2], id1[3], id1[4], id1[5]);

	for (jj = 0; jj < (int)(sizeof(plot1) / sizeof(plot1[0])); ++jj) {
		j = plot1[jj];

		smoothed_dv = 0.0;
		for (i = count - 1; i >= 2; --i) {
			/*
			 * Timestamp
			 */
			t = ary[i].realtime.tv_sec;
			if (t < 1000)
				continue;
			if (UseGMT)
				tmv = gmtime(&t);
			else
				tmv = localtime(&t);
			strftime(buf, sizeof(buf), "%d-%b-%Y %H:%M:%S", tmv);
			value = ary[i].data[j];
			if (jj <= 4) {
				for (k = jj + 1; k <= 4; ++k)
					value += ary[i].data[plot1[k]];
				dv = (double)value / 1e9;
			} else {
				dv = (double)value / 100.0;
			}
			if (SmoothOpt) {
				if (i == (int)(count - 1)) {
					smoothed_dv = dv;
				} else if (smoothed_dv < dv) {
					smoothed_dv =
					 (smoothed_dv * 5.0 + 5 * dv) /
					 10.0;
				} else {
					smoothed_dv =
					 (smoothed_dv * 9.0 + 1 * dv) /
					 10.0;
				}
				dv = smoothed_dv;
			}
			fprintf(OutFP, "%s %6.2f\n", buf, dv);
		}
		fprintf(OutFP, "e\n");
	}

	fprintf(OutFP, "set ylabel \"Cpu Utilization\"\n");
	fprintf(OutFP, "set y2label \"MOps/sec (smoothed)\"\n");
	fprintf(OutFP, "set ytics nomirror\n");
	fprintf(OutFP, "set y2tics nomirror\n");
	fprintf(OutFP, "set yrange [0:105]\n");
	fprintf(OutFP, "set y2range [0:1.0]\n");

	fprintf(OutFP,
		"plot "
		"\"-\" using 1:3 title \"%s\" with boxes lw 1, "
		"\"-\" using 1:3 title \"%s\" with boxes lw 1, "
		"\"-\" using 1:3 title \"%s\" with boxes lw 1, "
		"\"-\" using 1:3 title \"%s\" with boxes lw 1, "
		"\"-\" using 1:3 title \"%s\" with lines lw 1, "
		"\"-\" using 1:3 axes x1y2 title \"%s\" with lines lw 1, "
		"\"-\" using 1:3 axes x1y2 title \"%s\" with lines lw 1, "
		"\"-\" using 1:3 axes x1y2 title \"%s\" with lines lw 1\n",
		id2[0], id2[1], id2[2], id2[3], id2[4], id2[5], id2[6], id2[7]);

	for (jj = 0; jj < (int)(sizeof(plot2) / sizeof(plot2[0])); ++jj) {
		j = plot2[jj];

		smoothed_dv = 0.0;
		for (i = count - 1; i >= 2; --i) {
			/*
			 * Timestamp
			 */
			t = ary[i].realtime.tv_sec;
			if (t < 1000)
				continue;
			if (UseGMT)
				tmv = gmtime(&t);
			else
				tmv = localtime(&t);
			strftime(buf, sizeof(buf), "%d-%b-%Y %H:%M:%S", tmv);
			value = ary[i].data[j];

			if (jj <= 3) {
				/*
				 * intr/sys/user/idle percentages
				 */
				for (k = jj + 1; k <= 3; ++k)
					value += ary[i].data[plot2[k]];
				dv = (double)value / 100.0;
				if (SmoothOpt) {
					if (i == (int)(count - 1)) {
						smoothed_dv = dv;
					} else if (smoothed_dv < dv) {
						smoothed_dv =
						 (smoothed_dv * 5.0 + 5 * dv) /
					         10.0;
					} else {
						smoothed_dv =
						 (smoothed_dv * 9.0 + 1 * dv) /
					         10.0;
					}
					dv = smoothed_dv;
				}
			} else {
				if (jj >= 5) {
					/* fault counters */
					dv = (double)value / KCOLLECT_INTERVAL;
					dv = dv / 1e6;
				} else {
					/* swap percentage (line graph) */
					dv = (double)value / 100.0;
				}
				if (i == (int)(count - 1)) {
					smoothed_dv = dv;
				} else if (smoothed_dv < dv) {
					smoothed_dv =
					 (smoothed_dv * 5.0 + 5 * dv) /
					 10.0;
				} else {
					smoothed_dv = (smoothed_dv * 9.0 + dv) /
						      10.0;
				}
				dv = smoothed_dv;
			}
			fprintf(OutFP, "%s %6.2f\n", buf, dv);
		}
		fprintf(OutFP, "e\n");
	}
	fflush(OutFP);
}
