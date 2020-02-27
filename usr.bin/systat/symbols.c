/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
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
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <ctype.h>
#include <devinfo.h>
#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <evtr.h>
#include <stdarg.h>
#include "symbols.h"

struct symdata {
	RB_ENTRY(symdata) node;
	const char *symname;
	char *symaddr_beg;
	char *symaddr_end;
	char symtype;
};

static int symdata_cmp(struct symdata *s1, struct symdata *s2);

RB_HEAD(symdata_rbtree, symdata);
RB_PROTOTYPE3(symdata_rbtree, symdata, node, symdata_cmp, char *);
RB_GENERATE3(symdata_rbtree, symdata, node, symdata_cmp, char *,
		symaddr_beg, symaddr_end);

static struct symdata_rbtree symroot = RB_INITIALIZER(symroot);
static char *symbegin = (void *)(intptr_t)0;
static char *symend = (void *)(intptr_t)-1;


void
read_symbols(const char *file)
{
	char buf[256];
	char cmd[256];
	size_t buflen = sizeof(buf);
	FILE *fp;
	struct symdata *lsym;
	struct symdata *sym;
	char *s1;
	char *s2;
	char *s3;

	RB_INIT(&symroot);

	if (file == NULL) {
		if (sysctlbyname("kern.bootfile", buf, &buflen, NULL, 0) < 0)
			file = "/boot/kernel";
		else
			file = buf;
	}
	snprintf(cmd, sizeof(cmd), "nm -n %s", file);
	if ((fp = popen(cmd, "r")) != NULL) {
		lsym = NULL;
		while (fgets(buf, sizeof(buf), fp) != NULL) {
		    s1 = strtok(buf, " \t\n");
		    s2 = strtok(NULL, " \t\n");
		    s3 = strtok(NULL, " \t\n");
		    if (s1 && s2 && s3) {
			sym = malloc(sizeof(struct symdata));
			sym->symaddr_beg = (char *)strtoul(s1, NULL, 16);
			sym->symaddr_end = sym->symaddr_beg;
			if (lsym)
				lsym->symaddr_end = sym->symaddr_beg - 1;
			sym->symtype = s2[0];
			sym->symname = strdup(s3);
			if (strcmp(s3, "kernbase") == 0)
				symbegin = sym->symaddr_beg;
			if (strcmp(s3, "end") == 0)
				symend = sym->symaddr_beg;
			RB_INSERT(symdata_rbtree, &symroot, sym);
			lsym = sym;
		    }
		}
		pclose(fp);
	}
}

const char *
address_to_symbol(void *kptr, struct save_ctx *ctx)
{
	static struct symdata *sym;
	char *buf = ctx->save_buf;
	int size = sizeof(ctx->save_buf);

	sym = RB_RLOOKUP(symdata_rbtree, &symroot, (char *)kptr);
	if (sym) {
		snprintf(buf, size, "%s+%d", sym->symname,
			(int)((char *)kptr - sym->symaddr_beg));
	} else {
		snprintf(buf, size, "%p", kptr);
	}
	return(buf);
}

static int
symdata_cmp(struct symdata *s1, struct symdata *s2)
{
	if (s1->symaddr_beg < s2->symaddr_beg)
		return -1;
	if (s1->symaddr_beg > s2->symaddr_beg)
		return 1;
	return 0;
}
