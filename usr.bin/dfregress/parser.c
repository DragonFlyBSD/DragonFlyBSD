/*
 * Copyright (c) 2009-2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include "parser.h"

#define _iswhitespace(X)	((((X) == ' ') || ((X) == '\t'))?1:0)


static int line_no = 1;

static int iswhitespace(char c)
{
	return _iswhitespace(c);
}

static int iscomma(char c)
{
	return (c == ',');
}

void
syntax_error(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	errx(1, "syntax error on line %d: %s\n", line_no, buf);
}


int
entry_check_num_args(char **tokens, int num)
{
	int i;

	for (i = 0; tokens[i] != NULL; i++)
		;

	if (i < num) {
		syntax_error("at least %d tokens were expected but only %d "
		    "were found", num, i);
		return 1;
	}
	return 0;
}

static int
line_tokenize(char *buffer, int (*is_sep)(char), char comment_char, char **tokens)
{
	int c, n, i;
	int quote = 0;

	i = strlen(buffer) + 1;
	c = 0;

	/* Skip leading white-space */
	while ((_iswhitespace(buffer[c])) && (c < i)) c++;

	/*
	 * If this line effectively (after indentation) begins with the comment
	 * character, we ignore the rest of the line.
	 */
	if (buffer[c] == comment_char)
		return 0;

	tokens[0] = &buffer[c];
	for (n = 1; c < i; c++) {
		if (buffer[c] == '"') {
			quote = !quote;
			if (quote) {
				if ((c >= 1) && (&buffer[c] != tokens[n-1])) {
#if 0
					syntax_error("stray opening quote not "
					    "at beginning of token");
					/* NOTREACHED */
#endif
				} else {
					tokens[n-1] = &buffer[c+1];
				}
			} else {
				if ((c < i-1) && (!is_sep(buffer[c+1]))) {
#if 0
					syntax_error("stray closing quote not "
					    "at end of token");
					/* NOTREACHED */
#endif
				} else {
					buffer[c] = '\0';
				}
			}
		}

		if (quote) {
			continue;
		}

		if (is_sep(buffer[c])) {
			buffer[c++] = '\0';
			while ((_iswhitespace(buffer[c])) && (c < i)) c++;
			tokens[n++] = &buffer[c--];
		}
	}
	tokens[n] = NULL;

	if (quote) {
		tokens[0] = NULL;
		return 0;
	}

	return n;
}

static int
process_line(FILE* fd, parser_t parser, void *arg)
{
	char buffer[4096];
	char *tokens[256];
	int c, n, i = 0;
	int ret = 0;

	while (((c = fgetc(fd)) != EOF) && (c != '\n')) {
		buffer[i++] = (char)c;
		if (i == (sizeof(buffer) -1))
			break;
	}
	buffer[i] = '\0';

	if (feof(fd) || ferror(fd))
		ret = 1;


	n = line_tokenize(buffer, &iswhitespace, '#', tokens);

	/*
	 * If there are not enough arguments for any function or it is
	 * a line full of whitespaces, we just return here. Or if a
	 * quote wasn't closed.
	 */
	if ((n < 1) || (tokens[0][0] == '\0'))
		return ret;

	parser(arg, tokens);

	return ret;
}

int
parse_options(char *str, char **options)
{
	int i;

	i = line_tokenize(str, &iscomma, '#', options);
	if (i == 0)
		syntax_error("Invalid expression in options token");
		/* NOTREACHED */

	return i;
}

int
process_file(const char *file, parser_t parser, void *arg, int *nlines)
{
	FILE *fd;

	line_no = 0;

	fd = fopen(file, "r");
	if (fd == NULL)
		err(1, "fopen");
		/* NOTREACHED */

	while (process_line(fd, parser, arg) == 0)
		++line_no;

	fclose(fd);

	if (nlines != NULL)
		*nlines = line_no;

	return 0;
}

