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
 * fn.h
 * $Id: fn.h,v 1.3 2005/02/07 06:46:20 cpressey Exp $
 */

#ifndef __FN_H_
#define __FN_H_

#include "libaura/dict.h"
#include "libdfui/dfui.h"

#include "libinstaller/functions.h"

/*** PROTOTYPES ***/

/* General fn_ functions */

void		 fn_select_disk(struct i_fn_args *);
void		 fn_select_slice(struct i_fn_args *);
void		 fn_get_passphrase(struct i_fn_args *);

/* Configure an Installed System (only) */

void 		 fn_root_passwd(struct i_fn_args *);
void 		 fn_add_user(struct i_fn_args *);
void		 fn_install_packages(struct i_fn_args *);
void		 fn_remove_packages(struct i_fn_args *);
void		 fn_select_services(struct i_fn_args *);

int		 mount_target_system(struct i_fn_args *);

/* Configure the LiveCD Environment -or- an Installed System */

void		 fn_assign_datetime(struct i_fn_args *);
void		 fn_set_timezone(struct i_fn_args *);
void 		 fn_set_kbdmap(struct i_fn_args *);
void 		 fn_set_vidfont(struct i_fn_args *);
void 		 fn_set_scrnmap(struct i_fn_args *);
void		 fn_assign_hostname_domain(struct i_fn_args *);
void		 fn_assign_ip(struct i_fn_args *);

/* LiveCD Utilities: Diagnostics */

void		 fn_show_dmesg(struct i_fn_args *);
void		 fn_show_pciconf(struct i_fn_args *);
void		 fn_show_natacontrol(struct i_fn_args *);

void		 show_ifconfig(struct dfui_connection *, char *);

/* LiveCD Utilties: Disk Utilities */

char		*fn_select_file(const char *, const char *, const char *,
				const char *, const char *, const char *,
				const struct i_fn_args *);

void		 fn_format_disk_mbr(struct i_fn_args *);
void		 fn_format_disk_uefi(struct i_fn_args *);
void		 fn_install_bootblocks(struct i_fn_args *, const char *device);
void		 fn_wipe_start_of_disk(struct i_fn_args *);
void		 fn_wipe_start_of_slice(struct i_fn_args *);
void		 fn_format_msdos_floppy(struct i_fn_args *);
void		 fn_create_cdboot_floppy(struct i_fn_args *);

int		 format_slice(struct i_fn_args *);

void		 fn_create_subpartitions_ufs(struct i_fn_args *);
void		 fn_create_subpartitions_hammer(int which, struct i_fn_args *);
void		 fn_install_os(struct i_fn_args *);

/* Global variables */

extern struct	config_vars *rc_conf;

#endif /* !__FN_H_ */
