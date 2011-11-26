/*
 * util.h
 *
 * Copyright (c) 2010, Sascha Wildner
 *
 * You may distribute under the terms of the GNU General Public
 * License as specified in the file COPYING that comes with the man
 * distribution.
 */

extern int do_system_command(char *);
extern void downcase(unsigned char *);
extern int is_directory(char *);
extern int is_newer(char *, char *);
extern char *mkprogname(char *);
