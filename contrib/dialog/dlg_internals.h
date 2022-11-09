/*
 *  $Id: dlg_internals.h,v 1.9 2022/04/08 21:01:58 tom Exp $
 *
 *  dlg_internals.h -- internal definitions for dialog
 *
 *  Copyright 2019-2021,2022	Thomas E. Dickey
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License, version 2.1
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to
 *	Free Software Foundation, Inc.
 *	51 Franklin St., Fifth Floor
 *	Boston, MA 02110, USA.
 */

#ifndef DLG_INTERNALS_H_included
#define DLG_INTERNALS_H_included 1

#include <dialog.h>

#ifdef NEED_WCHAR_H
#include <wchar.h>
#endif

#ifdef ENABLE_NLS
#include <libintl.h>
#include <langinfo.h>
#define _(s) dgettext(PACKAGE, s)
#else
#undef _
#define _(s) s
#endif

#include <time.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#define intptr_t long
#endif

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_SETLOCALE
#include <locale.h>
#endif

#ifdef STDC_HEADERS
#include <stddef.h>		/* for offsetof, usually via stdlib.h */
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#if defined(TIME_WITH_SYS_TIME)
# include <sys/time.h>
# include <time.h>
#else
# if defined(HAVE_SYS_TIME_H)
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if defined(HAVE_DIRENT_H)
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if defined(HAVE_SYS_NDIR_H)
#  include <sys/ndir.h>
# endif
# if defined(HAVE_SYS_DIR_H)
#  include <sys/dir.h>
# endif
# if defined(HAVE_NDIR_H)
#  include <ndir.h>
# endif
#endif

# if defined(_FILE_OFFSET_BITS) && defined(HAVE_STRUCT_DIRENT64)
#  if !defined(_LP64) && (_FILE_OFFSET_BITS == 64)
#   define      DIRENT  struct dirent64
#  else
#   define      DIRENT  struct dirent
#  endif
# else
#  define       DIRENT  struct dirent
# endif

#if defined(HAVE_SEARCH_H) && defined(HAVE_TSEARCH)
#include <search.h>
#define leaf leaf2	/* Solaris name-conflict */
#else
#undef HAVE_TSEARCH
#endif

/* possible conflicts with <term.h> which may be included in <curses.h> */
#ifdef color_names
#undef color_names
#endif

#ifdef buttons
#undef buttons
#endif

#ifndef HAVE_WGET_WCH
#undef USE_WIDE_CURSES
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* these definitions may work for antique versions of curses */
#ifndef HAVE_GETBEGYX
#undef  getbegyx
#define getbegyx(win,y,x)  (y = (win)?(win)->_begy:ERR, x = (win)?(win)->_begx:ERR)
#endif

#ifndef HAVE_GETMAXYX
#undef  getmaxyx
#define getmaxyx(win,y,x)  (y = (win)?(win)->_maxy:ERR, x = (win)?(win)->_maxx:ERR)
#endif

#ifndef HAVE_GETPARYX
#undef  getparyx
#define getparyx(win,y,x)  (y = (win)?(win)->_pary:ERR, x = (win)?(win)->_parx:ERR)
#endif

#if !defined(HAVE_WSYNCUP)
#undef wsyncup
#define wsyncup(win) /* nothing */
#endif

#if !defined(HAVE_WCURSYNCUP)
#undef wcursyncup
#define wcursyncup(win) /* nothing */
#endif

/* these definitions may be needed for bleeding-edge curses implementations */
#if !(defined(HAVE_GETBEGX) && defined(HAVE_GETBEGY))
#undef getbegx
#undef getbegy
#define getbegx(win) dlg_getbegx(win)
#define getbegy(win) dlg_getbegy(win)
extern int dlg_getbegx(WINDOW * /*win*/);
extern int dlg_getbegy(WINDOW * /*win*/);
#endif

#if !(defined(HAVE_GETCURX) && defined(HAVE_GETCURY))
#undef getcurx
#undef getcury
#define getcurx(win) dlg_getcurx(win)
#define getcury(win) dlg_getcury(win)
extern int dlg_getcurx(WINDOW * /*win*/);
extern int dlg_getcury(WINDOW * /*win*/);
#endif

#if !(defined(HAVE_GETMAXX) && defined(HAVE_GETMAXY))
#undef getmaxx
#undef getmaxy
#define getmaxx(win) dlg_getmaxx(win)
#define getmaxy(win) dlg_getmaxy(win)
extern int dlg_getmaxx(WINDOW * /*win*/);
extern int dlg_getmaxy(WINDOW * /*win*/);
#endif

#if !(defined(HAVE_GETPARX) && defined(HAVE_GETPARY))
#undef getparx
#undef getpary
#define getparx(win) dlg_getparx(win)
#define getpary(win) dlg_getpary(win)
extern int dlg_getparx(WINDOW * /*win*/);
extern int dlg_getpary(WINDOW * /*win*/);
#endif

#if !(defined(HAVE_WGETPARENT) && defined(HAVE_WINDOW__PARENT))
#undef wgetparent
#define wgetparent(win) dlg_wgetparent(win)
extern WINDOW * dlg_wgetparent(WINDOW * /*win*/);
#elif !defined(HAVE_WGETPARENT) && defined(HAVE_WINDOW__PARENT)
#undef  wgetparent
#define wgetparent(win)    ((win) ? (win)->_parent : 0)
#endif
/*
 * Use attributes.
 */
#ifdef PDCURSES
#define dlg_attrset(w,a)   (void) wattrset((w), (a))
#define dlg_attron(w,a)    (void) wattron((w), (a))
#define dlg_attroff(w,a)   (void) wattroff((w), (a))
#else
#define dlg_attrset(w,a)   (void) wattrset((w), (int)(a))
#define dlg_attron(w,a)    (void) wattron((w), (int)(a))
#define dlg_attroff(w,a)   (void) wattroff((w), (int)(a))
#endif

#ifndef isblank
#define isblank(c)         ((c) == ' ' || (c) == TAB)
#endif

#define MAX_LEN            2048
#define BUF_SIZE           (10L*1024)

#undef  MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))

#undef  MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))

#define UCH(ch)            ((unsigned char)(ch))

#define assert_ptr(ptr,msg) if ((ptr) == 0) dlg_exiterr("cannot allocate memory in " msg)

#define dlg_malloc(t,n)    (t *) malloc((size_t)(n) * sizeof(t))
#define dlg_calloc(t,n)    (t *) calloc((size_t)(n), sizeof(t))
#define dlg_realloc(t,n,p) (t *) realloc((p), (n) * sizeof(t))

#define TableSize(name) (sizeof(name)/sizeof((name)[0]))

/* *INDENT-OFF* */
#define resizeit(name, NAME) \
		name = ((NAME >= old_##NAME) \
			? (NAME - (old_##NAME - old_##name)) \
			: old_##name)

#define AddLastKey() \
	if (dialog_vars.last_key) { \
	    if (dlg_need_separator()) \
		dlg_add_separator(); \
	    dlg_add_last_key(-1); \
	}

/*
 * This is used only for debugging (FIXME: should have a separate header).
 */
#ifdef NO_LEAKS
extern void _dlg_inputstr_leaks(void);
#if defined(NCURSES_VERSION)
#if defined(HAVE_EXIT_CURSES)
/* just use exit_curses() */
#elif defined(HAVE__NC_FREE_AND_EXIT)
extern void _nc_free_and_exit(int); /* nc_alloc.h normally not installed */
#define exit_curses(code) _nc_free_and_exit(code)
#endif
#endif /* NCURSES_VERSION */
#endif /* NO_LEAKS */

/* *INDENT-ON* */

#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* DLG_INTERNALS_H_included */
