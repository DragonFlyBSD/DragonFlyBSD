/* Wide characters for gdb
   Copyright (C) 2009, 2010 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef GDB_WCHAR_H
#define GDB_WCHAR_H

/* We handle three different modes here.
   
   Capable systems have the full suite: wchar_t support and iconv
   (perhaps via GNU libiconv).  On these machines, full functionality
   is available.
   
   DJGPP is known to have libiconv but not wchar_t support.  On
   systems like this, we use the narrow character functions.  The full
   functionality is available to the user, but many characters (those
   outside the narrow range) will be displayed as escapes.
   
   Finally, some systems do not have iconv.  Here we provide a phony
   iconv which only handles a single character set, and we provide
   wrappers for the wchar_t functionality we use.  */


#define INTERMEDIATE_ENCODING "wchar_t"

#if defined (HAVE_ICONV)
#include <iconv.h>
#else
/* This define is used elsewhere so we don't need to duplicate the
   same checking logic in multiple places.  */
#define PHONY_ICONV
#endif

/* We use "btowc" as a sentinel to detect functioning wchar_t
   support.  */
#if defined (HAVE_ICONV) && defined (HAVE_WCHAR_H) && defined (HAVE_BTOWC)

#include <wchar.h>
#include <wctype.h>

typedef wchar_t gdb_wchar_t;
typedef wint_t gdb_wint_t;

#define gdb_wcslen wcslen
#define gdb_iswprint iswprint
#define gdb_iswdigit iswdigit
#define gdb_btowc btowc
#define gdb_WEOF WEOF

#define LCST(X) L ## X

#else

typedef char gdb_wchar_t;
typedef int gdb_wint_t;

#define gdb_wcslen strlen
#define gdb_iswprint isprint
#define gdb_iswdigit isdigit
#define gdb_btowc /* empty */
#define gdb_WEOF EOF

#define LCST(X) X

/* If we are using the narrow character set, we want to use the host
   narrow encoding as our intermediate encoding.  However, if we are
   also providing a phony iconv, we might as well just stick with
   "wchar_t".  */
#ifndef PHONY_ICONV
#undef INTERMEDIATE_ENCODING
#define INTERMEDIATE_ENCODING host_charset ()
#endif

#endif

#endif /* GDB_WCHAR_H */
