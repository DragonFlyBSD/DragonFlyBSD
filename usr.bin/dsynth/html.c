/*
 * Copyright (c) 2019-2020 The DragonFly Project.  All rights reserved.
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

#define SNPRINTF(buf, ctl, ...)         \
	snprintf((buf), sizeof(buf), ctl, ## __VA_ARGS__)

static char *ReportPath;
static int HistNum;
static int EntryNum;
static char KickOff_Buf[64];

static const char *CopyFilesAry[] = {
	"favicon.png",
	"progress.html",
	"progress.css",
	"progress.js",
	"dsynth.png",
	NULL
};

static char **HtmlSlots;
static time_t HtmlStart;
static time_t HtmlLast;

/*
 * Get rid of stuff that might blow up the json output.
 */
static const char *
dequote(const char *reason)
{
	static char *buf;
	int i;

	for (i = 0; reason[i]; ++i) {
		if (reason[i] == '\"' || reason[i] == '\n' ||
		    reason[i] == '\\') {
			if (reason != buf) {
				if (buf)
					free(buf);
				buf = strdup(reason);
				reason = buf;
			}
			buf[i] = ' ';
		}
	}
	return reason;
}

static void
HtmlInit(void)
{
	struct dirent *den;
	DIR *dir;
	struct stat st;
	struct tm tmm;
	size_t len;
	char *src;
	char *dst;
	time_t t;
	int i;

	HtmlSlots = calloc(sizeof(char *), MaxWorkers);
	HtmlLast = 0;
	HtmlStart = time(NULL);

	asprintf(&ReportPath, "%s/Report", LogsPath);
	if (stat(ReportPath, &st) < 0 && mkdir(ReportPath, 0755) < 0)
		dfatal("Unable to create %s", ReportPath);
	for (i = 0; CopyFilesAry[i]; ++i) {
		asprintf(&src, "%s/%s", SCRIPTPATH(SCRIPTDIR), CopyFilesAry[i]);
		if (strcmp(CopyFilesAry[i], "progress.html") == 0) {
			asprintf(&dst, "%s/index.html", ReportPath);
		} else {
			asprintf(&dst, "%s/%s", ReportPath, CopyFilesAry[i]);
		}
		copyfile(src, dst);
		free(src);
		free(dst);
	}

	asprintf(&src, "%s/summary.json", ReportPath);
	remove(src);
	free(src);

	t = time(NULL);
	gmtime_r(&t, &tmm);
	strftime(KickOff_Buf, sizeof(KickOff_Buf),
		 " %d-%b-%Y %H:%M:%S %Z", &tmm);

	dir = opendir(ReportPath);
	if (dir == NULL)
		dfatal("Unable to scan %s", ReportPath);
	while ((den = readdir(dir)) != NULL) {
		len = strlen(den->d_name);
		if (len > 13 &&
		    strcmp(den->d_name + len - 13, "_history.json") == 0) {
			asprintf(&src, "%s/%s", ReportPath, den->d_name);
			remove(src);
			free(src);
		}
	}
	closedir(dir);

	/*
	 * First history file
	 */
	HistNum = 0;
	EntryNum = 1;
}

static void
HtmlDone(void)
{
	int i;

	for (i = 0; i < MaxWorkers; ++i) {
		if (HtmlSlots[i])
			free(HtmlSlots[i]);
	}
	free(HtmlSlots);
	HtmlSlots = NULL;
}

static void
HtmlReset(void)
{
}

static void
HtmlUpdate(worker_t *work, const char *portdir)
{
	const char *phase;
	const char *origin;
	time_t t;
	int i = work->index;
	int h;
	int m;
	int s;
	int clear;
	char elapsed_buf[32];
	char lines_buf[32];

	phase = "Unknown";
	origin = "";
	clear = 0;

	switch(work->state) {
	case WORKER_NONE:
		phase = "None";
		/* fall through */
	case WORKER_IDLE:
		if (work->state == WORKER_IDLE)
			phase = "Idle";
		clear = 1;
		break;
	case WORKER_FAILED:
		if (work->state == WORKER_FAILED)
			phase = "Failed";
		/* fall through */
	case WORKER_EXITING:
		if (work->state == WORKER_EXITING)
			phase = "Exiting";
		return;
		/* NOT REACHED */
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

	if (clear) {
		SNPRINTF(elapsed_buf, "%s", " --:--:--");
		SNPRINTF(lines_buf, "%s", "");
		origin = "";
	} else {
		t = time(NULL) - work->start_time;
		s = t % 60;
		m = t / 60 % 60;
		h = t / 60 / 60;
		if (h > 99)
			SNPRINTF(elapsed_buf, "%3d:%02d:%02d", h, m, s);
		else
			SNPRINTF(elapsed_buf, " %02d:%02d:%02d", h, m, s);

		if (work->state == WORKER_RUNNING)
			phase = getphasestr(work->phase);

		/*
		 * When called from the monitor frontend portdir has to be
		 * passed in directly because work->pkg is not mapped.
		 */
		if (portdir)
			origin = portdir;
		else if (work->pkg)
			origin = work->pkg->portdir;
		else
			origin = "";

		SNPRINTF(lines_buf, "%ld", work->lines);
	}

	/*
	 * Update the summary information
	 */
	if (HtmlSlots[i])
		free(HtmlSlots[i]);
	asprintf(&HtmlSlots[i],
		 "  {\n"
		 "     \"ID\":\"%02d\"\n"
		 "     ,\"elapsed\":\"%s\"\n"
		 "     ,\"phase\":\"%s\"\n"
		 "     ,\"origin\":\"%s\"\n"
		 "     ,\"lines\":\"%s\"\n"
		 "  }\n",
		 i,
		 elapsed_buf,
		 phase,
		 origin,
		 lines_buf
	);
}

static void
HtmlUpdateTop(topinfo_t *info)
{
	char *path;
	char *dst;
	FILE *fp;
	int i;
	char elapsed_buf[32];
	char swap_buf[32];
	char load_buf[32];

	/*
	 * Be sure to do the first update and final update, but otherwise
	 * only update every 10 seconds or so.
	 */
	if (HtmlLast && (int)(time(NULL) - HtmlLast) < 10 && info->active)
		return;
	HtmlLast = time(NULL);

	if (info->h > 99) {
		SNPRINTF(elapsed_buf, "%3d:%02d:%02d",
			 info->h, info->m, info->s);
	} else {
		SNPRINTF(elapsed_buf, " %02d:%02d:%02d",
			 info->h, info->m, info->s);
	}

	if (info->noswap)
		SNPRINTF(swap_buf, "-    ");
	else
		SNPRINTF(swap_buf, "%5.1f", info->dswap);

	if (info->dload[0] > 999.9)
		SNPRINTF(load_buf, "%5.0f", info->dload[0]);
	else
		SNPRINTF(load_buf, "%5.1f", info->dload[0]);

	asprintf(&path, "%s/summary.json.new", ReportPath);
	asprintf(&dst, "%s/summary.json", ReportPath);
	fp = fopen(path, "we");
	if (!fp)
		ddassert(0);
	if (fp) {
		fprintf(fp,
			"{\n"
			"  \"profile\":\"%s\"\n"
			"  ,\"kickoff\":\"%s\"\n"
			"  ,\"kfiles\":%d\n"
			"  ,\"active\":%d\n"
			"  ,\"stats\":{\n"
			"    \"queued\":%d\n"
			"    ,\"built\":%d\n"
			"    ,\"failed\":%d\n"
			"    ,\"ignored\":%d\n"
			"    ,\"skipped\":%d\n"
			"    ,\"remains\":%d\n"
			"    ,\"meta\":%d\n"
			"    ,\"elapsed\":\"%s\"\n"
			"    ,\"pkghour\":%d\n"
			"    ,\"impulse\":%d\n"
			"    ,\"swapinfo\":\"%s\"\n"
			"    ,\"load\":\"%s\"\n"
			"  }\n",
			Profile,
			KickOff_Buf,
			HistNum,		/* kfiles */
			info->active,		/* active */

			info->total,		/* queued */
			info->successful,	/* built */
			info->failed,		/* failed */
			info->ignored,		/* ignored */
			info->skipped,		/* skipped */
			info->remaining,	/* remaining */
			info->meta,		/* meta-nodes */
			elapsed_buf,		/* elapsed */
			info->pkgrate,		/* pkghour */
			info->pkgimpulse,	/* impulse */
			swap_buf,		/* swapinfo */
			load_buf		/* load */
		);
		fprintf(fp,
			"  ,\"builders\":[\n"
		);
		for (i = 0; i < MaxWorkers; ++i) {
			if (HtmlSlots[i]) {
				if (i)
					fprintf(fp, ",");
				fwrite(HtmlSlots[i], 1,
				       strlen(HtmlSlots[i]), fp);
			} else {
				fprintf(fp,
					"   %s{\n"
					"     \"ID\":\"%02d\"\n"
					"     ,\"elapsed\":\"Shutdown\"\n"
					"     ,\"phase\":\"\"\n"
					"     ,\"origin\":\"\"\n"
					"     ,\"lines\":\"\"\n"
					"    }\n",
					(i ? "," : ""),
					i
				);
			}
		}
		fprintf(fp,
			"  ]\n"
			"}\n");
		fflush(fp);
		fclose(fp);
	}
	rename(path, dst);
	free(path);
	free(dst);
}

static void
HtmlUpdateLogs(void)
{
}

static void
HtmlUpdateCompletion(worker_t *work, int dlogid, pkg_t *pkg,
		     const char *reason, const char *skipbuf)
{
	FILE *fp;
	char *path;
	char elapsed_buf[64];
	struct stat st;
	time_t t;
	int s, m, h;
	int slot;
	const char *result;
	char *mreason;

	if (skipbuf[0] == 0)
		skipbuf = "0";
	else if (skipbuf[0] == ' ')
		++skipbuf;

	mreason = NULL;
	if (work) {
		t = time(NULL) - work->start_time;
		s = t % 60;
		m = t / 60 % 60;
		h = t / 60 / 60;
		SNPRINTF(elapsed_buf, "%02d:%02d:%02d", h, m, s);
		slot = work->index;
	} else {
		slot = -1;
		elapsed_buf[0] = 0;
	}

	switch(dlogid) {
	case DLOG_SUCC:
		if (pkg->flags & PKGF_DUMMY)
			result = "meta";
		else
			result = "built";
		break;
	case DLOG_FAIL:
		result = "failed";
		if (work) {
			asprintf(&mreason, "%s:%s",
				 getphasestr(work->phase),
				 reason);
		} else {
			asprintf(&mreason, "unknown:%s", reason);
		}
		reason = mreason;
		break;
	case DLOG_IGN:
		result = "ignored";
		asprintf(&mreason, "%s:|:%s", reason, skipbuf);
		reason = mreason;
		break;
	case DLOG_SKIP:
		result = "skipped";
		break;
	default:
		result = "Unknown";
		break;
	}

	t = time(NULL) - HtmlStart;
	s = t % 60;
	m = t / 60 % 60;
	h = t / 60 / 60;

	/*
	 * Cycle history file as appropriate, includes initial file handling.
	 */
	if (HistNum == 0)
		HistNum = 1;
	asprintf(&path, "%s/%02d_history.json", ReportPath, HistNum);
	if (stat(path, &st) < 0) {
		fp = fopen(path, "we");
	} else if (st.st_size > 50000) {
		++HistNum;
		free(path);
		asprintf(&path, "%s/%02d_history.json", ReportPath, HistNum);
		fp = fopen(path, "we");
	} else {
		fp = fopen(path, "r+e");
		fseek(fp, 0, SEEK_END);
	}

	if (fp) {
		if (ftell(fp) == 0) {
			fprintf(fp, "[\n");
		} else {
			fseek(fp, -2, SEEK_END);
		}
		fprintf(fp,
			"  %s{\n"
			"   \"entry\":%d\n"
			"   ,\"elapsed\":\"%02d:%02d:%02d\"\n"
			"   ,\"ID\":\"%02d\"\n"
			"   ,\"result\":\"%s\"\n"
			"   ,\"origin\":\"%s\"\n"
			"   ,\"info\":\"%s\"\n"
			"   ,\"duration\":\"%s\"\n"
			"  }\n"
			"]\n",
			((ftell(fp) > 10) ? "," : ""),
			EntryNum,
			h, m, s,
			slot,
			result,
			pkg->portdir,
			dequote(reason),
			elapsed_buf
		);
		++EntryNum;
		fclose(fp);

	}
	free(path);
	if (mreason)
		free(mreason);
}

static void
HtmlSync(void)
{
}

runstats_t HtmlRunStats = {
	.init = HtmlInit,
	.done = HtmlDone,
	.reset = HtmlReset,
	.update = HtmlUpdate,
	.updateTop = HtmlUpdateTop,
	.updateLogs = HtmlUpdateLogs,
	.updateCompletion = HtmlUpdateCompletion,
	.sync = HtmlSync
};
