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
 * $DragonFly: src/lib/libsys/genhooks/lex.c,v 1.1 2005/05/08 18:14:56 dillon Exp $
 */
/*
 * LEX.C
 *
 * Lexical tokenizer for the parser.
 */

#include "defs.h"

static int lex_keyword(const char *ptr, int len);

void
lex_open(const char *path, lex_token *tok)
{
    struct stat st;
    lex_info *lex;
    int fd;

    lex = zalloc(sizeof(*lex));
    bzero(tok, sizeof(lex_token));
    lex->fd = open(path, O_RDONLY);
    if (lex->fd < 0) {
	err(1, "unable to open %s", path);
	/* not reached */
    }
    if (fstat(lex->fd, &st) < 0) {
	err(1, "unable to stat %s", path);
	/* not reached */
    }
    lex->path = strdup(path);
    lex->size = st.st_size;
    lex->base = mmap(NULL, lex->size, PROT_READ, MAP_SHARED, lex->fd, 0);
    lex->cache_line = 1;
    if (lex->base == MAP_FAILED) {
	err(1, "unable to mmap %s", path);
	/* not reached */
    }

    tok->info = lex;
}

void
lex_close(lex_token *tok)
{
    lex_info *lex = tok->info;

    assert(lex->fd >= 0);
    close(lex->fd);
    munmap((void *)lex->base, lex->size);
    lex->fd = -1;
    lex->base = NULL;
    tok->info = NULL;
    free(lex->path);
    free(lex);
}

int
lex_gettoken(lex_token *tok)
{
    lex_info *lex = tok->info;
    int b = tok->index + tok->len;
    int i = b;
    char c;

    tok->type = TOK_EOF;
    while (i < lex->size) {
	c = lex->base[i];

	switch(c) {
	case '\n':
	case ' ':
	case '\t':
	    ++b;
	    ++i;
	    break;
	case '#':
	    while (i < lex->size && lex->base[i] != '\n')
		++i;
	    b = i;
	    break;
	case ';':
	case ',':
	case '(':
	case ')':
	case '{':
	case '}':
	    ++i;
	    tok->type = c;
	    goto done;
	default:
	    if (c >= '0' && c <= '9') {
		tok->type = TOK_INTEGER;
		tok->value = 0;
		while (i < lex->size) {
		    c = lex->base[i];
		    if (c < '0' || c > '9')
			break;
		    tok->value = tok->value * 10 + (c - '0');
		    ++i;
		}
		goto done;
	    }
	    if (c == '_' || isalpha(c)) {
		while (i < lex->size) {
		    c = lex->base[i];
		    if (c != '_' && isalnum(c) == 0)
			break;
		    ++i;
		}
		tok->type = lex_keyword(lex->base + b, i - b);
		goto done;
	    }
	    tok->type = TOK_UNKNOWN;
	    ++i;
	    goto done;
	}
    }
done:
    tok->index = b;
    tok->sym = lex->base + b;
    tok->len = i - b;
    return(tok->type);
}

static int
lex_keyword(const char *ptr, int len)
{
    if (len == 4 && strncasecmp(ptr, "base", 4) == 0)
	return(TOK_BASE);
    if (len == 3 && strncasecmp(ptr, "add", 3) == 0)
	return(TOK_ADD);
    if (len == 8 && strncasecmp(ptr, "function", 8) == 0)
	return(TOK_FUNCTION);
    if (len == 14 && strncasecmp(ptr, "implementation", 14) == 0)
	return(TOK_IMPLEMENTATION);
    if (len == 6 && strncasecmp(ptr, "direct", 6) == 0)
	return(TOK_DIRECT);
    if (len == 9 && strncasecmp(ptr, "simulated", 9) == 0)
	return(TOK_SIMULATED);
    return(TOK_SYMBOL);
}

const char *
lex_string_quick(lex_token *tok)
{
    lex_info *lex = tok->info;
    static char save_buf[64];
    static char *save_str = save_buf;

    if (save_str != save_buf)
	free(save_str);
    if (tok->len < sizeof(save_buf)) 
	save_str = save_buf;
    else
	save_str = malloc(tok->len + 1);
    bcopy(lex->base + tok->index, save_str, tok->len);
    save_str[tok->len] = 0;
    return(save_str);
}

char *
lex_string(lex_token *tok)
{
    lex_info *lex = tok->info;
    char *ptr;

    ptr = malloc(tok->len + 1);
    bcopy(lex->base + tok->index, ptr, tok->len);
    ptr[tok->len] = 0;
    return(ptr);
}

int
lex_skip_token(lex_token *tok, int type)
{
    if (tok->type != type) {
	if (type < 0x0100)
	    lex_error(tok, "Unexpected token, expected '%c'", type);
	else
	    lex_error(tok, "Unexpected token");
	exit(1);
    }
    return(lex_gettoken(tok));
}

void
lex_error(lex_token *tok, const char *ctl, ...)
{
    lex_info *lex = tok->info;
    va_list va;
    int i;
    int j;

    /*
     * Locate the line and line number containing the error
     */
    while (lex->cache_index > tok->index) {
	--lex->cache_line;
	for (i = lex->cache_index - 1; i > 0; --i) {
	    if (lex->base[i - 1] == '\n')
		break;
	}
	lex->cache_len = lex->cache_index - i;
	lex->cache_index = i;
    }
    for (i = lex->cache_index; i <= lex->size; ++i) {
	if (i == lex->size) {
	    lex->cache_len = i - lex->cache_index;
	    break;
	}
	if (lex->base[i] == '\n') {
	    if (tok->index <= i) {
		lex->cache_len = i + 1 - lex->cache_index;
		break;
	    }
	    lex->cache_index = i + 1;
	    ++lex->cache_line;
	}
    }

    /*
     * Pretty print.
     */
    fprintf(stderr, "line %d of %s, ", lex->cache_line, lex->path);
    va_start(va, ctl);
    vfprintf(stderr, ctl, va);
    va_end(va);
    fprintf(stderr, ":\n");

    i = tok->index - lex->cache_index;
    j = (lex->cache_index + lex->cache_len) - (tok->index + tok->len); 
    fprintf(stderr, "%*.*s", i, i, lex->base + lex->cache_index);
    fprintf(stderr, "\033[7m%*.*s\033[0m", tok->len, tok->len, lex->base + tok->index);
    fprintf(stderr, "%*.*s", j, j, lex->base + tok->index + tok->len);
}

