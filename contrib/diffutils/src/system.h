/* System dependent declarations.

   Copyright (C) 1988-1989, 1992-1995, 1998, 2001-2002, 2004, 2006, 2009-2010
   Free Software Foundation, Inc.

   This file is part of GNU DIFF.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <config.h>

/* Use this to suppress gcc's `...may be used before initialized' warnings. */
#ifdef lint
# define IF_LINT(Code) Code
#else
# define IF_LINT(Code) /* empty */
#endif

/* Define `__attribute__' and `volatile' first
   so that they're used consistently in all system includes.  */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 6) || __STRICT_ANSI__
# define __attribute__(x)
#endif

#include <verify.h>

#include <sys/types.h>

#include <sys/stat.h>
#include "stat-macros.h"

#ifndef STAT_BLOCKSIZE
# if HAVE_STRUCT_STAT_ST_BLKSIZE
#  define STAT_BLOCKSIZE(s) ((s).st_blksize)
# else
#  define STAT_BLOCKSIZE(s) (8 * 1024)
# endif
#endif

#include <unistd.h>

#include <fcntl.h>
#include <time.h>

#include <sys/wait.h>
#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val) ((unsigned int) (stat_val) >> 8)
#endif
#ifndef WIFEXITED
# define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

#include <dirent.h>
#ifndef _D_EXACT_NAMLEN
# define _D_EXACT_NAMLEN(dp) strlen ((dp)->d_name)
#endif

#include <stdlib.h>
#define EXIT_TROUBLE 2

#include <limits.h>
#include <locale.h>
#include <stddef.h>
#include <inttypes.h>

#include <string.h>
#if ! HAVE_STRCASECOLL
# if HAVE_STRICOLL || defined stricoll
#  define strcasecoll(a, b) stricoll (a, b)
# else
#  define strcasecoll(a, b) strcasecmp (a, b) /* best we can do */
# endif
#endif
#if ! (HAVE_STRCASECMP || defined strcasecmp)
int strcasecmp (char const *, char const *);
#endif

#include <gettext.h>
#if ! ENABLE_NLS
# undef textdomain
# define textdomain(Domainname) /* empty */
# undef bindtextdomain
# define bindtextdomain(Domainname, Dirname) /* empty */
#endif

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include <ctype.h>

/* ISDIGIT differs from isdigit, as follows:
   - Its arg may be any int or unsigned int; it need not be an unsigned char.
   - It's guaranteed to evaluate its argument exactly once.
   - It's typically faster.
   POSIX 1003.1-2001 says that only '0' through '9' are digits.
   Prefer ISDIGIT to isdigit unless it's important to use the locale's
   definition of `digit' even when the host does not conform to POSIX.  */
#define ISDIGIT(c) ((unsigned int) (c) - '0' <= 9)

#include <errno.h>

#include <signal.h>
#ifndef SA_RESTART
# ifdef SA_INTERRUPT /* e.g. SunOS 4.1.x */
#  define SA_RESTART SA_INTERRUPT
# else
#  define SA_RESTART 0
# endif
#endif
#if !defined SIGCHLD && defined SIGCLD
# define SIGCHLD SIGCLD
#endif

#undef MIN
#undef MAX
#define MIN(a, b) ((a) <= (b) ? (a) : (b))
#define MAX(a, b) ((a) >= (b) ? (a) : (b))

#include <stdbool.h>

#if HAVE_VFORK_H
# include <vfork.h>
#endif

#if ! HAVE_WORKING_VFORK
# define vfork fork
#endif

#include <intprops.h>
#include "propername.h"

/* Type used for fast comparison of several bytes at a time.  */

#ifndef word
# define word uintmax_t
#endif

/* The integer type of a line number.  Since files are read into main
   memory, ptrdiff_t should be wide enough.  */

typedef ptrdiff_t lin;
#define LIN_MAX PTRDIFF_MAX
verify (TYPE_SIGNED (lin));
verify (sizeof (ptrdiff_t) <= sizeof (lin));
verify (sizeof (lin) <= sizeof (long int));

/* This section contains POSIX-compliant defaults for macros
   that are meant to be overridden by hand in config.h as needed.  */

#ifndef file_name_cmp
# define file_name_cmp strcmp
#endif

#ifndef initialize_main
# define initialize_main(argcp, argvp)
#endif

#ifndef NULL_DEVICE
# define NULL_DEVICE "/dev/null"
#endif

/* Do struct stat *S, *T describe the same special file?  */
#ifndef same_special_file
# if HAVE_ST_RDEV && defined S_ISBLK && defined S_ISCHR
#  define same_special_file(s, t) \
     (((S_ISBLK ((s)->st_mode) && S_ISBLK ((t)->st_mode)) \
       || (S_ISCHR ((s)->st_mode) && S_ISCHR ((t)->st_mode))) \
      && (s)->st_rdev == (t)->st_rdev)
# else
#  define same_special_file(s, t) 0
# endif
#endif

/* Do struct stat *S, *T describe the same file?  Answer -1 if unknown.  */
#ifndef same_file
# define same_file(s, t) \
    ((((s)->st_ino == (t)->st_ino) && ((s)->st_dev == (t)->st_dev)) \
     || same_special_file (s, t))
#endif

/* Do struct stat *S, *T have the same file attributes?

   POSIX says that two files are identical if st_ino and st_dev are
   the same, but many file systems incorrectly assign the same (device,
   inode) pair to two distinct files, including:

   - GNU/Linux NFS servers that export all local file systems as a
     single NFS file system, if a local device number (st_dev) exceeds
     255, or if a local inode number (st_ino) exceeds 16777215.

   - Network Appliance NFS servers in snapshot directories; see
     Network Appliance bug #195.

   - ClearCase MVFS; see bug id ATRia04618.

   Check whether two files that purport to be the same have the same
   attributes, to work around instances of this common bug.  Do not
   inspect all attributes, only attributes useful in checking for this
   bug.

   It's possible for two distinct files on a buggy file system to have
   the same attributes, but it's not worth slowing down all
   implementations (or complicating the configuration) to cater to
   these rare cases in buggy implementations.  */

#ifndef same_file_attributes
# define same_file_attributes(s, t) \
   ((s)->st_mode == (t)->st_mode \
    && (s)->st_nlink == (t)->st_nlink \
    && (s)->st_uid == (t)->st_uid \
    && (s)->st_gid == (t)->st_gid \
    && (s)->st_size == (t)->st_size \
    && (s)->st_mtime == (t)->st_mtime \
    && (s)->st_ctime == (t)->st_ctime)
#endif

#define STREQ(a, b) (strcmp (a, b) == 0)
