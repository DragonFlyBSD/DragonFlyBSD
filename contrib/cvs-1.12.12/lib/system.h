/* system-dependent definitions for CVS.
   Copyright (C) 1989-1992 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.  */

/***
 *** Begin the default set of autoconf includes.
 ***/

/* Headers assumed for C89 freestanding compilers.  See HACKING for more.  */
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>

/* C89 hosted headers assumed since they were included in UNIX version 7.
 * See HACKING for more.
 */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>

/* C89 hosted headers we _think_ GCC supplies even on freestanding systems.
 * If we find any systems which do not have them, a replacement header should
 * be discussed with the GNULIB folks.
 *
 * For more information, please see the `Portability' section of the `HACKING'
 * file.
 */
#include <stdlib.h>
#include <string.h>

/* We assume this because it has been around forever despite not being a part
 * of any of the other standards we assume conformance to.  So far this hasn't
 * been a problem.
 *
 * For more information, please see the `Portability' section of the `HACKING'
 * file.
 */
#include <sys/types.h>

/* A GNULIB replacement for this C99 header is supplied when it is missing.
 * See the comments in stdbool_.h for its limitations.
 */
#include <stdbool.h>

/* Ditto for these POSIX.2 headers.  */
#include <fnmatch.h>
#include <getopt.h>	/* Has GNU extensions,  */



#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif /* HAVE_SYS_STAT_H */
#if !STDC_HEADERS && HAVE_MEMORY_H
# include <memory.h>
#endif /* !STDC_HEADERS && HAVE_MEMORY_H */
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else /* ! HAVE_INTTYPES_H */
# if HAVE_STDINT_H
#  include <stdint.h>
# endif /* HAVE_STDINT_H */
#endif /* HAVE_INTTYPES_H */
#if HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */
/* End the default set of autoconf includes */

/* Assume these headers. */
#include <pwd.h>

/* More GNULIB includes */
/* This include enables the use of the *_unlocked IO functions from glibc. */
#include "unlocked-io.h"

/* For struct timespec.  */
#include "timespec.h"

/* This is a replacement stub for gettext provided by GNULIB when gettext is
 * not available.
 */
#include <gettext.h>
/* End GNULIB includes */

#ifdef STAT_MACROS_BROKEN
#undef S_ISBLK
#undef S_ISCHR
#undef S_ISDIR
#undef S_ISREG
#undef S_ISFIFO
#undef S_ISLNK
#undef S_ISSOCK
#undef S_ISMPB
#undef S_ISMPC
#undef S_ISNWK
#endif

/* Not all systems have S_IFMT, but we want to use it if we have it.
   The S_IFMT code below looks right (it masks and compares).  The
   non-S_IFMT code looks bogus (are there really systems on which
   S_IFBLK, S_IFLNK, &c, each have their own bit?  I suspect it was
   written for OS/2 using the IBM C/C++ Tools 2.01 compiler).

   Of course POSIX systems will have S_IS*, so maybe the issue is
   semi-moot.  */

#if !defined(S_ISBLK) && defined(S_IFBLK)
# if defined(S_IFMT)
# define	S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
# else
# define S_ISBLK(m) ((m) & S_IFBLK)
# endif
#endif

#if !defined(S_ISCHR) && defined(S_IFCHR)
# if defined(S_IFMT)
# define	S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
# else
# define S_ISCHR(m) ((m) & S_IFCHR)
# endif
#endif

#if !defined(S_ISDIR) && defined(S_IFDIR)
# if defined(S_IFMT)
# define	S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
# else
# define S_ISDIR(m) ((m) & S_IFDIR)
# endif
#endif

#if !defined(S_ISREG) && defined(S_IFREG)
# if defined(S_IFMT)
# define	S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
# else
# define S_ISREG(m) ((m) & S_IFREG)
# endif
#endif

#if !defined(S_ISFIFO) && defined(S_IFIFO)
# if defined(S_IFMT)
# define	S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
# else
# define S_ISFIFO(m) ((m) & S_IFIFO)
# endif
#endif

#if !defined(S_ISLNK) && defined(S_IFLNK)
# if defined(S_IFMT)
# define	S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
# else
# define S_ISLNK(m) ((m) & S_IFLNK)
# endif
#endif

#ifndef S_ISSOCK
# if defined( S_IFSOCK )
#   ifdef S_IFMT
#     define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#   else
#     define S_ISSOCK(m) ((m) & S_IFSOCK)
#   endif /* S_IFMT */
# elif defined( S_ISNAM )
    /* SCO OpenServer 5.0.6a */
#   define S_ISSOCK S_ISNAM
# endif /* !S_IFSOCK && S_ISNAM */
#endif /* !S_ISSOCK */

#if !defined(S_ISMPB) && defined(S_IFMPB) /* V7 */
# if defined(S_IFMT)
# define S_ISMPB(m) (((m) & S_IFMT) == S_IFMPB)
# define S_ISMPC(m) (((m) & S_IFMT) == S_IFMPC)
# else
# define S_ISMPB(m) ((m) & S_IFMPB)
# define S_ISMPC(m) ((m) & S_IFMPC)
# endif
#endif

#if !defined(S_ISNWK) && defined(S_IFNWK) /* HP/UX */
# if defined(S_IFMT)
# define S_ISNWK(m) (((m) & S_IFMT) == S_IFNWK)
# else
# define S_ISNWK(m) ((m) & S_IFNWK)
# endif
#endif

#ifdef NEED_DECOY_PERMISSIONS        /* OS/2, really */

#define	S_IRUSR S_IREAD
#define	S_IWUSR S_IWRITE
#define	S_IXUSR S_IEXEC
#define	S_IRWXU	(S_IRUSR | S_IWUSR | S_IXUSR)
#define	S_IRGRP S_IREAD
#define	S_IWGRP S_IWRITE
#define	S_IXGRP S_IEXEC
#define	S_IRWXG	(S_IRGRP | S_IWGRP | S_IXGRP)
#define	S_IROTH S_IREAD
#define	S_IWOTH S_IWRITE
#define	S_IXOTH S_IEXEC
#define	S_IRWXO	(S_IROTH | S_IWOTH | S_IXOTH)

#else /* ! NEED_DECOY_PERMISSIONS */

#ifndef S_IRUSR
#define	S_IRUSR 0400
#define	S_IWUSR 0200
#define	S_IXUSR 0100
/* Read, write, and execute by owner.  */
#define	S_IRWXU	(S_IRUSR|S_IWUSR|S_IXUSR)

#define	S_IRGRP	(S_IRUSR >> 3)	/* Read by group.  */
#define	S_IWGRP	(S_IWUSR >> 3)	/* Write by group.  */
#define	S_IXGRP	(S_IXUSR >> 3)	/* Execute by group.  */
/* Read, write, and execute by group.  */
#define	S_IRWXG	(S_IRWXU >> 3)

#define	S_IROTH	(S_IRGRP >> 3)	/* Read by others.  */
#define	S_IWOTH	(S_IWGRP >> 3)	/* Write by others.  */
#define	S_IXOTH	(S_IXGRP >> 3)	/* Execute by others.  */
/* Read, write, and execute by others.  */
#define	S_IRWXO	(S_IRWXG >> 3)
#endif /* !def S_IRUSR */
#endif /* NEED_DECOY_PERMISSIONS */

#ifndef DEVNULL
# define	DEVNULL		"/dev/null"
#endif

#ifdef HAVE_IO_H
#include <io.h>
#endif

#ifdef HAVE_DIRECT_H
#include <direct.h>
#endif

/* The NeXT (without _POSIX_SOURCE, which we don't want) has a utime.h
   which doesn't define anything.  It would be cleaner to have configure
   check for struct utimbuf, but for now I'm checking NeXT here (so I don't
   have to debug the configure check across all the machines).  */
#if defined (HAVE_UTIME_H) && !defined (NeXT)
#  include <utime.h>
#else
#  if defined (HAVE_SYS_UTIME_H)
#    include <sys/utime.h>
#  else
#    ifndef ALTOS
struct utimbuf
{
  long actime;
  long modtime;
};
#    endif
int utime ();
#  endif
#endif

/* errno.h variations:
 *
 * Not all systems set the same error code on a non-existent-file
 * error.  This tries to ask the question somewhat portably.
 * On systems that don't have ENOTEXIST, this should behave just like
 * x == ENOENT.  "x" is probably errno, of course.
 */
#ifdef ENOTEXIST
#  ifdef EOS2ERR
#    define existence_error(x) \
     (((x) == ENOTEXIST) || ((x) == ENOENT) || ((x) == EOS2ERR))
#  else
#    define existence_error(x) \
     (((x) == ENOTEXIST) || ((x) == ENOENT))
#  endif
#else
#  ifdef EVMSERR
#     define existence_error(x) \
((x) == ENOENT || (x) == EINVAL || (x) == EVMSERR)
#  else
#    define existence_error(x) ((x) == ENOENT)
#  endif
#endif

#ifdef HAVE_MALLOC
# define CVS_MALLOC malloc
# else /* !HAVE_MALLOC */
# define CVS_MALLOC rpl_malloc
#endif /* HAVE_MALLOC */
#ifdef HAVE_REALLOC
# define CVS_REALLOC realloc
#else /* !HAVE_REALLOC */
# define CVS_REALLOC rpl_realloc
#endif /* HAVE_REALLOC */

#ifndef HAVE_STDLIB_H
char *getenv ();
char *malloc ();
char *realloc ();
char *calloc ();
extern int errno;
#endif

/* check for POSIX signals */
#if defined(HAVE_SIGACTION) && defined(HAVE_SIGPROCMASK)
# define POSIX_SIGNALS
#endif

/* MINIX 1.6 doesn't properly support sigaction */
#if defined(_MINIX)
# undef POSIX_SIGNALS
#endif

/* If !POSIX, try for BSD.. Reason: 4.4BSD implements these as wrappers */
#if !defined(POSIX_SIGNALS)
# if defined(HAVE_SIGVEC) && defined(HAVE_SIGSETMASK) && defined(HAVE_SIGBLOCK)
#  define BSD_SIGNALS
# endif
#endif

/* Under OS/2, this must be included _after_ stdio.h; that's why we do
   it here. */
#ifdef USE_OWN_TCPIP_H
# include "tcpip.h"
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#else
# include <sys/file.h>
#endif

#ifndef SEEK_SET
# define SEEK_SET 0
# define SEEK_CUR 1
# define SEEK_END 2
#endif

#ifndef F_OK
# define F_OK 0
# define X_OK 1
# define W_OK 2
# define R_OK 4
#endif

#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

/* Convert B 512-byte blocks to kilobytes if K is nonzero,
   otherwise return it unchanged. */
#define convert_blocks(b, k) ((k) ? ((b) + 1) / 2 : (b))

#ifndef S_ISLNK
# define lstat stat
#endif

/*
 * Some UNIX distributions don't include these in their stat.h Defined here
 * because "config.h" is always included last.
 */
#ifndef S_IWRITE
# define	S_IWRITE	0000200    /* write permission, owner */
#endif
#ifndef S_IWGRP
# define	S_IWGRP		0000020    /* write permission, grougroup */
#endif
#ifndef S_IWOTH
# define	S_IWOTH		0000002    /* write permission, other */
#endif

/* Under non-UNIX operating systems (MS-DOS, WinNT, MacOS), many filesystem
   calls take  only one argument; permission is handled very differently on
   those systems than in Unix.  So we leave such systems a hook on which they
   can hang their own definitions.  */

#ifndef CVS_ACCESS
# define CVS_ACCESS access
#endif

#ifndef CVS_CHDIR
# define CVS_CHDIR chdir
#endif

#ifndef CVS_CREAT
# define CVS_CREAT creat
#endif

#ifndef CVS_FOPEN
# define CVS_FOPEN fopen
#endif

#ifndef CVS_FDOPEN
# define CVS_FDOPEN fdopen
#endif

#ifndef CVS_MKDIR
# define CVS_MKDIR mkdir
#endif

#ifndef CVS_OPEN
# define CVS_OPEN open
#endif

#ifndef CVS_READDIR
# define CVS_READDIR readdir
#endif

#ifndef CVS_CLOSEDIR
# define CVS_CLOSEDIR closedir
#endif

#ifndef CVS_OPENDIR
# define CVS_OPENDIR opendir
#endif

#ifndef CVS_RENAME
# define CVS_RENAME rename
#endif

#ifndef CVS_RMDIR
# define CVS_RMDIR rmdir
#endif

#ifndef CVS_STAT
/* Use the function from lib/stat.c when the system version is broken.
 */
# ifdef HAVE_STAT_EMPTY_STRING_BUG
#   define CVS_STAT rpl_stat
# else /* !HAVE_STAT_EMPTY_STRING_BUG */
#   define CVS_STAT stat
# endif /* HAVE_STAT_EMPTY_STRING_BUG */
#endif

/* Open question: should CVS_STAT be lstat by default?  We need
   to use lstat in order to handle symbolic links correctly with
   the PreservePermissions option. -twp */
#ifndef CVS_LSTAT
/* Use the function from lib/lstat.c when the system version is broken.
 */
# if defined( HAVE_STAT_EMPTY_STRING_BUG ) || !defined( LSTAT_FOLLOWS_SLASHED_SYMLINK )
#   define CVS_LSTAT rpl_lstat
# else /* !defined(HAVE_STAT_EMPTY_STRING_BUG )
        *    && defined( LSTAT_FOLLOWS_SLASHED_SYMLINK )
        */
#   define CVS_LSTAT lstat
# endif /* defined(HAVE_STAT_EMPTY_STRING_BUG )
         * || !defined( LSTAT_FOLLOWS_SLASHED_SYMLINK )
         */
#endif

#ifndef CVS_UNLINK
# define CVS_UNLINK unlink
#endif

/* Wildcard matcher.  Should be case-insensitive if the system is.  */
#ifndef CVS_FNMATCH
# define CVS_FNMATCH fnmatch
#endif

#ifndef HAVE_FSEEKO
off_t ftello (FILE *);
int fseeko (FILE *, off_t, int);
#endif /* HAVE_FSEEKO */

#ifdef WIN32
/*
 * According to GNU conventions, we should avoid referencing any macro
 * containing "WIN" as a reference to Microsoft Windows, as we would like to
 * avoid any implication that we consider Microsoft Windows any sort of "win".
 *
 * FIXME: As of 2003-06-09, folks on the GNULIB project were discussing
 * defining a configure macro to define WOE32 appropriately.  If they ever do
 * write such a beast, we should use it, though in most cases it would be
 * preferable to avoid referencing any OS or compiler anyhow, per Autoconf
 * convention, and reference only tested features of the system.
 */
# define WOE32 1
#endif /* WIN32 */



#ifdef FILENAMES_CASE_INSENSITIVE

# if defined (__CYGWIN32__) || defined (WOE32)
    /* Under Windows, filenames are case-insensitive, and both / and \
       are path component separators.  */
#   define FOLD_FN_CHAR(c) (WNT_filename_classes[(unsigned char) (c)])
extern unsigned char WNT_filename_classes[];
# else /* !__CYGWIN32__ && !WOE32 */
  /* As far as I know, only Macintosh OS X & VMS make it here, but any
   * platform defining FILENAMES_CASE_INSENSITIVE which isn't WOE32 or
   * piggy-backing the same could, in theory.  Since the OS X fold just folds
   * A-Z into a-z, I'm just allowing it to be used for any case insensitive
   * system which we aren't yet making other specific folds or exceptions for.
   * WOE32 needs its own class since \ and C:\ style absolute paths also need
   * to be accounted for.
   */
#   define FOLD_FN_CHAR(c) (OSX_filename_classes[(unsigned char) (c)])
extern unsigned char OSX_filename_classes[];
# endif /* __CYGWIN32__ || WOE32 */

/* The following need to be declared for all case insensitive filesystems.
 * When not FOLD_FN_CHAR is not #defined, a default definition for these
 * functions is provided later in this header file.  */

/* Like strcmp, but with the appropriate tweaks for file names.  */
extern int fncmp (const char *n1, const char *n2);

/* Fold characters in FILENAME to their canonical forms.  */
extern void fnfold (char *FILENAME);

#endif /* FILENAMES_CASE_INSENSITIVE */



/* Some file systems are case-insensitive.  If FOLD_FN_CHAR is
   #defined, it maps the character C onto its "canonical" form.  In a
   case-insensitive system, it would map all alphanumeric characters
   to lower case.  Under Windows NT, / and \ are both path component
   separators, so FOLD_FN_CHAR would map them both to /.  */
#ifndef FOLD_FN_CHAR
# define FOLD_FN_CHAR(c) (c)
# define fnfold(filename) (filename)
# define fncmp strcmp
#endif

/* Different file systems can have different naming patterns which designate
 * a path as absolute.
 */
#ifndef ISABSOLUTE
# define ISABSOLUTE(s) ISSLASH(s[FILE_SYSTEM_PREFIX_LEN(s)])
#endif


/* On some systems, we have to be careful about writing/reading files
   in text or binary mode (so in text mode the system can handle CRLF
   vs. LF, VMS text file conventions, &c).  We decide to just always
   be careful.  That way we don't have to worry about whether text and
   binary differ on this system.  We just have to worry about whether
   the system has O_BINARY and "rb".  The latter is easy; all ANSI C
   libraries have it, SunOS4 has it, and CVS has used it unguarded
   some places for a while now without complaints (e.g. "rb" in
   server.c (server_updated), since CVS 1.8).  The former is just an
   #ifdef.  */

#define FOPEN_BINARY_READ ("rb")
#define FOPEN_BINARY_WRITE ("wb")
#define FOPEN_BINARY_READWRITE ("r+b")

#ifdef O_BINARY
#define OPEN_BINARY (O_BINARY)
#else
#define OPEN_BINARY (0)
#endif
