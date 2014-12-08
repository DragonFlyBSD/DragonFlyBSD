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
 * functions.h
 * $Id: functions.h,v 1.22 2005/02/06 21:05:18 cpressey Exp $
 */

#ifndef __FUNCTIONS_H_
#define __FUNCTIONS_H_

#include <stdio.h>

#include "libaura/dict.h"
#include "libdfui/dfui.h"

#include "confed.h"

struct storage;

/*
 * Function argument flags
 */
#define I_BOOTED_LIVECD		0x1
#define I_UPGRADE_TOOGLE	0x2

/*
 * Installer function arguments.
 */
struct i_fn_args {
	struct dfui_connection *c;	/* DFUI connection to f/e */
	struct storage *s;		/* description of system storage */
	const char *os_root;		/* where livecd files live */
	const char *cfg_root;		/* where target conf files live */
	const char *tmp;		/* directory for temporary files */
	const char *name;		/* overrides title of form */
	const char *short_desc;		/* overrides short desc */
	const char *long_desc;		/* overrides help text */
	const char *cancel_desc;	/* overrides cancel button */
	int result;			/* result of function */
	FILE *log;			/* file to log to */
	struct aura_dict *temp_files;	/* names of files to delete on exit */
	struct config_vars *cmd_names;	/* names (and paths) of commands to use */
	int flags;			/* Option flags */
};

/*** PROTOTYPES ***/

/* Installer Context */

struct i_fn_args *i_fn_args_new(const char *, const char *, int, const char *);
void		 i_fn_args_free(struct i_fn_args *);

void		 i_log(struct i_fn_args *, const char *, ...)
		     __printflike(2, 3);

/* General Utilities */

void		 abort_backend(void);
int		 assert_clean(struct dfui_connection *, const char *, const char *, const char *);
int		 hex_to_int(const char *, int *);
int		 first_non_space_char_is(const char *, char);
const char	*capacity_to_string(long);
int		 string_to_capacity(const char *, long *);
unsigned long	 next_power_of_two(unsigned long);
char		*filename_noext(const char *);

/* Temp Files */

int		 temp_file_add(struct i_fn_args *, const char *);
int		 temp_files_clean(struct i_fn_args *);

/* Command Names */

const char	*cmd_name(const struct i_fn_args *, const char *);

#endif /* !__FUNCTIONS_H_ */
