/*
 * Copyright (c) 2004 Joerg Sonnenberger <joerg@bec.de>
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
 *
 * $DragonFly: src/lib/libiberty/argv.c,v 1.1 2004/10/23 12:15:21 joerg Exp $
 */

#include <assert.h>
#include <libiberty.h>
#include <stdlib.h>
#include <string.h>

#define IS_SEP(x) ((x) == ' ' || (x) == '\t')

/*
 * Return the first character of the next word.
 * len is the word len after quoting and escaping has been removed.
 */
static const char *
find_next_word(const char *arg, size_t *len)
{
	enum {NOQUOTE, SQUOTE, DQUOTE} in_quote = NOQUOTE;

	*len = 0;

	while (*arg != '\0') {
		if (IS_SEP(*arg) && in_quote == NOQUOTE) {
			break;
		} else if (*arg == '\\') {
			arg++;
			if (*arg == '\0')
				break;
			(*len)++;
		} else if (*arg == '"') {
			if (in_quote == NOQUOTE)
				in_quote = DQUOTE;
			else if (in_quote == DQUOTE)
				in_quote = NOQUOTE;				
			else
				(*len)++;				
		} else if (*arg == '\'') {
			if (in_quote == NOQUOTE)
				in_quote = SQUOTE;
			else if (in_quote == SQUOTE)
				in_quote = NOQUOTE;				
			else
				(*len)++;
		} else {
			(*len)++;
		}
		arg++;
	}
	return(arg);
}

static char *
copy_word(const char *arg, const char *end, size_t len)
{
	char *buf, *buf_begin;
	enum {NOQUOTE, SQUOTE, DQUOTE} in_quote = NOQUOTE;

	assert(arg < end);

	buf_begin = buf = malloc(len + 1);

	for (; arg < end; arg++) {
		if (*arg == '\\') {
			arg++;
			if (arg >= end)
			        break;
			*buf++ = *arg;
		} else if (*arg == '"') {
			if (in_quote == NOQUOTE)
				in_quote = DQUOTE;
			else if (in_quote == DQUOTE)
				in_quote = NOQUOTE;				
			else
		    		*buf++ = *arg;
		} else if (*arg == '\'') {
			if (in_quote == NOQUOTE)
				in_quote = SQUOTE;
			else if (in_quote == SQUOTE)
				in_quote = NOQUOTE;				
			else
		    		*buf++ = *arg;
		} else {
		        *buf++ = *arg;
		}
	}
	*buf = '\0';
	return(buf_begin);
}

char **
buildargv(const char *arg)
{
	void *tmp;
	const char *begin_arg;
	char **argv;
	int args;
	size_t len;

	if (arg == NULL)
		return(NULL);

	args = 0;
	argv = malloc(sizeof(char *));
	if (argv == NULL)
		return(NULL);
	argv[0] = NULL;

	while (*arg != '\0') {
		/* Skip leading white space. */
		while (IS_SEP(*arg))
			arg++;
		if (*arg == '\0')
			break;

		begin_arg = arg;
		arg = find_next_word(arg, &len);

		tmp = realloc(argv, (args + 2) * sizeof(char *));
		if (tmp == NULL)
			goto error;
		argv = tmp;

		argv[args] = copy_word(begin_arg, arg, len);
		if (argv[args] == NULL)
			goto error;
		args++;
		argv[args] = NULL;
	}

	/*
	 * The argument might be only white space, in that case,
	 * an empty argument list should be returned.
	 */
	if (args == 0) {
		tmp = realloc(argv, 2 * sizeof(char *));
		if (tmp == NULL)
			goto error;
		argv = tmp;

		argv[0] = strdup("");
		if (argv[0] == NULL)
			goto error;
		argv[1] = NULL;
	}
	return(argv);
error:
	freeargv(argv);
	return(NULL);
}

void
freeargv(char **argv)
{
	char **orig_argv;

	if (argv == NULL)
		return;

	for (orig_argv = argv; *argv != NULL; argv++)
		free(*argv);

	free(orig_argv);	
}

char **
dupargv(char * const *argv)
{
	char * const *orig_argv;
	char **new_argv, **new_argv2;
	size_t len;

	orig_argv = argv;
	for (len = 0; *argv != NULL; argv++)
		len++;

	new_argv = malloc((len+1) * sizeof(char *));

	new_argv2 = new_argv;
	for (; orig_argv != NULL; orig_argv++, new_argv++) {
		*new_argv = strdup(*orig_argv);
		if (*new_argv == NULL) {
			freeargv(new_argv2);
			return(NULL);
		}
	}
	*new_argv = NULL;

	return(new_argv2);
}
