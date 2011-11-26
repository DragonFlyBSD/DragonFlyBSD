/*
 * gripes.h
 *
 * Copyright (c) 1990, 1991, John W. Eaton.
 *
 * You may distribute under the terms of the GNU General Public
 * License as specified in the file COPYING that comes with the man
 * distribution.
 *
 * John W. Eaton
 * jwe@che.utexas.edu
 * Department of Chemical Engineering
 * The University of Texas at Austin
 * Austin, Texas  78712
 */

extern void gripe_no_name (char *);
extern void gripe_converting_name (char *, int);
extern void gripe_system_command (int);
extern void gripe_reading_man_file (char *);
extern void gripe_not_found (char *, char *);
extern void gripe_invalid_section (char *);
extern void gripe_manpath (void);
extern void gripe_alloc (int, const char *);
extern void gripe_incompatible (const char *);
extern void gripe_getting_mp_config (char *);
extern void gripe_reading_mp_config (char *);
extern void gripe_roff_command_from_file (char *);
extern void gripe_roff_command_from_env (void);
extern void gripe_roff_command_from_command_line (void);
