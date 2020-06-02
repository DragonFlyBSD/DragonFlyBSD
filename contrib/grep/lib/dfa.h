/* dfa.h - declarations for GNU deterministic regexp compiler
   Copyright (C) 1988, 1998, 2007, 2009-2020 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc.,
   51 Franklin Street - Fifth Floor, Boston, MA  02110-1301, USA */

/* Written June, 1988 by Mike Haertel */

#include <regex.h>
#include <stdbool.h>
#include <stddef.h>

struct localeinfo; /* See localeinfo.h.  */

/* Element of a list of strings, at least one of which is known to
   appear in any R.E. matching the DFA. */
struct dfamust
{
  bool exact;
  bool begline;
  bool endline;
  char must[FLEXIBLE_ARRAY_MEMBER];
};

/* The dfa structure. It is completely opaque. */
struct dfa;

/* Needed when Gnulib is not used.  */
#ifndef _GL_ATTRIBUTE_MALLOC
# define  _GL_ATTRIBUTE_MALLOC
#endif

/* Entry points. */

/* Allocate a struct dfa.  The struct dfa is completely opaque.
   It should be initialized via dfasyntax or dfacopysyntax before other use.
   The returned pointer should be passed directly to free() after
   calling dfafree() on it. */
extern struct dfa *dfaalloc (void) _GL_ATTRIBUTE_MALLOC;

/* DFA options that can be ORed together, for dfasyntax's 4th arg.  */
enum
  {
    /* ^ and $ match only the start and end of data, and do not match
       end-of-line within data.  This is always false for grep, but
       possibly true for other apps.  */
    DFA_ANCHOR = 1 << 0,

    /* '\0' in data is end-of-line, instead of the traditional '\n'.  */
    DFA_EOL_NUL = 1 << 1
  };

/* Initialize or reinitialize a DFA.  The arguments are:
   1. The DFA to operate on.
   2. Information about the current locale.
   3. Syntax bits described in regex.h.
   4. Additional DFA options described above.  */
extern void dfasyntax (struct dfa *, struct localeinfo const *,
                       reg_syntax_t, int);

/* Initialize or reinitialize a DFA from an already-initialized DFA.  */
extern void dfacopysyntax (struct dfa *, struct dfa const *);

/* Parse the given string of given length into the given struct dfa.  */
extern void dfaparse (char const *, ptrdiff_t, struct dfa *);

/* Allocate and return a struct dfamust from a struct dfa that was
   initialized by dfaparse and not yet given to dfacomp.  */
extern struct dfamust *dfamust (struct dfa const *);

/* Free the storage held by the components of a struct dfamust. */
extern void dfamustfree (struct dfamust *);

/* Compile the given string of the given length into the given struct dfa.
   The last argument says whether to build a searching or an exact matcher.
   A null first argument means the struct dfa has already been
   initialized by dfaparse; the second argument is ignored.  */
extern void dfacomp (char const *, ptrdiff_t, struct dfa *, bool);

/* Search through a buffer looking for a match to the given struct dfa.
   Find the first occurrence of a string matching the regexp in the
   buffer, and the shortest possible version thereof.  Return a pointer to
   the first character after the match, or NULL if none is found.  BEGIN
   points to the beginning of the buffer, and END points to the first byte
   after its end.  Note however that we store a sentinel byte (usually
   newline) in *END, so the actual buffer must be one byte longer.
   When ALLOW_NL is true, newlines may appear in the matching string.
   If COUNT is non-NULL, increment *COUNT once for each newline processed.
   Finally, if BACKREF is non-NULL set *BACKREF to indicate whether we
   encountered a back-reference.  The caller can use this to decide
   whether to fall back on a backtracking matcher.  */
extern char *dfaexec (struct dfa *d, char const *begin, char *end,
                      bool allow_nl, ptrdiff_t *count, bool *backref);

/* Return a superset for D.  The superset matches everything that D
   matches, along with some other strings (though the latter should be
   rare, for efficiency reasons).  Return a null pointer if no useful
   superset is available.  */
extern struct dfa *dfasuperset (struct dfa const *d) _GL_ATTRIBUTE_PURE;

/* The DFA is likely to be fast.  */
extern bool dfaisfast (struct dfa const *) _GL_ATTRIBUTE_PURE;

/* Free the storage held by the components of a struct dfa. */
extern void dfafree (struct dfa *);

/* Error handling. */

/* dfawarn() is called by the regexp routines whenever a regex is compiled
   that likely doesn't do what the user wanted.  It takes a single
   argument, a NUL-terminated string describing the situation.  The user
   must supply a dfawarn.  */
extern void dfawarn (const char *);

/* dfaerror() is called by the regexp routines whenever an error occurs.  It
   takes a single argument, a NUL-terminated string describing the error.
   The user must supply a dfaerror.  */
extern _Noreturn void dfaerror (const char *);
