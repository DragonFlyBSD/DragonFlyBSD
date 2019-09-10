/*-
 * Copyright (c) 2010 Ed Schouten <ed@FreeBSD.org>
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
 */

#include <sys/endian.h>
#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <utmpx.h>

struct outmp {
	char	ut_line[8];
	char	ut_user[16];
	char	ut_host[16];
        time_t  ut_time;
};

static const char vers[] = "utmpx-2.00";

static void
usage(void)
{

	fprintf(stderr, "usage: wtmpcvt input output\n");
	exit(1);
}

static void
outmp_to_utmpx(const struct outmp *ui, struct utmpx *uo)
{

	memset(uo, 0, sizeof *uo);
#define	COPY_STRING(field) do {						\
	strncpy(uo->ut_ ## field, ui->ut_ ## field,			\
	    MIN(sizeof uo->ut_ ## field, sizeof ui->ut_ ## field));	\
} while (0)
#define	COPY_LINE_TO_ID() do {						\
	strncpy(uo->ut_id, ui->ut_line,					\
	    MIN(sizeof uo->ut_id, sizeof ui->ut_line));			\
} while (0)
#define	MATCH(field, value)	(strncmp(ui->ut_ ## field, (value),	\
					sizeof(ui->ut_ ## field)) == 0)
	if (MATCH(user, "reboot") && MATCH(line, "~")) {
		uo->ut_type = INIT_PROCESS;
		COPY_STRING(user);
		COPY_STRING(line);
	} else if (MATCH(user, "date") && MATCH(line, "|")) {
		uo->ut_type = OLD_TIME;
	} else if (MATCH(user, "date") && MATCH(line, "{")) {
		uo->ut_type = NEW_TIME;
	} else if (MATCH(user, "shutdown") && MATCH(line, "~")) {
		uo->ut_type = INIT_PROCESS;
		COPY_STRING(user);
		COPY_STRING(line);
	} else if (MATCH(user, "") && MATCH(host, "") && !MATCH(line, "")) {
		uo->ut_type = DEAD_PROCESS;
		COPY_LINE_TO_ID();
	} else if (!MATCH(user, "") && !MATCH(line, "") && ui->ut_time != 0) {
		uo->ut_type = USER_PROCESS;
		COPY_STRING(user);
		COPY_STRING(line);
		COPY_STRING(host);
		COPY_LINE_TO_ID();
	} else {
		uo->ut_type = EMPTY;
		return;
	}
#undef COPY_STRING
#undef COPY_LINE_TO_ID
#undef MATCH

	uo->ut_tv.tv_sec = ui->ut_time;
	uo->ut_tv.tv_usec = 0;
}

int
main(int argc, char *argv[])
{
	FILE *in, *out;
	struct outmp ui;
	struct utmpx uo;

	if (argc != 3)
		usage();

	/* Open files. */
	in = fopen(argv[1], "r");
	if (in == NULL)
		err(1, "%s", argv[1]);
	out = fopen(argv[2], "w");
	if (out == NULL)
		err(1, "%s", argv[2]);

	/* Write signature. */
	memset(&uo, 0, sizeof uo);
	uo.ut_type = SIGNATURE;
	memcpy(uo.ut_user, vers, sizeof(vers));
	fwrite(&uo, sizeof uo, 1, out);

	/* Process entries. */
	while (fread(&ui, sizeof ui, 1, in) == 1) {
		outmp_to_utmpx(&ui, &uo);
		if (uo.ut_type == EMPTY)
			continue;

		/* Write new entry to output file. */
		fwrite(&uo, sizeof uo, 1, out);
	}

	fclose(in);
	fclose(out);
	return (0);
}
