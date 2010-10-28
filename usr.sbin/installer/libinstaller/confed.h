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
 * confed.h
 * $Id: confed.h,v 1.5 2005/02/06 21:05:18 cpressey Exp $
 */

#ifndef __CONFED_H_
#define __CONFED_H_

#include "libaura/dict.h"

struct i_fn_args;

#define CONFIG_TYPE_SH		0
#define CONFIG_TYPE_RESOLV	1

struct config_vars {
	struct aura_dict *d;
};

struct config_vars	*config_vars_new(void);
void			 config_vars_free(struct config_vars *);
int			 config_var_set(struct config_vars *, const char *, const char *);
const char		*config_var_get(const struct config_vars *, const char *);
int			 config_vars_write(const struct config_vars *, int,
			     const char *, ...) __printflike(3, 4);
int			 config_vars_read(struct i_fn_args *,
			     struct config_vars *, int, const char *, ...)
			     __printflike(4, 5);

#endif /* !__CONFED_H_ */
