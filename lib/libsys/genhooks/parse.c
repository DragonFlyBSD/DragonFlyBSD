/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/lib/libsys/genhooks/parse.c,v 1.1 2005/05/08 18:14:56 dillon Exp $
 */
/*
 * PARSE.C
 *
 * Parse the system call configuration file.
 */

#include "defs.h"

struct sys_info **sys_array;
int sys_count;
char *sys_sectname;

static int parse_base(lex_token *tok);
static int parse_add(lex_token *tok);
static int parse_int(lex_token *tok, int *ret_p);
static int parse_type(lex_token *tok, sys_type **type_pp);

void
parse_file(const char *path)
{
    lex_token tok;
    int t;

    lex_open(path, &tok);
    t = lex_gettoken(&tok);
    while (t != TOK_EOF) {
	switch(t) {
	case TOK_BASE:
	    t = parse_base(&tok);
	    break;
	case TOK_ADD:
	    t = parse_add(&tok);
	    break;
	default:
	    lex_error(&tok, "Expected command directive");
	    exit(1);
	}
    }
    lex_close(&tok);
}

int
parse_base(lex_token *tok)
{
    int t;

    t = lex_gettoken(tok);
    if (t & TOK_SYMBOL) {
	sys_sectname = lex_string(tok);
	t = lex_gettoken(tok);
    } else {
	lex_error(tok, "Expected section extension symbol");
	exit(1);
    }
    t = lex_skip_token(tok, TOK_SEMI);
    return(t);
}

static int
parse_add(lex_token *tok)
{
    sys_info *info;
    sys_type *type;
    int sysno;
    int t;

    lex_gettoken(tok);
    info = zalloc(sizeof(sys_info));

    parse_int(tok, &info->sysno);

    t = lex_skip_token(tok, TOK_OBRACE);

    while (t != TOK_CBRACE) {
	switch(t) {
	case TOK_FUNCTION:
	    t = lex_gettoken(tok);
	    parse_type(tok, &info->func_ret);
	    t = lex_skip_token(tok, TOK_OPAREN);
	    while (t != TOK_CPAREN) {
		t = parse_type(tok, &type);
		if (t != TOK_COMMA)
		    break;
		t = lex_gettoken(tok);
		info->func_args = realloc(info->func_args, 
				    sizeof(sys_type) * (info->nargs + 1));
		info->func_args[info->nargs++] = type;
	    }

	    /*
	     * cleanup void
	     */
	    if (info->nargs == 1 && 
		strcmp(info->func_args[0]->type_name, "void") == 0
	    ) {
		info->nargs = 0;
		/* XXX free/cleanup */
	    }

	    t = lex_skip_token(tok, TOK_CPAREN);
	    t = lex_skip_token(tok, TOK_SEMI);
	    break;
	case TOK_IMPLEMENTATION:
	    t = lex_gettoken(tok);
	    switch(t) {
	    case TOK_DIRECT:
		t = lex_gettoken(tok);
		break;
	    default:
		lex_error(tok, "Expected 'direct'");
		exit(1);
	    }
	    t = lex_skip_token(tok, TOK_SEMI);
	    break;
	default:
	    lex_error(tok, "Expected command directive");
	    exit(1);
	}
    }
    t = lex_skip_token(tok, TOK_CBRACE);
    if (sys_count <= info->sysno) {
	sys_array = realloc(sys_array, 
				sizeof(sys_info *) * (info->sysno + 1));
	while (sys_count <= info->sysno)
	    sys_array[sys_count++] = NULL;
    }
    sys_array[info->sysno] = info;
    return(t);
}

static
int
parse_int(lex_token *tok, int *ret_p)
{
    int t = tok->type;

    if (t != TOK_INTEGER) {
	lex_error(tok, "Expected integer");
	exit(1);
    }
    *ret_p = tok->value;
    return(lex_gettoken(tok));
}

static
int
parse_type(lex_token *tok, sys_type **type_pp)
{
    int t = tok->type;
    sys_type *type;

    type = zalloc(sizeof(sys_type));

    if ((t & TOK_SYMBOL) == 0) {
	lex_error(tok, "Expected type identifier");
	exit(1);
    }
    type->type_name = lex_string(tok);
    t = lex_gettoken(tok);
    if (t != TOK_COMMA && t != TOK_CPAREN) {
	if ((t & TOK_SYMBOL) == 0) {
	    lex_error(tok, "Expected name identifier");
	    exit(1);
	}
	type->var_name = lex_string(tok);
	t = lex_gettoken(tok);
    } else {
	type->var_name = NULL;
    }
    *type_pp = type;
    return(t);
}

