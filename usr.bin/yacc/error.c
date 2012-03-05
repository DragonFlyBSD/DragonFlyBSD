/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Paul Corbett.
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
 *
 * @(#)error.c	5.3 (Berkeley) 6/1/90
 * $FreeBSD: src/usr.bin/yacc/error.c,v 1.7 1999/08/28 01:07:59 peter Exp $
 */

/* routines for printing error messages  */

#include <stdlib.h>
#include "defs.h"

static void print_pos(const char *, const char *);

void
fatal(const char *msg)
{
    errx(2, "f - %s", msg);
}


void
no_space(void)
{
    errx(2, "f - out of space");
}


void
open_error(const char *filename)
{
    errx(2, "f - cannot open \"%s\"", filename);
}


void
unexpected_EOF(void)
{
    errx(1, "e - line %d of \"%s\", unexpected end-of-file",
	    lineno, input_file_name);
}


static void
print_pos(const char *st_line, const char *st_cptr)
{
    const char *s;

    if (st_line == NULL) return;
    for (s = st_line; *s != '\n'; ++s)
    {
	if (isprint(*s) || *s == '\t')
	    putc(*s, stderr);
	else
	    putc('?', stderr);
    }
    putc('\n', stderr);
    for (s = st_line; s < st_cptr; ++s)
    {
	if (*s == '\t')
	    putc('\t', stderr);
	else
	    putc(' ', stderr);
    }
    putc('^', stderr);
    putc('\n', stderr);
}


void
syntax_error(int st_lineno, const char *st_line, const char *st_cptr)
{
    warnx("e - line %d of \"%s\", syntax error",
	    st_lineno, input_file_name);
    print_pos(st_line, st_cptr);
    exit(1);
}


void
unterminated_comment(int c_lineno, const char *c_line, const char *c_cptr)
{
    warnx("e - line %d of \"%s\", unmatched /*",
	    c_lineno, input_file_name);
    print_pos(c_line, c_cptr);
    exit(1);
}


void
unterminated_string(int s_lineno, const char *s_line, const char *s_cptr)
{
    warnx("e - line %d of \"%s\", unterminated string",
	    s_lineno, input_file_name);
    print_pos(s_line, s_cptr);
    exit(1);
}


void
unterminated_text(int t_lineno, const char *t_line, const char *t_cptr)
{
    warnx("e - line %d of \"%s\", unmatched %%{",
	    t_lineno, input_file_name);
    print_pos(t_line, t_cptr);
    exit(1);
}


void
unterminated_union(int u_lineno, const char *u_line, const char *u_cptr)
{
    warnx("e - line %d of \"%s\", unterminated %%union declaration",
		u_lineno, input_file_name);
    print_pos(u_line, u_cptr);
    exit(1);
}


void
over_unionized(const char *u_cptr)
{
    warnx("e - line %d of \"%s\", too many %%union declarations",
		lineno, input_file_name);
    print_pos(line, u_cptr);
    exit(1);
}


void
illegal_tag(int t_lineno, const char *t_line, const char *t_cptr)
{
    warnx("e - line %d of \"%s\", illegal tag", t_lineno, input_file_name);
    print_pos(t_line, t_cptr);
    exit(1);
}


void
illegal_character(const char *c_cptr)
{
    warnx("e - line %d of \"%s\", illegal character", lineno, input_file_name);
    print_pos(line, c_cptr);
    exit(1);
}


void
used_reserved(const char *s)
{
    errx(1, "e - line %d of \"%s\", illegal use of reserved symbol %s",
		lineno, input_file_name, s);
}


void
tokenized_start(const char *s)
{
     errx(1, "e - line %d of \"%s\", the start symbol %s cannot be \
declared to be a token", lineno, input_file_name, s);
}


void
retyped_warning(const char *s)
{
    warnx("w - line %d of \"%s\", the type of %s has been redeclared",
		lineno, input_file_name, s);
}


void
reprec_warning(const char *s)
{
    warnx("w - line %d of \"%s\", the precedence of %s has been redeclared",
		lineno, input_file_name, s);
}


void
revalued_warning(const char *s)
{
    warnx("w - line %d of \"%s\", the value of %s has been redeclared",
		lineno, input_file_name, s);
}


void
terminal_start(const char *s)
{
    errx(1, "e - line %d of \"%s\", the start symbol %s is a token",
		lineno, input_file_name, s);
}


void
restarted_warning(void)
{
    warnx("w - line %d of \"%s\", the start symbol has been redeclared",
		lineno, input_file_name);
}


void
no_grammar(void)
{
    errx(1, "e - line %d of \"%s\", no grammar has been specified",
		lineno, input_file_name);
}


void
terminal_lhs(int s_lineno)
{
    errx(1, "e - line %d of \"%s\", a token appears on the lhs of a production",
		s_lineno, input_file_name);
}


void
prec_redeclared(void)
{
    warnx("w - line %d of  \"%s\", conflicting %%prec specifiers",
		lineno, input_file_name);
}


void
unterminated_action(int a_lineno, const char *a_line, const char *a_cptr)
{
    warnx("e - line %d of \"%s\", unterminated action",
	    a_lineno, input_file_name);
    print_pos(a_line, a_cptr);
}


void
dollar_warning(int a_lineno, int i)
{
    warnx("w - line %d of \"%s\", $%d references beyond the \
end of the current rule", a_lineno, input_file_name, i);
}


void
dollar_error(int a_lineno, const char *a_line, const char *a_cptr)
{
    warnx("e - line %d of \"%s\", illegal $-name", a_lineno, input_file_name);
    print_pos(a_line, a_cptr);
    exit(1);
}


void
untyped_lhs(void)
{
    errx(1, "e - line %d of \"%s\", $$ is untyped", lineno, input_file_name);
}


void
untyped_rhs(int i, const char *s)
{
    errx(1, "e - line %d of \"%s\", $%d (%s) is untyped",
	    lineno, input_file_name, i, s);
}


void
unknown_rhs(int i)
{
    errx(1, "e - line %d of \"%s\", $%d is untyped", lineno, input_file_name, i);
}


void
default_action_warning(void)
{
    warnx("w - line %d of \"%s\", the default action assigns an \
undefined value to $$", lineno, input_file_name);
}


void
undefined_goal(const char *s)
{
    errx(1, "e - the start symbol %s is undefined", s);
}


void
undefined_symbol_warning(const char *s)
{
    warnx("w - the symbol %s is undefined", s);
}
