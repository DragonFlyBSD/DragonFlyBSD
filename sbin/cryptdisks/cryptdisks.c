/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <libcryptsetup.h>

#define iswhitespace(X)	((((X) == ' ') || ((X) == '\t'))?1:0)

#define CRYPTDISKS_START	1
#define CRYPTDISKS_STOP		2

static void syntax_error(const char *, ...) __printflike(1, 2);

static int line_no = 1;

static int yesDialog(char *msg __unused)
{
	return 1;
}

static void cmdLineLog(int level __unused, char *msg)
{
	printf("%s", msg);
}

static struct interface_callbacks cmd_icb = {
	.yesDialog = yesDialog,
	.log = cmdLineLog,
};

static void
syntax_error(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	errx(1, "crypttab: syntax error on line %d: %s\n", line_no, buf);
}


static int
entry_check_num_args(char **tokens, int num)
{
	int i;

	for (i = 0; tokens[i] != NULL; i++)
		;

	if (i < num) {
		syntax_error("at least %d tokens were expected but only %d were found", num, i);
		return 1;
	}
	return 0;
}

static int
entry_parser(char **tokens, int type)
{
	struct crypt_options co;
	int r, error;

	if (entry_check_num_args(tokens, 2) != 0)
		return 1;

	bzero(&co, sizeof(co));

	co.icb = &cmd_icb;
	co.name = tokens[0];
	co.device = tokens[1];

	error = crypt_isLuks(&co);
	if (error) {
		printf("crypttab: line %d: device %s is not a luks device\n",
		    line_no, co.device);
		return 1;
	}

	if (type == CRYPTDISKS_STOP) {
		/* Check if the device is active */
		r = crypt_query_device(&co);

		/* If r > 0, then the device is active */
		if (r <= 0)
			return 0;

		/* Actually close the device */
		crypt_remove_device(&co);
	} else if (type == CRYPTDISKS_START) {
		if ((tokens[2] != NULL) && (strcmp(tokens[2], "none") != 0)) {
			/* We got a keyfile */
			co.key_file = tokens[2];
		}

		/* Open the device */
		crypt_luksOpen(&co);
	}

	return 0;
}

static int
process_line(FILE* fd, int type)
{
	char buffer[4096];
	char *tokens[256];
	int c, n, i = 0;
	int quote = 0;
	int ret = 0;

	while (((c = fgetc(fd)) != EOF) && (c != '\n')) {
		buffer[i++] = (char)c;
		if (i == (sizeof(buffer) -1))
			break;
	}
	buffer[i] = '\0';

	if (feof(fd) || ferror(fd))
		ret = 1;
	c = 0;
	while (((buffer[c] == ' ') || (buffer[c] == '\t')) && (c < i)) c++;
	/*
	 * If this line effectively (after indentation) begins with the comment
	 * character #, we ignore the rest of the line.
	 */
	if (buffer[c] == '#')
		return 0;

	tokens[0] = &buffer[c];
	for (n = 1; c < i; c++) {
		if (buffer[c] == '"') {
			quote = !quote;
			if (quote) {
				if ((c >= 1) && (&buffer[c] != tokens[n-1])) {
					syntax_error("stray opening quote not at beginning of token");
					/* NOTREACHED */
				}
				tokens[n-1] = &buffer[c+1];
			} else {
				if ((c < i-1) && (!iswhitespace(buffer[c+1]))) {
					syntax_error("stray closing quote not at end of token");
					/* NOTREACHED */
				}
				buffer[c] = '\0';
			}
		}

		if (quote) {
			continue;
		}

		if ((buffer[c] == ' ') || (buffer[c] == '\t')) {
			buffer[c++] = '\0';
			while ((iswhitespace(buffer[c])) && (c < i)) c++;
			tokens[n++] = &buffer[c--];
		}
	}
	tokens[n] = NULL;

	/*
	 * If there are not enough arguments for any function or it is
	 * a line full of whitespaces, we just return here. Or if a
	 * quote wasn't closed.
	 */
	if ((quote) || (n < 2) || (tokens[0][0] == '\0'))
		return ret;

	entry_parser(tokens, type);

	return ret;
}


int
main(int argc, char *argv[])
{
	FILE *fd;
	int ch, start = 0, stop = 0;

	while ((ch = getopt(argc, argv, "01")) != -1) {
		switch (ch) {
		case '1':
			start = 1;
			break;
		case '0':
			stop = 1;
			break;
		default:
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if ((start && stop) || (!start && !stop))
		errx(1, "please specify exactly one of -0 and -1");

	fd = fopen("/etc/crypttab", "r");
	if (fd == NULL)
		err(1, "fopen");
		/* NOTREACHED */

	while (process_line(fd, (start) ? CRYPTDISKS_START : CRYPTDISKS_STOP) == 0)
		++line_no;

	fclose(fd);
	return 0;
}
