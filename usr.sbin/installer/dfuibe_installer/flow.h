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
 * flow.h
 * $Id: flow.h,v 1.10 2005/03/20 18:40:40 den Exp $
 */

#ifndef __FLOW_H_
#define __FLOW_H_

#define	DISK_MIN	4096
#define	HAMMER_WARN	51200
#define	HAMMER_MIN	10240

#define	SWAP_MIN	256	/* suggested minimum */
#define	SWAP_MAX	524288
#define BUILD_MIN	10240	/* suggested */
#define BUILD_MAX	20480	/* suggested */

struct i_fn_args;

extern int use_hammer;		/* 0=UFS 1=HAMMER 2=HAMMER2 */
extern int use_uefi;
extern int during_install;

/*** PROTOTYPES ***/

/* Menus */

#ifdef ENABLE_NLS
void		 state_lang_menu(struct i_fn_args *);
#endif
void		 state_welcome(struct i_fn_args *);
void		 state_welcome_system(struct i_fn_args *);
void 		 state_configure_menu(struct i_fn_args *);
void 		 state_utilities_menu(struct i_fn_args *);

void 		 state_environment_menu(struct i_fn_args *);
void 		 state_diagnostics_menu(struct i_fn_args *);
void 		 state_diskutil_menu(struct i_fn_args *);

/* Install */

void		 state_begin_install(struct i_fn_args *);
void		 state_begin_upgrade(struct i_fn_args *);
void		 state_ask_uefi(struct i_fn_args *);
void		 state_select_disk(struct i_fn_args *);
void		 state_ask_fs(struct i_fn_args *);
void		 state_format_disk(struct i_fn_args *);
void		 state_select_slice(struct i_fn_args *);
void		 state_create_subpartitions(struct i_fn_args *);
void		 state_install_os(struct i_fn_args *);
void		 state_install_bootstrap(struct i_fn_args *);
void		 state_finish_install(struct i_fn_args *);
void		 state_reboot(struct i_fn_args *);
void		 state_setup_remote_installation_server(struct i_fn_args *);

/* Entry Point */

int		 flow(int, char *, char *, int);

#endif /* !__FLOW_H_ */
