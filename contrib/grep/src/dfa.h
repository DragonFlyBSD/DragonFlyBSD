/* dfa.h - declarations for GNU deterministic regexp compiler
   Copyright (C) 1988, 1998, 2007, 2009-2014 Free Software Foundation, Inc.

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

/* Element of a list of strings, at least one of which is known to
   appear in any R.E. matching the DFA. */
struct dfamust
{
  bool exact;
  bool begline;
  bool endline;
  char *must;
  struct dfamust *next;
};

/* The dfa structure. It is completely opaque. */
struct dfa;

/* Entry points. */

/* Allocate a struct dfa.  The struct dfa is completely opaque.
   The returned pointer should be passed directly to free() after
   calling dfafree() on it. */
extern struct dfa *dfaalloc (void);

/* Return the dfamusts associated with a dfa. */
extern struct dfamust *dfamusts (struct dfa const *);

/* dfasyntax() takes three arguments; the first sets the syntax bits described
   earlier in this file, the second sets the case-folding flag, and the
   third specifies the line terminator. */
extern void dfasyntax (reg_syntax_t, int, unsigned char);

/* Compile the given string of the given length into the given struct dfa.
   Final argument is a flag specifying whether to build a searching or an
   exact matcher. */
extern void dfacomp (char const *, size_t, struct dfa *, int);

/* Search through a buffer looking for a match to the given struct dfa.
   Find the first occurrence of a string matching the regexp in the
   buffer, and the shortest possible version thereof.  Return a pointer to
   the first character after the match, or NULL if none is found.  BEGIN
   points to the beginning of the buffer, and END points to the first byte
   after its end.  Note however that we store a sentinel byte (usually
   newline) in *END, so the actual buffer must be one byte longer.
   When NEWLINE is nonzero, newlines may appear in the matching string.
   If COUNT is non-NULL, increment *COUNT once for each newline processed.
   Finally, if BACKREF is non-NULL set *BACKREF to indicate whether we
   encountered a back-reference (1) or not (0).  The caller may use this
   to decide whether to fall back on a backtracking matcher. */
extern char *dfaexec (struct dfa *d, char const *begin, char *end,
                      int newline, size_t *count, int *backref);

/* Return a superset for D.  The superset matches everything that D
   matches, along with some other strings (though the latter should be
   rare, for efficiency reasons).  Return a null pointer if no useful
   superset is available.  */
extern struct dfa *dfasuperset (struct dfa const *d) _GL_ATTRIBUTE_PURE;

/* The DFA is likely to be fast.  */
extern bool dfaisfast (struct dfa const *) _GL_ATTRIBUTE_PURE;

/* Free the storage held by the components of a struct dfa. */
extern void dfafree (struct dfa *);

/* Entry points for people who know what they're doing. */

/* Initialize the components of a struct dfa. */
extern void dfainit (struct dfa *);

/* Incrementally parse a string of given length into a struct dfa. */
extern void dfaparse (char const *, size_t, struct dfa *);

/* Analyze a parsed regexp; second argument tells whether to build a searching
   or an exact matcher. */
extern void dfaanalyze (struct dfa *, int);

/* Compute, for each possible character, the transitions out of a given
   state, storing them in an array of integers. */
extern void dfastate (ptrdiff_t, struct dfa *, ptrdiff_t []);

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

extern int using_utf8 (void);
