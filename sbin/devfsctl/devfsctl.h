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

#ifndef DEVFSCTL_H
#define DEVFSCTL_H

#include <sys/queue.h>

#define iswhitespace(X)	((((X) == ' ') || ((X) == '\t'))?1:0)
#define RULE_MAX_STACK	32
#define RULES_FILE		0x01

#if 0
#define RULETAB_FILE	0x02
#define RULETAB_ALL		0x01
#define RULETAB_ONLY_BOOT	0x02
#define RULETAB_SPECIFIC	0x03
#endif

struct groupdevid {
	enum {
		isGROUP = 1,
		isNAME,
		isTYPE
	}		type;

	union {
		char		*name;
		int		devtype;
	};

	struct groupdevid **list;
	size_t	listsize;
#if 0
    struct groupdevid *next;
	TAILQ_HEAD(, groupdevid) list;
#endif
	TAILQ_ENTRY(groupdevid) link;
};

struct rule {
	enum {
		rPERM = 1,
		rLINK,
		rHIDE,
		rSHOW
	}		type;

	struct groupdevid *id;
	char		*dest;
	uid_t		uid;
	uid_t		gid;
	int		mode;
	int		jail;

	TAILQ_ENTRY(rule) link;
};

#if 0
struct rule_tab {
	const char	*mntpoint;
	const char	*rule_file;
	TAILQ_ENTRY(rule_tab) link;
};
#endif

typedef int (rule_iterate_callback_t)(struct rule *rule,
		struct groupdevid *id);
typedef int (rule_parser_t)(char **);

struct groupdevid *new_id(const char *, int);
struct groupdevid *get_id(const char *);
struct groupdevid *get_group(const char *, int);
struct rule *new_rule(int, struct groupdevid *);
void add_rule(struct rule *);
int rule_apply(void);
void dump_config(void);
int read_config(const char *, int);
void syntax_error(const char *fmt, ...) __dead2 __printflike(1, 2);
void rule_tab(void);
void delete_rules(void);
#endif
