/*
 * Copyright (c)2004 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of the DragonFly Project nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * commands.h
 * $Id: commands.h,v 1.14 2005/02/06 21:05:18 cpressey Exp $
 */

#include <stdio.h>

#include "libdfui/dfui.h"

#ifndef __COMMANDS_H_
#define __COMMANDS_H_

#include "diskutil.h"

/*** TYPES ***/

#include "functions.h"

struct commands;
struct command;

#ifdef NEEDS_COMMANDS_STRUCTURE_DEFINITIONS

struct commands {
	struct command *head;
	struct command *tail;
};

struct command {
	struct command *next;
	struct command *prev;
	char *cmdline;		/* command line to execute */
	char *desc;		/* description displayed in progress bar */
	int log_mode;		/* use a COMMAND_LOG_* constant here */
	int failure_mode;	/* use a COMMAND_FAILURE_* constant here */
	char *tag;		/* tag which is recorded on failure */
	int result;		/* result code from executing command */
	char *output;		/* output of command */
};

#endif

#define	COMMAND_LOG_SILENT	0
#define	COMMAND_LOG_QUIET	1
#define	COMMAND_LOG_VERBOSE	2

#define	COMMAND_FAILURE_IGNORE	0
#define	COMMAND_FAILURE_WARN	1
#define	COMMAND_FAILURE_ABORT	2

#define COMMAND_RESULT_NEVER_EXECUTED	 -1
#define COMMAND_RESULT_POPEN_ERR	256
#define COMMAND_RESULT_SELECT_ERR	257
#define COMMAND_RESULT_CANCELLED	512
#define COMMAND_RESULT_SKIPPED		513

/*** PROTOTYPES ***/

struct commands	*commands_new(void);
struct command	*command_add(struct commands *, const char *, ...)
		     __printflike(2, 3);

void		 command_set_log_mode(struct command *, int);
void		 command_set_failure_mode(struct command *, int);
void		 command_set_desc(struct command *, const char *, ...)
		     __printflike(2, 3);
void		 command_set_tag(struct command *, const char *, ...)
		     __printflike(2, 3);

struct command	*command_get_first(const struct commands *);
struct command	*command_get_next(const struct command *);

char		*command_get_cmdline(const struct command *);
char		*command_get_tag(const struct command *);
int		 command_get_result(const struct command *);

void		 commands_preview(struct dfui_connection *, const struct commands *);
int		 commands_execute(struct i_fn_args *, struct commands *);

void		 commands_free(struct commands *);

void		 view_command_log(struct i_fn_args *);

/* Command Generators */

void		 unmount_all_under(struct i_fn_args *, struct commands *,
		     const char *, ...) __printflike(3, 4);

#endif /* !__COMMANDS_H_ */
