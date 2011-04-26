/* dfa.c - deterministic extended regexp routines for GNU
   Copyright (C) 1988, 1998, 2000, 2002, 2004, 2005, 2007-2010 Free Software
   Foundation, Inc.

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

/* Written June, 1988 by Mike Haertel
   Modified July, 1988 by Arthur David Olson to assist BMG speedups  */

#include <config.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <locale.h>

#define STREQ(a, b) (strcmp (a, b) == 0)

/* ISASCIIDIGIT differs from isdigit, as follows:
   - Its arg may be any int or unsigned int; it need not be an unsigned char.
   - It's guaranteed to evaluate its argument exactly once.
   - It's typically faster.
   Posix 1003.2-1992 section 2.5.2.1 page 50 lines 1556-1558 says that
   only '0' through '9' are digits.  Prefer ISASCIIDIGIT to isdigit unless
   it's important to use the locale's definition of `digit' even when the
   host does not conform to Posix.  */
#define ISASCIIDIGIT(c) ((unsigned) (c) - '0' <= 9)

/* gettext.h ensures that we don't use gettext if ENABLE_NLS is not defined */
#include "gettext.h"
#define _(str) gettext (str)

#include "mbsupport.h"  /* defines MBS_SUPPORT if appropriate */
#include <wchar.h>
#include <wctype.h>

#if HAVE_LANGINFO_CODESET
# include <langinfo.h>
#endif

#include "regex.h"
#include "dfa.h"
#include "hard-locale.h"
#include "xalloc.h"

/* HPUX, define those as macros in sys/param.h */
#ifdef setbit
# undef setbit
#endif
#ifdef clrbit
# undef clrbit
#endif

/* Number of bits in an unsigned char. */
#ifndef CHARBITS
# define CHARBITS 8
#endif

/* First integer value that is greater than any character code. */
#define NOTCHAR (1 << CHARBITS)

/* INTBITS need not be exact, just a lower bound. */
#ifndef INTBITS
# define INTBITS (CHARBITS * sizeof (int))
#endif

/* Number of ints required to hold a bit for every character. */
#define CHARCLASS_INTS ((NOTCHAR + INTBITS - 1) / INTBITS)

/* Sets of unsigned characters are stored as bit vectors in arrays of ints. */
typedef int charclass[CHARCLASS_INTS];

/* Sometimes characters can only be matched depending on the surrounding
   context.  Such context decisions depend on what the previous character
   was, and the value of the current (lookahead) character.  Context
   dependent constraints are encoded as 8 bit integers.  Each bit that
   is set indicates that the constraint succeeds in the corresponding
   context.

   bit 7 - previous and current are newlines
   bit 6 - previous was newline, current isn't
   bit 5 - previous wasn't newline, current is
   bit 4 - neither previous nor current is a newline
   bit 3 - previous and current are word-constituents
   bit 2 - previous was word-constituent, current isn't
   bit 1 - previous wasn't word-constituent, current is
   bit 0 - neither previous nor current is word-constituent

   Word-constituent characters are those that satisfy isalnum().

   The macro SUCCEEDS_IN_CONTEXT determines whether a a given constraint
   succeeds in a particular context.  Prevn is true if the previous character
   was a newline, currn is true if the lookahead character is a newline.
   Prevl and currl similarly depend upon whether the previous and current
   characters are word-constituent letters. */
#define MATCHES_NEWLINE_CONTEXT(constraint, prevn, currn) \
  ((constraint) & 1 << (((prevn) ? 2 : 0) + ((currn) ? 1 : 0) + 4))
#define MATCHES_LETTER_CONTEXT(constraint, prevl, currl) \
  ((constraint) & 1 << (((prevl) ? 2 : 0) + ((currl) ? 1 : 0)))
#define SUCCEEDS_IN_CONTEXT(constraint, prevn, currn, prevl, currl) \
  (MATCHES_NEWLINE_CONTEXT(constraint, prevn, currn)		     \
   && MATCHES_LETTER_CONTEXT(constraint, prevl, currl))

/* The following macros give information about what a constraint depends on. */
#define PREV_NEWLINE_DEPENDENT(constraint) \
  (((constraint) & 0xc0) >> 2 != ((constraint) & 0x30))
#define PREV_LETTER_DEPENDENT(constraint) \
  (((constraint) & 0x0c) >> 2 != ((constraint) & 0x03))

/* Tokens that match the empty string subject to some constraint actually
   work by applying that constraint to determine what may follow them,
   taking into account what has gone before.  The following values are
   the constraints corresponding to the special tokens previously defined. */
#define NO_CONSTRAINT 0xff
#define BEGLINE_CONSTRAINT 0xcf
#define ENDLINE_CONSTRAINT 0xaf
#define BEGWORD_CONSTRAINT 0xf2
#define ENDWORD_CONSTRAINT 0xf4
#define LIMWORD_CONSTRAINT 0xf6
#define NOTLIMWORD_CONSTRAINT 0xf9

/* The regexp is parsed into an array of tokens in postfix form.  Some tokens
   are operators and others are terminal symbols.  Most (but not all) of these
   codes are returned by the lexical analyzer. */
typedef enum
{
  END = -1,			/* END is a terminal symbol that matches the
                                   end of input; any value of END or less in
                                   the parse tree is such a symbol.  Accepting
                                   states of the DFA are those that would have
                                   a transition on END. */

  /* Ordinary character values are terminal symbols that match themselves. */

  EMPTY = NOTCHAR,		/* EMPTY is a terminal symbol that matches
                                   the empty string. */

  BACKREF,			/* BACKREF is generated by \<digit>; it
                                   it not completely handled.  If the scanner
                                   detects a transition on backref, it returns
                                   a kind of "semi-success" indicating that
                                   the match will have to be verified with
                                   a backtracking matcher. */

  BEGLINE,			/* BEGLINE is a terminal symbol that matches
                                   the empty string if it is at the beginning
                                   of a line. */

  ENDLINE,			/* ENDLINE is a terminal symbol that matches
                                   the empty string if it is at the end of
                                   a line. */

  BEGWORD,			/* BEGWORD is a terminal symbol that matches
                                   the empty string if it is at the beginning
                                   of a word. */

  ENDWORD,			/* ENDWORD is a terminal symbol that matches
                                   the empty string if it is at the end of
                                   a word. */

  LIMWORD,			/* LIMWORD is a terminal symbol that matches
                                   the empty string if it is at the beginning
                                   or the end of a word. */

  NOTLIMWORD,			/* NOTLIMWORD is a terminal symbol that
                                   matches the empty string if it is not at
                                   the beginning or end of a word. */

  QMARK,			/* QMARK is an operator of one argument that
                                   matches zero or one occurences of its
                                   argument. */

  STAR,				/* STAR is an operator of one argument that
                                   matches the Kleene closure (zero or more
                                   occurrences) of its argument. */

  PLUS,				/* PLUS is an operator of one argument that
                                   matches the positive closure (one or more
                                   occurrences) of its argument. */

  REPMN,			/* REPMN is a lexical token corresponding
                                   to the {m,n} construct.  REPMN never
                                   appears in the compiled token vector. */

  CAT,				/* CAT is an operator of two arguments that
                                   matches the concatenation of its
                                   arguments.  CAT is never returned by the
                                   lexical analyzer. */

  OR,				/* OR is an operator of two arguments that
                                   matches either of its arguments. */

  LPAREN,			/* LPAREN never appears in the parse tree,
                                   it is only a lexeme. */

  RPAREN,			/* RPAREN never appears in the parse tree. */

  ANYCHAR,                     /* ANYCHAR is a terminal symbol that matches
                                  any multibyte (or single byte) characters.
                                  It is used only if MB_CUR_MAX > 1.  */

  MBCSET,			/* MBCSET is similar to CSET, but for
                                   multibyte characters.  */

  WCHAR,			/* Only returned by lex.  wctok contains
                                   the wide character representation.  */

  CSET				/* CSET and (and any value greater) is a
                                   terminal symbol that matches any of a
                                   class of characters. */
} token;


/* States of the recognizer correspond to sets of positions in the parse
   tree, together with the constraints under which they may be matched.
   So a position is encoded as an index into the parse tree together with
   a constraint. */
typedef struct
{
  unsigned int index;		/* Index into the parse array. */
  unsigned int constraint;	/* Constraint for matching this position. */
} position;

/* Sets of positions are stored as arrays. */
typedef struct
{
  position *elems;		/* Elements of this position set. */
  int nelem;			/* Number of elements in this set. */
} position_set;

/* A state of the dfa consists of a set of positions, some flags,
   and the token value of the lowest-numbered position of the state that
   contains an END token. */
typedef struct
{
  int hash;			/* Hash of the positions of this state. */
  position_set elems;		/* Positions this state could match. */
  char newline;			/* True if previous state matched newline. */
  char letter;			/* True if previous state matched a letter. */
  char backref;			/* True if this state matches a \<digit>. */
  unsigned char constraint;	/* Constraint for this state to accept. */
  int first_end;		/* Token value of the first END in elems. */
#if MBS_SUPPORT
  position_set mbps;           /* Positions which can match multibyte
                                  characters.  e.g. period.
                                  These staff are used only if
                                  MB_CUR_MAX > 1.  */
#endif
} dfa_state;

#if MBS_SUPPORT
/* A bracket operator.
   e.g. [a-c], [[:alpha:]], etc.  */
struct mb_char_classes
{
  int cset;
  int invert;
  wchar_t *chars;		/* Normal characters.  */
  int nchars;
  wctype_t *ch_classes;		/* Character classes.  */
  int nch_classes;
  wchar_t *range_sts;		/* Range characters (start of the range).  */
  wchar_t *range_ends;		/* Range characters (end of the range).  */
  int nranges;
  char **equivs;		/* Equivalent classes.  */
  int nequivs;
  char **coll_elems;
  int ncoll_elems;		/* Collating elements.  */
};
#endif

/* A compiled regular expression. */
struct dfa
{
  /* Fields filled by the scanner. */
  charclass *charclasses;	/* Array of character sets for CSET tokens. */
  int cindex;			/* Index for adding new charclasses. */
  int calloc;			/* Number of charclasses currently allocated. */

  /* Fields filled by the parser. */
  token *tokens;		/* Postfix parse array. */
  int tindex;			/* Index for adding new tokens. */
  int talloc;			/* Number of tokens currently allocated. */
  int depth;			/* Depth required of an evaluation stack
                                   used for depth-first traversal of the
                                   parse tree. */
  int nleaves;			/* Number of leaves on the parse tree. */
  int nregexps;			/* Count of parallel regexps being built
                                   with dfaparse(). */
  unsigned int mb_cur_max;	/* Cached value of MB_CUR_MAX.  */
  int utf8_anychar_classes[5];	/* To lower ANYCHAR in UTF-8 locales.  */

  /* The following are used only if MB_CUR_MAX > 1.  */

  /* The value of multibyte_prop[i] is defined by following rule.
       if tokens[i] < NOTCHAR
         bit 0 : tokens[i] is the first byte of a character, including
                 single-byte characters.
         bit 1 : tokens[i] is the last byte of a character, including
                 single-byte characters.

       if tokens[i] = MBCSET
         ("the index of mbcsets correspnd to this operator" << 2) + 3

     e.g.
     tokens
        = 'single_byte_a', 'multi_byte_A', single_byte_b'
        = 'sb_a', 'mb_A(1st byte)', 'mb_A(2nd byte)', 'mb_A(3rd byte)', 'sb_b'
     multibyte_prop
        = 3     , 1               ,  0              ,  2              , 3
  */
  int nmultibyte_prop;
  int *multibyte_prop;

  /* Array of the bracket expression in the DFA.  */
  struct mb_char_classes *mbcsets;
  int nmbcsets;
  int mbcsets_alloc;

  /* Fields filled by the state builder. */
  dfa_state *states;		/* States of the dfa. */
  int sindex;			/* Index for adding new states. */
  int salloc;			/* Number of states currently allocated. */

  /* Fields filled by the parse tree->NFA conversion. */
  position_set *follows;	/* Array of follow sets, indexed by position
                                   index.  The follow of a position is the set
                                   of positions containing characters that
                                   could conceivably follow a character
                                   matching the given position in a string
                                   matching the regexp.  Allocated to the
                                   maximum possible position index. */
  int searchflag;		/* True if we are supposed to build a searching
                                   as opposed to an exact matcher.  A searching
                                   matcher finds the first and shortest string
                                   matching a regexp anywhere in the buffer,
                                   whereas an exact matcher finds the longest
                                   string matching, but anchored to the
                                   beginning of the buffer. */

  /* Fields filled by dfaexec. */
  int tralloc;			/* Number of transition tables that have
                                   slots so far. */
  int trcount;			/* Number of transition tables that have
                                   actually been built. */
  int **trans;			/* Transition tables for states that can
                                   never accept.  If the transitions for a
                                   state have not yet been computed, or the
                                   state could possibly accept, its entry in
                                   this table is NULL. */
  int **realtrans;		/* Trans always points to realtrans + 1; this
                                   is so trans[-1] can contain NULL. */
  int **fails;			/* Transition tables after failing to accept
                                   on a state that potentially could do so. */
  int *success;			/* Table of acceptance conditions used in
                                   dfaexec and computed in build_state. */
  int *newlines;		/* Transitions on newlines.  The entry for a
                                   newline in any transition table is always
                                   -1 so we can count lines without wasting
                                   too many cycles.  The transition for a
                                   newline is stored separately and handled
                                   as a special case.  Newline is also used
                                   as a sentinel at the end of the buffer. */
  struct dfamust *musts;	/* List of strings, at least one of which
                                   is known to appear in any r.e. matching
                                   the dfa. */
};

/* Some macros for user access to dfa internals. */

/* ACCEPTING returns true if s could possibly be an accepting state of r. */
#define ACCEPTING(s, r) ((r).states[s].constraint)

/* ACCEPTS_IN_CONTEXT returns true if the given state accepts in the
   specified context. */
#define ACCEPTS_IN_CONTEXT(prevn, currn, prevl, currl, state, dfa) \
  SUCCEEDS_IN_CONTEXT((dfa).states[state].constraint,		   \
                       prevn, currn, prevl, currl)

static void dfamust (struct dfa *dfa);
static void regexp (void);

#define CALLOC(p, t, n) ((p) = xcalloc((size_t)(n), sizeof (t)))
#define MALLOC(p, t, n) ((p) = xmalloc((n) * sizeof (t)))
#define REALLOC(p, t, n) ((p) = xrealloc((p), (n) * sizeof (t)))

/* Reallocate an array of type t if nalloc is too small for index. */
#define REALLOC_IF_NECESSARY(p, t, nalloc, index) \
  if ((index) >= (nalloc))			  \
    {						  \
      do					  \
        (nalloc) *= 2;				  \
      while ((index) >= (nalloc));		  \
      REALLOC(p, t, nalloc);			  \
    }


#ifdef DEBUG

static void
prtok (token t)
{
  char const *s;

  if (t < 0)
    fprintf(stderr, "END");
  else if (t < NOTCHAR)
    fprintf(stderr, "%c", t);
  else
    {
      switch (t)
        {
        case EMPTY: s = "EMPTY"; break;
        case BACKREF: s = "BACKREF"; break;
        case BEGLINE: s = "BEGLINE"; break;
        case ENDLINE: s = "ENDLINE"; break;
        case BEGWORD: s = "BEGWORD"; break;
        case ENDWORD: s = "ENDWORD"; break;
        case LIMWORD: s = "LIMWORD"; break;
        case NOTLIMWORD: s = "NOTLIMWORD"; break;
        case QMARK: s = "QMARK"; break;
        case STAR: s = "STAR"; break;
        case PLUS: s = "PLUS"; break;
        case CAT: s = "CAT"; break;
        case OR: s = "OR"; break;
        case LPAREN: s = "LPAREN"; break;
        case RPAREN: s = "RPAREN"; break;
#if MBS_SUPPORT
        case ANYCHAR: s = "ANYCHAR"; break;
        case MBCSET: s = "MBCSET"; break;
#endif /* MBS_SUPPORT */
        default: s = "CSET"; break;
        }
      fprintf(stderr, "%s", s);
    }
}
#endif /* DEBUG */

/* Stuff pertaining to charclasses. */

static int
tstbit (unsigned int b, charclass const c)
{
  return c[b / INTBITS] & 1 << b % INTBITS;
}

static void
setbit (unsigned int b, charclass c)
{
  c[b / INTBITS] |= 1 << b % INTBITS;
}

static void
clrbit (unsigned int b, charclass c)
{
  c[b / INTBITS] &= ~(1 << b % INTBITS);
}

static void
copyset (charclass const src, charclass dst)
{
  memcpy (dst, src, sizeof (charclass));
}

static void
zeroset (charclass s)
{
  memset (s, 0, sizeof (charclass));
}

static void
notset (charclass s)
{
  int i;

  for (i = 0; i < CHARCLASS_INTS; ++i)
    s[i] = ~s[i];
}

static int
equal (charclass const s1, charclass const s2)
{
  return memcmp (s1, s2, sizeof (charclass)) == 0;
}

/* A pointer to the current dfa is kept here during parsing. */
static struct dfa *dfa;

/* Find the index of charclass s in dfa->charclasses, or allocate a new charclass. */
static int
charclass_index (charclass const s)
{
  int i;

  for (i = 0; i < dfa->cindex; ++i)
    if (equal(s, dfa->charclasses[i]))
      return i;
  REALLOC_IF_NECESSARY(dfa->charclasses, charclass, dfa->calloc, dfa->cindex);
  ++dfa->cindex;
  copyset(s, dfa->charclasses[i]);
  return i;
}

/* Syntax bits controlling the behavior of the lexical analyzer. */
static reg_syntax_t syntax_bits, syntax_bits_set;

/* Flag for case-folding letters into sets. */
static int case_fold;

/* End-of-line byte in data.  */
static unsigned char eolbyte;

/* Entry point to set syntax options. */
void
dfasyntax (reg_syntax_t bits, int fold, unsigned char eol)
{
  syntax_bits_set = 1;
  syntax_bits = bits;
  case_fold = fold;
  eolbyte = eol;
}

/* Like setbit, but if case is folded, set both cases of a letter.
   For MB_CUR_MAX > 1, one or both of the two cases may not be set,
   so the resulting charset may only be used as an optimization.  */
static void
setbit_case_fold (
#if MBS_SUPPORT
                  wint_t b,
#else
                  unsigned int b,
#endif
                  charclass c)
{
  if (case_fold)
    {
#if MBS_SUPPORT
      if (MB_CUR_MAX > 1)
        {
          wint_t b1 = iswupper(b) ? towlower(b) : b;
          wint_t b2 = iswlower(b) ? towupper(b) : b;
          if (wctob ((unsigned char)b1) == b1)
            setbit (b1, c);
          if (b2 != b1 && wctob ((unsigned char)b2) == b2)
            setbit (b2, c);
        }
      else
#endif
        {
          unsigned char b1 = isupper(b) ? tolower(b) : b;
          unsigned char b2 = islower(b) ? toupper(b) : b;
          setbit (b1, c);
          if (b2 != b1)
            setbit (b2, c);
        }
    }
  else
    {
#if MBS_SUPPORT
      if (wctob ((unsigned char)b) == b)
#endif
        setbit (b, c);
    }
}


/* UTF-8 encoding allows some optimizations that we can't otherwise
   assume in a multibyte encoding. */
static inline int
using_utf8 (void)
{
  static int utf8 = -1;
  if (utf8 == -1)
    {
#if defined HAVE_LANGINFO_CODESET && defined MBS_SUPPORT
      utf8 = (STREQ (nl_langinfo (CODESET), "UTF-8"));
#else
      utf8 = 0;
#endif
    }

  return utf8;
}

/* Lexical analyzer.  All the dross that deals with the obnoxious
   GNU Regex syntax bits is located here.  The poor, suffering
   reader is referred to the GNU Regex documentation for the
   meaning of the @#%!@#%^!@ syntax bits. */

static char const *lexptr;	/* Pointer to next input character. */
static int lexleft;		/* Number of characters remaining. */
static token lasttok;		/* Previous token returned; initially END. */
static int laststart;		/* True if we're separated from beginning or (, |
                                   only by zero-width characters. */
static int parens;		/* Count of outstanding left parens. */
static int minrep, maxrep;	/* Repeat counts for {m,n}. */
static int hard_LC_COLLATE;	/* Nonzero if LC_COLLATE is hard.  */

static int cur_mb_len = 1;	/* Length of the multibyte representation of
                                   wctok.  */
#if MBS_SUPPORT
/* These variables are used only if (MB_CUR_MAX > 1).  */
static mbstate_t mbs;		/* Mbstate for mbrlen().  */
static wchar_t wctok;		/* Wide character representation of the current
                                   multibyte character.  */
static unsigned char *mblen_buf;/* Correspond to the input buffer in dfaexec().
                                   Each element store the amount of remain
                                   byte of corresponding multibyte character
                                   in the input string.  A element's value
                                   is 0 if corresponding character is a
                                   single byte chracter.
                                   e.g. input : 'a', <mb(0)>, <mb(1)>, <mb(2)>
                                    mblen_buf :   0,       3,       2,       1
                                */
static wchar_t *inputwcs;	/* Wide character representation of input
                                   string in dfaexec().
                                   The length of this array is same as
                                   the length of input string(char array).
                                   inputstring[i] is a single-byte char,
                                   or 1st byte of a multibyte char.
                                   And inputwcs[i] is the codepoint.  */
static unsigned char const *buf_begin;	/* reference to begin in dfaexec().  */
static unsigned char const *buf_end;	/* reference to end in dfaexec().  */
#endif /* MBS_SUPPORT  */


#if MBS_SUPPORT
/* Note that characters become unsigned here. */
# define FETCH_WC(c, wc, eoferr)		\
  do {						\
    if (! lexleft)				\
      {						\
        if ((eoferr) != 0)			\
          dfaerror (eoferr);			\
        else					\
          return lasttok = END;			\
      }						\
    else					\
      {						\
        wchar_t _wc;				\
        cur_mb_len = mbrtowc(&_wc, lexptr, lexleft, &mbs); \
        if (cur_mb_len <= 0)			\
          {					\
            cur_mb_len = 1;			\
            --lexleft;				\
            (wc) = (c) = (unsigned char) *lexptr++; \
          }					\
        else					\
          {					\
            lexptr += cur_mb_len;		\
            lexleft -= cur_mb_len;		\
            (wc) = _wc;				\
            (c) = wctob(wc);			\
          }					\
      }						\
  } while(0)

# define FETCH(c, eoferr)			\
  do {						\
    wint_t wc;					\
    FETCH_WC(c, wc, eoferr);			\
  } while(0)

#else
/* Note that characters become unsigned here. */
# define FETCH(c, eoferr)	      \
  do {				      \
    if (! lexleft)		      \
      {				      \
        if ((eoferr) != 0)	      \
          dfaerror (eoferr);	      \
        else			      \
          return lasttok = END;	      \
      }				      \
    (c) = (unsigned char) *lexptr++;  \
    --lexleft;			      \
  } while(0)

# define FETCH_WC(c, unused, eoferr) FETCH (c, eoferr)

#endif /* MBS_SUPPORT */

static int
in_coll_range (char ch, char from, char to)
{
  char c[6] = { from, 0, ch, 0, to, 0 };
  return strcoll (&c[0], &c[2]) <= 0 && strcoll (&c[2], &c[4]) <= 0;
}

typedef int predicate (int);

/* The following list maps the names of the Posix named character classes
   to predicate functions that determine whether a given character is in
   the class.  The leading [ has already been eaten by the lexical analyzer. */
struct dfa_ctype {
  const char *name;
  predicate *func;
  bool single_byte_only;
};

static const struct dfa_ctype prednames[] = {
  { "alpha", isalpha, false },
  { "upper", isupper, false },
  { "lower", islower, false },
  { "digit", isdigit, true },
  { "xdigit", isxdigit, true },
  { "space", isspace, false },
  { "punct", ispunct, false },
  { "alnum", isalnum, false },
  { "print", isprint, false },
  { "graph", isgraph, false },
  { "cntrl", iscntrl, false },
  { "blank", isblank, false },
  { NULL, NULL, false }
};

static const struct dfa_ctype *
find_pred (const char *str)
{
  unsigned int i;
  for (i = 0; prednames[i].name; ++i)
    if (STREQ (str, prednames[i].name))
      break;

  return &prednames[i];
}

/* Multibyte character handling sub-routine for lex.
   This function  parse a bracket expression and build a struct
   mb_char_classes.  */
static token
parse_bracket_exp (void)
{
  int invert;
  int c, c1, c2;
  charclass ccl;

  /* Used to warn about [:space:].
     Bit 0 = first character is a colon.
     Bit 1 = last character is a colon.
     Bit 2 = includes any other character but a colon.
     Bit 3 = includes ranges, char/equiv classes or collation elements.  */
  int colon_warning_state;

#if MBS_SUPPORT
  wint_t wc, wc1, wc2;

  /* Work area to build a mb_char_classes.  */
  struct mb_char_classes *work_mbc;
  int chars_al, range_sts_al, range_ends_al, ch_classes_al,
    equivs_al, coll_elems_al;

  chars_al = 1;
  range_sts_al = range_ends_al = 0;
  ch_classes_al = equivs_al = coll_elems_al = 0;
  if (MB_CUR_MAX > 1)
    {
      REALLOC_IF_NECESSARY(dfa->mbcsets, struct mb_char_classes,
                           dfa->mbcsets_alloc, dfa->nmbcsets + 1);

      /* dfa->multibyte_prop[] hold the index of dfa->mbcsets.
         We will update dfa->multibyte_prop[] in addtok(), because we can't
         decide the index in dfa->tokens[].  */

      /* Initialize work area.  */
      work_mbc = &(dfa->mbcsets[dfa->nmbcsets++]);
      memset (work_mbc, 0, sizeof *work_mbc);
    }
  else
    work_mbc = NULL;
#endif

  memset (ccl, 0, sizeof ccl);
  FETCH_WC (c, wc, _("unbalanced ["));
  if (c == '^')
    {
      FETCH_WC (c, wc, _("unbalanced ["));
      invert = 1;
    }
  else
    invert = 0;

  colon_warning_state = (c == ':');
  do
    {
      c1 = EOF; /* mark c1 is not initialized".  */
      colon_warning_state &= ~2;

      /* Note that if we're looking at some other [:...:] construct,
         we just treat it as a bunch of ordinary characters.  We can do
         this because we assume regex has checked for syntax errors before
         dfa is ever called. */
      if (c == '[' && (syntax_bits & RE_CHAR_CLASSES))
        {
#define BRACKET_BUFFER_SIZE 128
          char str[BRACKET_BUFFER_SIZE];
          FETCH_WC (c1, wc1, _("unbalanced ["));

          /* If pattern contains `[[:', `[[.', or `[[='.  */
          if (c1 == ':'
#if MBS_SUPPORT
              /* TODO: handle `[[.' and `[[=' also for MB_CUR_MAX == 1.  */
              || (MB_CUR_MAX > 1 && (c1 == '.' || c1 == '='))
#endif
              )
            {
              size_t len = 0;
              for (;;)
                {
                  FETCH_WC (c, wc, _("unbalanced ["));
                  if ((c == c1 && *lexptr == ']') || lexleft == 0)
                    break;
                  if (len < BRACKET_BUFFER_SIZE)
                    str[len++] = c;
                  else
                    /* This is in any case an invalid class name.  */
                    str[0] = '\0';
                }
              str[len] = '\0';

              /* Fetch bracket.  */
              FETCH_WC (c, wc, _("unbalanced ["));
              if (c1 == ':')
                /* build character class.  */
                {
                  char const *class
                    = (case_fold && (STREQ  (str, "upper")
                                     || STREQ  (str, "lower"))
                                       ? "alpha"
                                       : str);
                  const struct dfa_ctype *pred = find_pred (class);
                  if (!pred)
                    dfaerror(_("invalid character class"));

#if MBS_SUPPORT
                  if (MB_CUR_MAX > 1 && !pred->single_byte_only)
                    {
                      /* Store the character class as wctype_t.  */
                      wctype_t wt = wctype (class);

                      if (ch_classes_al == 0)
                        MALLOC(work_mbc->ch_classes, wctype_t, ++ch_classes_al);
                      REALLOC_IF_NECESSARY(work_mbc->ch_classes, wctype_t,
                                           ch_classes_al,
                                           work_mbc->nch_classes + 1);
                      work_mbc->ch_classes[work_mbc->nch_classes++] = wt;
                    }
#endif

                  for (c2 = 0; c2 < NOTCHAR; ++c2)
                    if (pred->func(c2))
                      setbit_case_fold (c2, ccl);
                }

#if MBS_SUPPORT
              else if (c1 == '=' || c1 == '.')
                {
                  char *elem;
                  MALLOC(elem, char, len + 1);
                  strncpy(elem, str, len + 1);

                  if (c1 == '=')
                    /* build equivalent class.  */
                    {
                      if (equivs_al == 0)
                        MALLOC(work_mbc->equivs, char*, ++equivs_al);
                      REALLOC_IF_NECESSARY(work_mbc->equivs, char*,
                                           equivs_al,
                                           work_mbc->nequivs + 1);
                      work_mbc->equivs[work_mbc->nequivs++] = elem;
                    }

                  if (c1 == '.')
                    /* build collating element.  */
                    {
                      if (coll_elems_al == 0)
                        MALLOC(work_mbc->coll_elems, char*, ++coll_elems_al);
                      REALLOC_IF_NECESSARY(work_mbc->coll_elems, char*,
                                           coll_elems_al,
                                           work_mbc->ncoll_elems + 1);
                      work_mbc->coll_elems[work_mbc->ncoll_elems++] = elem;
                    }
                }
#endif
              colon_warning_state |= 8;

              /* Fetch new lookahead character.  */
              FETCH_WC (c1, wc1, _("unbalanced ["));
              continue;
            }

          /* We treat '[' as a normal character here.  c/c1/wc/wc1
             are already set up.  */
        }

      if (c == '\\' && (syntax_bits & RE_BACKSLASH_ESCAPE_IN_LISTS))
        FETCH_WC(c, wc, _("unbalanced ["));

      if (c1 == EOF)
        FETCH_WC(c1, wc1, _("unbalanced ["));

      if (c1 == '-')
        /* build range characters.  */
        {
          FETCH_WC(c2, wc2, _("unbalanced ["));
          if (c2 == ']')
            {
              /* In the case [x-], the - is an ordinary hyphen,
                 which is left in c1, the lookahead character. */
              lexptr -= cur_mb_len;
              lexleft += cur_mb_len;
            }
        }

      if (c1 == '-' && c2 != ']')
        {
          if (c2 == '\\'
              && (syntax_bits & RE_BACKSLASH_ESCAPE_IN_LISTS))
            FETCH_WC(c2, wc2, _("unbalanced ["));

#if MBS_SUPPORT
          if (MB_CUR_MAX > 1)
            {
              /* When case folding map a range, say [m-z] (or even [M-z])
                 to the pair of ranges, [m-z] [M-Z].  */
              if (range_sts_al == 0)
                {
                  MALLOC(work_mbc->range_sts, wchar_t, ++range_sts_al);
                  MALLOC(work_mbc->range_ends, wchar_t, ++range_ends_al);
                }
              REALLOC_IF_NECESSARY(work_mbc->range_sts, wchar_t,
                                   range_sts_al, work_mbc->nranges + 1);
              REALLOC_IF_NECESSARY(work_mbc->range_ends, wchar_t,
                                   range_ends_al, work_mbc->nranges + 1);
              work_mbc->range_sts[work_mbc->nranges] =
                case_fold ? towlower(wc) : (wchar_t)wc;
              work_mbc->range_ends[work_mbc->nranges++] =
                case_fold ? towlower(wc2) : (wchar_t)wc2;

#ifndef GREP
              if (case_fold && (iswalpha(wc) || iswalpha(wc2)))
                {
                  REALLOC_IF_NECESSARY(work_mbc->range_sts, wchar_t,
                                       range_sts_al, work_mbc->nranges + 1);
                  work_mbc->range_sts[work_mbc->nranges] = towupper(wc);
                  REALLOC_IF_NECESSARY(work_mbc->range_ends, wchar_t,
                                       range_ends_al, work_mbc->nranges + 1);
                  work_mbc->range_ends[work_mbc->nranges++] = towupper(wc2);
                }
#endif
            }
          else
#endif
            {
              c1 = c;
              if (case_fold)
                {
                  c1 = tolower (c1);
                  c2 = tolower (c2);
                }
              if (!hard_LC_COLLATE)
                for (c = c1; c <= c2; c++)
                  setbit_case_fold (c, ccl);
              else
                for (c = 0; c < NOTCHAR; ++c)
                  if (!(case_fold && isupper (c))
                      && in_coll_range (c, c1, c2))
                    setbit_case_fold (c, ccl);
            }

          colon_warning_state |= 8;
          FETCH_WC(c1, wc1, _("unbalanced ["));
          continue;
        }

      colon_warning_state |= (c == ':') ? 2 : 4;

#if MBS_SUPPORT
      /* Build normal characters.  */
      setbit_case_fold (wc, ccl);
      if (MB_CUR_MAX > 1)
        {
          if (case_fold && iswalpha(wc))
            {
              wc = towlower(wc);
              c = wctob(wc);
              if (c == EOF || (wint_t)c != (wint_t)wc)
                {
                  REALLOC_IF_NECESSARY(work_mbc->chars, wchar_t, chars_al,
                                       work_mbc->nchars + 1);
                  work_mbc->chars[work_mbc->nchars++] = wc;
                }
#ifdef GREP
              continue;
#else
              wc = towupper(wc);
              c = wctob(wc);
#endif
            }
          if (c == EOF || (wint_t)c != (wint_t)wc)
            {
              REALLOC_IF_NECESSARY(work_mbc->chars, wchar_t, chars_al,
                                   work_mbc->nchars + 1);
              work_mbc->chars[work_mbc->nchars++] = wc;
            }
        }
#else
      setbit_case_fold (c, ccl);
#endif
    }
  while ((
#if MBS_SUPPORT
         wc = wc1,
#endif
         (c = c1) != ']'));

  if (colon_warning_state == 7)
    dfawarn (_("character class syntax is [[:space:]], not [:space:]"));

#if MBS_SUPPORT
  if (MB_CUR_MAX > 1
      && (!using_utf8()
          || invert
          || work_mbc->nchars != 0
          || work_mbc->nch_classes != 0
          || work_mbc->nranges != 0
          || work_mbc->nequivs != 0
          || work_mbc->ncoll_elems != 0))
    {
      static charclass zeroclass;
      work_mbc->invert = invert;
      work_mbc->cset = equal(ccl, zeroclass) ? -1 : charclass_index(ccl);
      return MBCSET;
    }
#endif

  if (invert)
    {
#if MBS_SUPPORT
      assert(MB_CUR_MAX == 1);
#endif
      notset(ccl);
      if (syntax_bits & RE_HAT_LISTS_NOT_NEWLINE)
        clrbit(eolbyte, ccl);
    }

  return CSET + charclass_index(ccl);
}

/* Return non-zero if C is a `word-constituent' byte; zero otherwise.  */
#define IS_WORD_CONSTITUENT(C) (isalnum(C) || (C) == '_')

static token
lex (void)
{
  unsigned int c, c2;
  int backslash = 0;
  charclass ccl;
  int i;

  /* Basic plan: We fetch a character.  If it's a backslash,
     we set the backslash flag and go through the loop again.
     On the plus side, this avoids having a duplicate of the
     main switch inside the backslash case.  On the minus side,
     it means that just about every case begins with
     "if (backslash) ...".  */
  for (i = 0; i < 2; ++i)
    {
#if MBS_SUPPORT
      if (MB_CUR_MAX > 1)
        {
          FETCH_WC (c, wctok, NULL);
          if ((int)c == EOF)
            goto normal_char;
        }
      else
#endif /* MBS_SUPPORT  */
        FETCH(c, NULL);

      switch (c)
        {
        case '\\':
          if (backslash)
            goto normal_char;
          if (lexleft == 0)
            dfaerror(_("unfinished \\ escape"));
          backslash = 1;
          break;

        case '^':
          if (backslash)
            goto normal_char;
          if (syntax_bits & RE_CONTEXT_INDEP_ANCHORS
              || lasttok == END
              || lasttok == LPAREN
              || lasttok == OR)
            return lasttok = BEGLINE;
          goto normal_char;

        case '$':
          if (backslash)
            goto normal_char;
          if (syntax_bits & RE_CONTEXT_INDEP_ANCHORS
              || lexleft == 0
              || (syntax_bits & RE_NO_BK_PARENS
                  ? lexleft > 0 && *lexptr == ')'
                  : lexleft > 1 && lexptr[0] == '\\' && lexptr[1] == ')')
              || (syntax_bits & RE_NO_BK_VBAR
                  ? lexleft > 0 && *lexptr == '|'
                  : lexleft > 1 && lexptr[0] == '\\' && lexptr[1] == '|')
              || ((syntax_bits & RE_NEWLINE_ALT)
                  && lexleft > 0 && *lexptr == '\n'))
            return lasttok = ENDLINE;
          goto normal_char;

        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          if (backslash && !(syntax_bits & RE_NO_BK_REFS))
            {
              laststart = 0;
              return lasttok = BACKREF;
            }
          goto normal_char;

        case '`':
          if (backslash && !(syntax_bits & RE_NO_GNU_OPS))
            return lasttok = BEGLINE;	/* FIXME: should be beginning of string */
          goto normal_char;

        case '\'':
          if (backslash && !(syntax_bits & RE_NO_GNU_OPS))
            return lasttok = ENDLINE;	/* FIXME: should be end of string */
          goto normal_char;

        case '<':
          if (backslash && !(syntax_bits & RE_NO_GNU_OPS))
            return lasttok = BEGWORD;
          goto normal_char;

        case '>':
          if (backslash && !(syntax_bits & RE_NO_GNU_OPS))
            return lasttok = ENDWORD;
          goto normal_char;

        case 'b':
          if (backslash && !(syntax_bits & RE_NO_GNU_OPS))
            return lasttok = LIMWORD;
          goto normal_char;

        case 'B':
          if (backslash && !(syntax_bits & RE_NO_GNU_OPS))
            return lasttok = NOTLIMWORD;
          goto normal_char;

        case '?':
          if (syntax_bits & RE_LIMITED_OPS)
            goto normal_char;
          if (backslash != ((syntax_bits & RE_BK_PLUS_QM) != 0))
            goto normal_char;
          if (!(syntax_bits & RE_CONTEXT_INDEP_OPS) && laststart)
            goto normal_char;
          return lasttok = QMARK;

        case '*':
          if (backslash)
            goto normal_char;
          if (!(syntax_bits & RE_CONTEXT_INDEP_OPS) && laststart)
            goto normal_char;
          return lasttok = STAR;

        case '+':
          if (syntax_bits & RE_LIMITED_OPS)
            goto normal_char;
          if (backslash != ((syntax_bits & RE_BK_PLUS_QM) != 0))
            goto normal_char;
          if (!(syntax_bits & RE_CONTEXT_INDEP_OPS) && laststart)
            goto normal_char;
          return lasttok = PLUS;

        case '{':
          if (!(syntax_bits & RE_INTERVALS))
            goto normal_char;
          if (backslash != ((syntax_bits & RE_NO_BK_BRACES) == 0))
            goto normal_char;
          if (!(syntax_bits & RE_CONTEXT_INDEP_OPS) && laststart)
            goto normal_char;

          if (syntax_bits & RE_NO_BK_BRACES)
            {
              /* Scan ahead for a valid interval; if it's not valid,
                 treat it as a literal '{'.  */
              int lo = -1, hi = -1;
              char const *p = lexptr;
              char const *lim = p + lexleft;
              for (;  p != lim && ISASCIIDIGIT (*p);  p++)
                lo = (lo < 0 ? 0 : lo * 10) + *p - '0';
              if (p != lim && *p == ',')
                while (++p != lim && ISASCIIDIGIT (*p))
                  hi = (hi < 0 ? 0 : hi * 10) + *p - '0';
              else
                hi = lo;
              if (p == lim || *p != '}'
                  || lo < 0 || RE_DUP_MAX < hi || (0 <= hi && hi < lo))
                goto normal_char;
            }

          minrep = 0;
          /* Cases:
             {M} - exact count
             {M,} - minimum count, maximum is infinity
             {M,N} - M through N */
          FETCH(c, _("unfinished repeat count"));
          if (ISASCIIDIGIT (c))
            {
              minrep = c - '0';
              for (;;)
                {
                  FETCH(c, _("unfinished repeat count"));
                  if (! ISASCIIDIGIT (c))
                    break;
                  minrep = 10 * minrep + c - '0';
                }
            }
          else
            dfaerror(_("malformed repeat count"));
          if (c == ',')
            {
              FETCH (c, _("unfinished repeat count"));
              if (! ISASCIIDIGIT (c))
                maxrep = -1;
              else
                {
                  maxrep = c - '0';
                  for (;;)
                    {
                      FETCH (c, _("unfinished repeat count"));
                      if (! ISASCIIDIGIT (c))
                        break;
                      maxrep = 10 * maxrep + c - '0';
                    }
                  if (0 <= maxrep && maxrep < minrep)
                    dfaerror (_("malformed repeat count"));
                }
            }
          else
            maxrep = minrep;
          if (!(syntax_bits & RE_NO_BK_BRACES))
            {
              if (c != '\\')
                dfaerror(_("malformed repeat count"));
              FETCH(c, _("unfinished repeat count"));
            }
          if (c != '}')
            dfaerror(_("malformed repeat count"));
          laststart = 0;
          return lasttok = REPMN;

        case '|':
          if (syntax_bits & RE_LIMITED_OPS)
            goto normal_char;
          if (backslash != ((syntax_bits & RE_NO_BK_VBAR) == 0))
            goto normal_char;
          laststart = 1;
          return lasttok = OR;

        case '\n':
          if (syntax_bits & RE_LIMITED_OPS
              || backslash
              || !(syntax_bits & RE_NEWLINE_ALT))
            goto normal_char;
          laststart = 1;
          return lasttok = OR;

        case '(':
          if (backslash != ((syntax_bits & RE_NO_BK_PARENS) == 0))
            goto normal_char;
          ++parens;
          laststart = 1;
          return lasttok = LPAREN;

        case ')':
          if (backslash != ((syntax_bits & RE_NO_BK_PARENS) == 0))
            goto normal_char;
          if (parens == 0 && syntax_bits & RE_UNMATCHED_RIGHT_PAREN_ORD)
            goto normal_char;
          --parens;
          laststart = 0;
          return lasttok = RPAREN;

        case '.':
          if (backslash)
            goto normal_char;
#if MBS_SUPPORT
          if (MB_CUR_MAX > 1)
            {
              /* In multibyte environment period must match with a single
                 character not a byte.  So we use ANYCHAR.  */
              laststart = 0;
              return lasttok = ANYCHAR;
            }
#endif /* MBS_SUPPORT */
          zeroset(ccl);
          notset(ccl);
          if (!(syntax_bits & RE_DOT_NEWLINE))
            clrbit(eolbyte, ccl);
          if (syntax_bits & RE_DOT_NOT_NULL)
            clrbit('\0', ccl);
          laststart = 0;
          return lasttok = CSET + charclass_index(ccl);

#ifndef GAWK
        case 's':
        case 'S':
          if (!backslash || (syntax_bits & RE_NO_GNU_OPS))
            goto normal_char;
          zeroset(ccl);
          for (c2 = 0; c2 < NOTCHAR; ++c2)
            if (isspace(c2))
              setbit(c2, ccl);
          if (c == 'S')
            notset(ccl);
          laststart = 0;
          return lasttok = CSET + charclass_index(ccl);
#endif

        case 'w':
        case 'W':
          if (!backslash || (syntax_bits & RE_NO_GNU_OPS))
            goto normal_char;
          zeroset(ccl);
          for (c2 = 0; c2 < NOTCHAR; ++c2)
            if (IS_WORD_CONSTITUENT(c2))
              setbit(c2, ccl);
          if (c == 'W')
            notset(ccl);
          laststart = 0;
          return lasttok = CSET + charclass_index(ccl);

        case '[':
          if (backslash)
            goto normal_char;
          laststart = 0;
          return lasttok = parse_bracket_exp();

        default:
        normal_char:
          laststart = 0;
#if MBS_SUPPORT
          /* For multibyte character sets, folding is done in atom.  Always
             return WCHAR.  */
          if (MB_CUR_MAX > 1)
            return lasttok = WCHAR;
#endif

          if (case_fold && isalpha(c))
            {
              zeroset(ccl);
              setbit_case_fold (c, ccl);
              return lasttok = CSET + charclass_index(ccl);
            }

          return lasttok = c;
        }
    }

  /* The above loop should consume at most a backslash
     and some other character. */
  abort();
  return END;	/* keeps pedantic compilers happy. */
}

/* Recursive descent parser for regular expressions. */

static token tok;		/* Lookahead token. */
static int depth;		/* Current depth of a hypothetical stack
                                   holding deferred productions.  This is
                                   used to determine the depth that will be
                                   required of the real stack later on in
                                   dfaanalyze(). */

static void
addtok_mb (token t, int mbprop)
{
#if MBS_SUPPORT
  if (MB_CUR_MAX > 1)
    {
      REALLOC_IF_NECESSARY(dfa->multibyte_prop, int, dfa->nmultibyte_prop,
                           dfa->tindex);
      dfa->multibyte_prop[dfa->tindex] = mbprop;
    }
#else
  (void) mbprop;
#endif

  REALLOC_IF_NECESSARY(dfa->tokens, token, dfa->talloc, dfa->tindex);
  dfa->tokens[dfa->tindex++] = t;

  switch (t)
    {
    case QMARK:
    case STAR:
    case PLUS:
      break;

    case CAT:
    case OR:
      --depth;
      break;

    default:
      ++dfa->nleaves;
    case EMPTY:
      ++depth;
      break;
    }
  if (depth > dfa->depth)
    dfa->depth = depth;
}

/* Add the given token to the parse tree, maintaining the depth count and
   updating the maximum depth if necessary. */
static void
addtok (token t)
{
#if MBS_SUPPORT
  if (MB_CUR_MAX > 1 && t == MBCSET)
    addtok_mb (MBCSET, ((dfa->nmbcsets - 1) << 2) + 3);
  else
#endif
    addtok_mb (t, 3);
}

#if MBS_SUPPORT
/* We treat a multibyte character as a single atom, so that DFA
   can treat a multibyte character as a single expression.

   e.g. We construct following tree from "<mb1><mb2>".
   <mb1(1st-byte)><mb1(2nd-byte)><CAT><mb1(3rd-byte)><CAT>
   <mb2(1st-byte)><mb2(2nd-byte)><CAT><mb2(3rd-byte)><CAT><CAT> */
static void
addtok_wc (wint_t wc)
{
  unsigned char buf[MB_LEN_MAX];
  mbstate_t s;
  int i;
  memset (&s, 0, sizeof s);
  cur_mb_len = wcrtomb ((char *) buf, wc, &s);

  /* This is merely stop-gap.  When cur_mb_len is 0 or negative,
     buf[0] is undefined, yet skipping the addtok_mb call altogether
     can result in heap corruption.  */
  if (cur_mb_len <= 0)
    buf[0] = 0;

  addtok_mb(buf[0], cur_mb_len == 1 ? 3 : 1);
  for (i = 1; i < cur_mb_len; i++)
    {
      addtok_mb(buf[i], i == cur_mb_len - 1 ? 2 : 0);
      addtok(CAT);
    }
}
#endif

static void
add_utf8_anychar (void)
{
  static const charclass utf8_classes[5] = {
      {  0,  0,  0,  0, ~0, ~0, 0, 0 },            /* 80-bf: non-lead bytes */
      { ~0, ~0, ~0, ~0, 0, 0, 0, 0 },              /* 00-7f: 1-byte sequence */
      {  0,  0,  0,  0,  0,  0, 0xfffffffcU, 0 },  /* c2-df: 2-byte sequence */
      {  0,  0,  0,  0,  0,  0,  0, 0xffff },      /* e0-ef: 3-byte sequence */
      {  0,  0,  0,  0,  0,  0,  0, 0xff0000 }     /* f0-f7: 4-byte sequence */
  };
  const unsigned int n = sizeof (utf8_classes) / sizeof (utf8_classes[0]);
  unsigned int i;

  /* Define the five character classes that are needed below.  */
  if (dfa->utf8_anychar_classes[0] == 0)
    for (i = 0; i < n; i++)
      {
        charclass c;
        memcpy (c, utf8_classes[i], sizeof c);
        if (i == 1)
          {
            if (!(syntax_bits & RE_DOT_NEWLINE))
              clrbit (eolbyte, c);
            if (syntax_bits & RE_DOT_NOT_NULL)
              clrbit ('\0', c);
          }
        dfa->utf8_anychar_classes[i] = CSET + charclass_index(c);
      }

  /* A valid UTF-8 character is

          ([0x00-0x7f]
           |[0xc2-0xdf][0x80-0xbf]
           |[0xe0-0xef[0x80-0xbf][0x80-0xbf]
           |[0xf0-f7][0x80-0xbf][0x80-0xbf][0x80-0xbf])

     which I'll write more concisely "B|CA|DAA|EAAA".  Factor the [0x00-0x7f]
     and you get "B|(C|(D|EA)A)A".  And since the token buffer is in reverse
     Polish notation, you get "B C D E A CAT OR A CAT OR A CAT OR".  */
  for (i = 1; i < n; i++)
    addtok (dfa->utf8_anychar_classes[i]);
  while (--i > 1)
    {
      addtok (dfa->utf8_anychar_classes[0]);
      addtok (CAT);
      addtok (OR);
    }
}

/* The grammar understood by the parser is as follows.

   regexp:
     regexp OR branch
     branch

   branch:
     branch closure
     closure

   closure:
     closure QMARK
     closure STAR
     closure PLUS
     closure REPMN
     atom

   atom:
     <normal character>
     <multibyte character>
     ANYCHAR
     MBCSET
     CSET
     BACKREF
     BEGLINE
     ENDLINE
     BEGWORD
     ENDWORD
     LIMWORD
     NOTLIMWORD
     LPAREN regexp RPAREN
     <empty>

   The parser builds a parse tree in postfix form in an array of tokens. */

static void
atom (void)
{
  if (0)
    {
      /* empty */
    }
#if MBS_SUPPORT
  else if (tok == WCHAR)
    {
      addtok_wc (case_fold ? towlower(wctok) : wctok);
#ifndef GREP
      if (case_fold && iswalpha(wctok))
        {
          addtok_wc (towupper(wctok));
          addtok (OR);
        }
#endif

      tok = lex();
    }

  else if (tok == ANYCHAR && using_utf8())
    {
      /* For UTF-8 expand the period to a series of CSETs that define a valid
         UTF-8 character.  This avoids using the slow multibyte path.  I'm
         pretty sure it would be both profitable and correct to do it for
         any encoding; however, the optimization must be done manually as
         it is done above in add_utf8_anychar.	So, let's start with
         UTF-8: it is the most used, and the structure of the encoding
         makes the correctness more obvious.  */
      add_utf8_anychar();
      tok = lex();
    }
#endif /* MBS_SUPPORT  */

  else if ((tok >= 0 && tok < NOTCHAR) || tok >= CSET || tok == BACKREF
           || tok == BEGLINE || tok == ENDLINE || tok == BEGWORD
#if MBS_SUPPORT
           || tok == ANYCHAR || tok == MBCSET
#endif /* MBS_SUPPORT */
           || tok == ENDWORD || tok == LIMWORD || tok == NOTLIMWORD)
    {
      addtok(tok);
      tok = lex();
    }
  else if (tok == LPAREN)
    {
      tok = lex();
      regexp();
      if (tok != RPAREN)
        dfaerror(_("unbalanced ("));
      tok = lex();
    }
  else
    addtok(EMPTY);
}

/* Return the number of tokens in the given subexpression. */
static int
nsubtoks (int tindex)
{
  int ntoks1;

  switch (dfa->tokens[tindex - 1])
    {
    default:
      return 1;
    case QMARK:
    case STAR:
    case PLUS:
      return 1 + nsubtoks(tindex - 1);
    case CAT:
    case OR:
      ntoks1 = nsubtoks(tindex - 1);
      return 1 + ntoks1 + nsubtoks(tindex - 1 - ntoks1);
    }
}

/* Copy the given subexpression to the top of the tree. */
static void
copytoks (int tindex, int ntokens)
{
  int i;

  for (i = 0; i < ntokens; ++i)
    {
      addtok(dfa->tokens[tindex + i]);
#if MBS_SUPPORT
      /* Update index into multibyte csets.  */
      if (MB_CUR_MAX > 1 && dfa->tokens[tindex + i] == MBCSET)
        dfa->multibyte_prop[dfa->tindex - 1] = dfa->multibyte_prop[tindex + i];
#endif
    }
}

static void
closure (void)
{
  int tindex, ntokens, i;

  atom();
  while (tok == QMARK || tok == STAR || tok == PLUS || tok == REPMN)
    if (tok == REPMN && (minrep || maxrep))
      {
        ntokens = nsubtoks(dfa->tindex);
        tindex = dfa->tindex - ntokens;
        if (maxrep < 0)
          addtok(PLUS);
        if (minrep == 0)
          addtok(QMARK);
        for (i = 1; i < minrep; ++i)
          {
            copytoks(tindex, ntokens);
            addtok(CAT);
          }
        for (; i < maxrep; ++i)
          {
            copytoks(tindex, ntokens);
            addtok(QMARK);
            addtok(CAT);
          }
        tok = lex();
      }
    else if (tok == REPMN)
      {
        dfa->tindex -= nsubtoks(dfa->tindex);
        tok = lex();
        closure();
      }
    else
      {
        addtok(tok);
        tok = lex();
      }
}

static void
branch (void)
{
  closure();
  while (tok != RPAREN && tok != OR && tok >= 0)
    {
      closure();
      addtok(CAT);
    }
}

static void
regexp (void)
{
  branch();
  while (tok == OR)
    {
      tok = lex();
      branch();
      addtok(OR);
    }
}

/* Main entry point for the parser.  S is a string to be parsed, len is the
   length of the string, so s can include NUL characters.  D is a pointer to
   the struct dfa to parse into. */
void
dfaparse (char const *s, size_t len, struct dfa *d)
{
  dfa = d;
  lexptr = s;
  lexleft = len;
  lasttok = END;
  laststart = 1;
  parens = 0;
#ifdef LC_COLLATE
  hard_LC_COLLATE = hard_locale (LC_COLLATE);
#endif
#if MBS_SUPPORT
  if (MB_CUR_MAX > 1)
    {
      cur_mb_len = 0;
      memset(&mbs, 0, sizeof mbs);
    }
#endif /* MBS_SUPPORT  */

  if (! syntax_bits_set)
    dfaerror(_("no syntax specified"));

  tok = lex();
  depth = d->depth;

  regexp();

  if (tok != END)
    dfaerror(_("unbalanced )"));

  addtok(END - d->nregexps);
  addtok(CAT);

  if (d->nregexps)
    addtok(OR);

  ++d->nregexps;
}

/* Some primitives for operating on sets of positions. */

/* Copy one set to another; the destination must be large enough. */
static void
copy (position_set const *src, position_set *dst)
{
  int i;

  for (i = 0; i < src->nelem; ++i)
    dst->elems[i] = src->elems[i];
  dst->nelem = src->nelem;
}

/* Insert a position in a set.  Position sets are maintained in sorted
   order according to index.  If position already exists in the set with
   the same index then their constraints are logically or'd together.
   S->elems must point to an array large enough to hold the resulting set. */
static void
insert (position p, position_set *s)
{
  int count = s->nelem;
  int lo = 0, hi = count;
  while (lo < hi)
    {
      int mid = ((unsigned) lo + (unsigned) hi) >> 1;
      if (s->elems[mid].index < p.index)
        lo = mid + 1;
      else
        hi = mid;
    }

  if (lo < count && p.index == s->elems[lo].index)
    s->elems[lo].constraint |= p.constraint;
  else
    {
      int i;
      for (i = count; i > lo; i--)
        s->elems[i] = s->elems[i - 1];
      s->elems[lo] = p;
      ++s->nelem;
    }
}

/* Merge two sets of positions into a third.  The result is exactly as if
   the positions of both sets were inserted into an initially empty set. */
static void
merge (position_set const *s1, position_set const *s2, position_set *m)
{
  int i = 0, j = 0;

  m->nelem = 0;
  while (i < s1->nelem && j < s2->nelem)
    if (s1->elems[i].index > s2->elems[j].index)
      m->elems[m->nelem++] = s1->elems[i++];
    else if (s1->elems[i].index < s2->elems[j].index)
      m->elems[m->nelem++] = s2->elems[j++];
    else
      {
        m->elems[m->nelem] = s1->elems[i++];
        m->elems[m->nelem++].constraint |= s2->elems[j++].constraint;
      }
  while (i < s1->nelem)
    m->elems[m->nelem++] = s1->elems[i++];
  while (j < s2->nelem)
    m->elems[m->nelem++] = s2->elems[j++];
}

/* Delete a position from a set. */
static void
delete (position p, position_set *s)
{
  int i;

  for (i = 0; i < s->nelem; ++i)
    if (p.index == s->elems[i].index)
      break;
  if (i < s->nelem)
    for (--s->nelem; i < s->nelem; ++i)
      s->elems[i] = s->elems[i + 1];
}

/* Find the index of the state corresponding to the given position set with
   the given preceding context, or create a new state if there is no such
   state.  Newline and letter tell whether we got here on a newline or
   letter, respectively. */
static int
state_index (struct dfa *d, position_set const *s, int newline, int letter)
{
  int hash = 0;
  int constraint;
  int i, j;

  newline = newline ? 1 : 0;
  letter = letter ? 1 : 0;

  for (i = 0; i < s->nelem; ++i)
    hash ^= s->elems[i].index + s->elems[i].constraint;

  /* Try to find a state that exactly matches the proposed one. */
  for (i = 0; i < d->sindex; ++i)
    {
      if (hash != d->states[i].hash || s->nelem != d->states[i].elems.nelem
          || newline != d->states[i].newline || letter != d->states[i].letter)
        continue;
      for (j = 0; j < s->nelem; ++j)
        if (s->elems[j].constraint
            != d->states[i].elems.elems[j].constraint
            || s->elems[j].index != d->states[i].elems.elems[j].index)
          break;
      if (j == s->nelem)
        return i;
    }

  /* We'll have to create a new state. */
  REALLOC_IF_NECESSARY(d->states, dfa_state, d->salloc, d->sindex);
  d->states[i].hash = hash;
  MALLOC(d->states[i].elems.elems, position, s->nelem);
  copy(s, &d->states[i].elems);
  d->states[i].newline = newline;
  d->states[i].letter = letter;
  d->states[i].backref = 0;
  d->states[i].constraint = 0;
  d->states[i].first_end = 0;
#if MBS_SUPPORT
  d->states[i].mbps.nelem = 0;
  d->states[i].mbps.elems = NULL;
#endif
  for (j = 0; j < s->nelem; ++j)
    if (d->tokens[s->elems[j].index] < 0)
      {
        constraint = s->elems[j].constraint;
        if (SUCCEEDS_IN_CONTEXT(constraint, newline, 0, letter, 0)
            || SUCCEEDS_IN_CONTEXT(constraint, newline, 0, letter, 1)
            || SUCCEEDS_IN_CONTEXT(constraint, newline, 1, letter, 0)
            || SUCCEEDS_IN_CONTEXT(constraint, newline, 1, letter, 1))
          d->states[i].constraint |= constraint;
        if (! d->states[i].first_end)
          d->states[i].first_end = d->tokens[s->elems[j].index];
      }
    else if (d->tokens[s->elems[j].index] == BACKREF)
      {
        d->states[i].constraint = NO_CONSTRAINT;
        d->states[i].backref = 1;
      }

  ++d->sindex;

  return i;
}

/* Find the epsilon closure of a set of positions.  If any position of the set
   contains a symbol that matches the empty string in some context, replace
   that position with the elements of its follow labeled with an appropriate
   constraint.  Repeat exhaustively until no funny positions are left.
   S->elems must be large enough to hold the result. */
static void
epsclosure (position_set *s, struct dfa const *d)
{
  int i, j;
  char *visited;	/* array of booleans, enough to use char, not int */
  position p, old;

  CALLOC(visited, char, d->tindex);

  for (i = 0; i < s->nelem; ++i)
    if (d->tokens[s->elems[i].index] >= NOTCHAR
        && d->tokens[s->elems[i].index] != BACKREF
#if MBS_SUPPORT
        && d->tokens[s->elems[i].index] != ANYCHAR
        && d->tokens[s->elems[i].index] != MBCSET
#endif
        && d->tokens[s->elems[i].index] < CSET)
      {
        old = s->elems[i];
        p.constraint = old.constraint;
        delete(s->elems[i], s);
        if (visited[old.index])
          {
            --i;
            continue;
          }
        visited[old.index] = 1;
        switch (d->tokens[old.index])
          {
          case BEGLINE:
            p.constraint &= BEGLINE_CONSTRAINT;
            break;
          case ENDLINE:
            p.constraint &= ENDLINE_CONSTRAINT;
            break;
          case BEGWORD:
            p.constraint &= BEGWORD_CONSTRAINT;
            break;
          case ENDWORD:
            p.constraint &= ENDWORD_CONSTRAINT;
            break;
          case LIMWORD:
            p.constraint &= LIMWORD_CONSTRAINT;
            break;
          case NOTLIMWORD:
            p.constraint &= NOTLIMWORD_CONSTRAINT;
            break;
          default:
            break;
          }
        for (j = 0; j < d->follows[old.index].nelem; ++j)
          {
            p.index = d->follows[old.index].elems[j].index;
            insert(p, s);
          }
        /* Force rescan to start at the beginning. */
        i = -1;
      }

  free(visited);
}

/* Perform bottom-up analysis on the parse tree, computing various functions.
   Note that at this point, we're pretending constructs like \< are real
   characters rather than constraints on what can follow them.

   Nullable:  A node is nullable if it is at the root of a regexp that can
   match the empty string.
   *  EMPTY leaves are nullable.
   * No other leaf is nullable.
   * A QMARK or STAR node is nullable.
   * A PLUS node is nullable if its argument is nullable.
   * A CAT node is nullable if both its arguments are nullable.
   * An OR node is nullable if either argument is nullable.

   Firstpos:  The firstpos of a node is the set of positions (nonempty leaves)
   that could correspond to the first character of a string matching the
   regexp rooted at the given node.
   * EMPTY leaves have empty firstpos.
   * The firstpos of a nonempty leaf is that leaf itself.
   * The firstpos of a QMARK, STAR, or PLUS node is the firstpos of its
     argument.
   * The firstpos of a CAT node is the firstpos of the left argument, union
     the firstpos of the right if the left argument is nullable.
   * The firstpos of an OR node is the union of firstpos of each argument.

   Lastpos:  The lastpos of a node is the set of positions that could
   correspond to the last character of a string matching the regexp at
   the given node.
   * EMPTY leaves have empty lastpos.
   * The lastpos of a nonempty leaf is that leaf itself.
   * The lastpos of a QMARK, STAR, or PLUS node is the lastpos of its
     argument.
   * The lastpos of a CAT node is the lastpos of its right argument, union
     the lastpos of the left if the right argument is nullable.
   * The lastpos of an OR node is the union of the lastpos of each argument.

   Follow:  The follow of a position is the set of positions that could
   correspond to the character following a character matching the node in
   a string matching the regexp.  At this point we consider special symbols
   that match the empty string in some context to be just normal characters.
   Later, if we find that a special symbol is in a follow set, we will
   replace it with the elements of its follow, labeled with an appropriate
   constraint.
   * Every node in the firstpos of the argument of a STAR or PLUS node is in
     the follow of every node in the lastpos.
   * Every node in the firstpos of the second argument of a CAT node is in
     the follow of every node in the lastpos of the first argument.

   Because of the postfix representation of the parse tree, the depth-first
   analysis is conveniently done by a linear scan with the aid of a stack.
   Sets are stored as arrays of the elements, obeying a stack-like allocation
   scheme; the number of elements in each set deeper in the stack can be
   used to determine the address of a particular set's array. */
void
dfaanalyze (struct dfa *d, int searchflag)
{
  int *nullable;		/* Nullable stack. */
  int *nfirstpos;		/* Element count stack for firstpos sets. */
  position *firstpos;		/* Array where firstpos elements are stored. */
  int *nlastpos;		/* Element count stack for lastpos sets. */
  position *lastpos;		/* Array where lastpos elements are stored. */
  int *nalloc;			/* Sizes of arrays allocated to follow sets. */
  position_set tmp;		/* Temporary set for merging sets. */
  position_set merged;		/* Result of merging sets. */
  int wants_newline;		/* True if some position wants newline info. */
  int *o_nullable;
  int *o_nfirst, *o_nlast;
  position *o_firstpos, *o_lastpos;
  int i, j;
  position *pos;

#ifdef DEBUG
  fprintf(stderr, "dfaanalyze:\n");
  for (i = 0; i < d->tindex; ++i)
    {
      fprintf(stderr, " %d:", i);
      prtok(d->tokens[i]);
    }
  putc('\n', stderr);
#endif

  d->searchflag = searchflag;

  MALLOC(nullable, int, d->depth);
  o_nullable = nullable;
  MALLOC(nfirstpos, int, d->depth);
  o_nfirst = nfirstpos;
  MALLOC(firstpos, position, d->nleaves);
  o_firstpos = firstpos, firstpos += d->nleaves;
  MALLOC(nlastpos, int, d->depth);
  o_nlast = nlastpos;
  MALLOC(lastpos, position, d->nleaves);
  o_lastpos = lastpos, lastpos += d->nleaves;
  CALLOC(nalloc, int, d->tindex);
  MALLOC(merged.elems, position, d->nleaves);

  CALLOC(d->follows, position_set, d->tindex);

  for (i = 0; i < d->tindex; ++i)
#ifdef DEBUG
    {				/* Nonsyntactic #ifdef goo... */
#endif
    switch (d->tokens[i])
      {
      case EMPTY:
        /* The empty set is nullable. */
        *nullable++ = 1;

        /* The firstpos and lastpos of the empty leaf are both empty. */
        *nfirstpos++ = *nlastpos++ = 0;
        break;

      case STAR:
      case PLUS:
        /* Every element in the firstpos of the argument is in the follow
           of every element in the lastpos. */
        tmp.nelem = nfirstpos[-1];
        tmp.elems = firstpos;
        pos = lastpos;
        for (j = 0; j < nlastpos[-1]; ++j)
          {
            merge(&tmp, &d->follows[pos[j].index], &merged);
            REALLOC_IF_NECESSARY(d->follows[pos[j].index].elems, position,
                                 nalloc[pos[j].index], merged.nelem - 1);
            copy(&merged, &d->follows[pos[j].index]);
          }

      case QMARK:
        /* A QMARK or STAR node is automatically nullable. */
        if (d->tokens[i] != PLUS)
          nullable[-1] = 1;
        break;

      case CAT:
        /* Every element in the firstpos of the second argument is in the
           follow of every element in the lastpos of the first argument. */
        tmp.nelem = nfirstpos[-1];
        tmp.elems = firstpos;
        pos = lastpos + nlastpos[-1];
        for (j = 0; j < nlastpos[-2]; ++j)
          {
            merge(&tmp, &d->follows[pos[j].index], &merged);
            REALLOC_IF_NECESSARY(d->follows[pos[j].index].elems, position,
                                 nalloc[pos[j].index], merged.nelem - 1);
            copy(&merged, &d->follows[pos[j].index]);
          }

        /* The firstpos of a CAT node is the firstpos of the first argument,
           union that of the second argument if the first is nullable. */
        if (nullable[-2])
          nfirstpos[-2] += nfirstpos[-1];
        else
          firstpos += nfirstpos[-1];
        --nfirstpos;

        /* The lastpos of a CAT node is the lastpos of the second argument,
           union that of the first argument if the second is nullable. */
        if (nullable[-1])
          nlastpos[-2] += nlastpos[-1];
        else
          {
            pos = lastpos + nlastpos[-2];
            for (j = nlastpos[-1] - 1; j >= 0; --j)
              pos[j] = lastpos[j];
            lastpos += nlastpos[-2];
            nlastpos[-2] = nlastpos[-1];
          }
        --nlastpos;

        /* A CAT node is nullable if both arguments are nullable. */
        nullable[-2] = nullable[-1] && nullable[-2];
        --nullable;
        break;

      case OR:
        /* The firstpos is the union of the firstpos of each argument. */
        nfirstpos[-2] += nfirstpos[-1];
        --nfirstpos;

        /* The lastpos is the union of the lastpos of each argument. */
        nlastpos[-2] += nlastpos[-1];
        --nlastpos;

        /* An OR node is nullable if either argument is nullable. */
        nullable[-2] = nullable[-1] || nullable[-2];
        --nullable;
        break;

      default:
        /* Anything else is a nonempty position.  (Note that special
           constructs like \< are treated as nonempty strings here;
           an "epsilon closure" effectively makes them nullable later.
           Backreferences have to get a real position so we can detect
           transitions on them later.  But they are nullable. */
        *nullable++ = d->tokens[i] == BACKREF;

        /* This position is in its own firstpos and lastpos. */
        *nfirstpos++ = *nlastpos++ = 1;
        --firstpos, --lastpos;
        firstpos->index = lastpos->index = i;
        firstpos->constraint = lastpos->constraint = NO_CONSTRAINT;

        /* Allocate the follow set for this position. */
        nalloc[i] = 1;
        MALLOC(d->follows[i].elems, position, nalloc[i]);
        break;
      }
#ifdef DEBUG
    /* ... balance the above nonsyntactic #ifdef goo... */
      fprintf(stderr, "node %d:", i);
      prtok(d->tokens[i]);
      putc('\n', stderr);
      fprintf(stderr, nullable[-1] ? " nullable: yes\n" : " nullable: no\n");
      fprintf(stderr, " firstpos:");
      for (j = nfirstpos[-1] - 1; j >= 0; --j)
        {
          fprintf(stderr, " %d:", firstpos[j].index);
          prtok(d->tokens[firstpos[j].index]);
        }
      fprintf(stderr, "\n lastpos:");
      for (j = nlastpos[-1] - 1; j >= 0; --j)
        {
          fprintf(stderr, " %d:", lastpos[j].index);
          prtok(d->tokens[lastpos[j].index]);
        }
      putc('\n', stderr);
    }
#endif

  /* For each follow set that is the follow set of a real position, replace
     it with its epsilon closure. */
  for (i = 0; i < d->tindex; ++i)
    if (d->tokens[i] < NOTCHAR || d->tokens[i] == BACKREF
#if MBS_SUPPORT
        || d->tokens[i] == ANYCHAR
        || d->tokens[i] == MBCSET
#endif
        || d->tokens[i] >= CSET)
      {
#ifdef DEBUG
        fprintf(stderr, "follows(%d:", i);
        prtok(d->tokens[i]);
        fprintf(stderr, "):");
        for (j = d->follows[i].nelem - 1; j >= 0; --j)
          {
            fprintf(stderr, " %d:", d->follows[i].elems[j].index);
            prtok(d->tokens[d->follows[i].elems[j].index]);
          }
        putc('\n', stderr);
#endif
        copy(&d->follows[i], &merged);
        epsclosure(&merged, d);
        if (d->follows[i].nelem < merged.nelem)
          REALLOC(d->follows[i].elems, position, merged.nelem);
        copy(&merged, &d->follows[i]);
      }

  /* Get the epsilon closure of the firstpos of the regexp.  The result will
     be the set of positions of state 0. */
  merged.nelem = 0;
  for (i = 0; i < nfirstpos[-1]; ++i)
    insert(firstpos[i], &merged);
  epsclosure(&merged, d);

  /* Check if any of the positions of state 0 will want newline context. */
  wants_newline = 0;
  for (i = 0; i < merged.nelem; ++i)
    if (PREV_NEWLINE_DEPENDENT(merged.elems[i].constraint))
      wants_newline = 1;

  /* Build the initial state. */
  d->salloc = 1;
  d->sindex = 0;
  MALLOC(d->states, dfa_state, d->salloc);
  state_index(d, &merged, wants_newline, 0);

  free(o_nullable);
  free(o_nfirst);
  free(o_firstpos);
  free(o_nlast);
  free(o_lastpos);
  free(nalloc);
  free(merged.elems);
}

/* Find, for each character, the transition out of state s of d, and store
   it in the appropriate slot of trans.

   We divide the positions of s into groups (positions can appear in more
   than one group).  Each group is labeled with a set of characters that
   every position in the group matches (taking into account, if necessary,
   preceding context information of s).  For each group, find the union
   of the its elements' follows.  This set is the set of positions of the
   new state.  For each character in the group's label, set the transition
   on this character to be to a state corresponding to the set's positions,
   and its associated backward context information, if necessary.

   If we are building a searching matcher, we include the positions of state
   0 in every state.

   The collection of groups is constructed by building an equivalence-class
   partition of the positions of s.

   For each position, find the set of characters C that it matches.  Eliminate
   any characters from C that fail on grounds of backward context.

   Search through the groups, looking for a group whose label L has nonempty
   intersection with C.  If L - C is nonempty, create a new group labeled
   L - C and having the same positions as the current group, and set L to
   the intersection of L and C.  Insert the position in this group, set
   C = C - L, and resume scanning.

   If after comparing with every group there are characters remaining in C,
   create a new group labeled with the characters of C and insert this
   position in that group. */
void
dfastate (int s, struct dfa *d, int trans[])
{
  position_set *grps;		/* As many as will ever be needed. */
  charclass *labels;		/* Labels corresponding to the groups. */
  int ngrps = 0;		/* Number of groups actually used. */
  position pos;			/* Current position being considered. */
  charclass matches;		/* Set of matching characters. */
  int matchesf;			/* True if matches is nonempty. */
  charclass intersect;		/* Intersection with some label set. */
  int intersectf;		/* True if intersect is nonempty. */
  charclass leftovers;		/* Stuff in the label that didn't match. */
  int leftoversf;		/* True if leftovers is nonempty. */
  static charclass letters;	/* Set of characters considered letters. */
  static charclass newline;	/* Set of characters that aren't newline. */
  position_set follows;		/* Union of the follows of some group. */
  position_set tmp;		/* Temporary space for merging sets. */
  int state;			/* New state. */
  int wants_newline;		/* New state wants to know newline context. */
  int state_newline;		/* New state on a newline transition. */
  int wants_letter;		/* New state wants to know letter context. */
  int state_letter;		/* New state on a letter transition. */
  static int initialized;	/* Flag for static initialization. */
#if MBS_SUPPORT
  int next_isnt_1st_byte = 0;	/* Flag if we can't add state0.  */
#endif
  int i, j, k;

  grps = xnmalloc (NOTCHAR, sizeof *grps);
  labels = xnmalloc (NOTCHAR, sizeof *labels);

  /* Initialize the set of letters, if necessary. */
  if (! initialized)
    {
      initialized = 1;
      for (i = 0; i < NOTCHAR; ++i)
        if (IS_WORD_CONSTITUENT(i))
          setbit(i, letters);
      setbit(eolbyte, newline);
    }

  zeroset(matches);

  for (i = 0; i < d->states[s].elems.nelem; ++i)
    {
      pos = d->states[s].elems.elems[i];
      if (d->tokens[pos.index] >= 0 && d->tokens[pos.index] < NOTCHAR)
        setbit(d->tokens[pos.index], matches);
      else if (d->tokens[pos.index] >= CSET)
        copyset(d->charclasses[d->tokens[pos.index] - CSET], matches);
#if MBS_SUPPORT
      else if (d->tokens[pos.index] == ANYCHAR
               || d->tokens[pos.index] == MBCSET)
      /* MB_CUR_MAX > 1  */
        {
          /* ANYCHAR and MBCSET must match with a single character, so we
             must put it to d->states[s].mbps, which contains the positions
             which can match with a single character not a byte.  */
          if (d->states[s].mbps.nelem == 0)
            {
              MALLOC(d->states[s].mbps.elems, position,
                     d->states[s].elems.nelem);
            }
          insert(pos, &(d->states[s].mbps));
          continue;
        }
#endif /* MBS_SUPPORT */
      else
        continue;

      /* Some characters may need to be eliminated from matches because
         they fail in the current context. */
      if (pos.constraint != 0xFF)
        {
          if (! MATCHES_NEWLINE_CONTEXT(pos.constraint,
                                         d->states[s].newline, 1))
            clrbit(eolbyte, matches);
          if (! MATCHES_NEWLINE_CONTEXT(pos.constraint,
                                         d->states[s].newline, 0))
            for (j = 0; j < CHARCLASS_INTS; ++j)
              matches[j] &= newline[j];
          if (! MATCHES_LETTER_CONTEXT(pos.constraint,
                                        d->states[s].letter, 1))
            for (j = 0; j < CHARCLASS_INTS; ++j)
              matches[j] &= ~letters[j];
          if (! MATCHES_LETTER_CONTEXT(pos.constraint,
                                        d->states[s].letter, 0))
            for (j = 0; j < CHARCLASS_INTS; ++j)
              matches[j] &= letters[j];

          /* If there are no characters left, there's no point in going on. */
          for (j = 0; j < CHARCLASS_INTS && !matches[j]; ++j)
            continue;
          if (j == CHARCLASS_INTS)
            continue;
        }

      for (j = 0; j < ngrps; ++j)
        {
          /* If matches contains a single character only, and the current
             group's label doesn't contain that character, go on to the
             next group. */
          if (d->tokens[pos.index] >= 0 && d->tokens[pos.index] < NOTCHAR
              && !tstbit(d->tokens[pos.index], labels[j]))
            continue;

          /* Check if this group's label has a nonempty intersection with
             matches. */
          intersectf = 0;
          for (k = 0; k < CHARCLASS_INTS; ++k)
            (intersect[k] = matches[k] & labels[j][k]) ? (intersectf = 1) : 0;
          if (! intersectf)
            continue;

          /* It does; now find the set differences both ways. */
          leftoversf = matchesf = 0;
          for (k = 0; k < CHARCLASS_INTS; ++k)
            {
              /* Even an optimizing compiler can't know this for sure. */
              int match = matches[k], label = labels[j][k];

              (leftovers[k] = ~match & label) ? (leftoversf = 1) : 0;
              (matches[k] = match & ~label) ? (matchesf = 1) : 0;
            }

          /* If there were leftovers, create a new group labeled with them. */
          if (leftoversf)
            {
              copyset(leftovers, labels[ngrps]);
              copyset(intersect, labels[j]);
              MALLOC(grps[ngrps].elems, position, d->nleaves);
              copy(&grps[j], &grps[ngrps]);
              ++ngrps;
            }

          /* Put the position in the current group.  Note that there is no
             reason to call insert() here. */
          grps[j].elems[grps[j].nelem++] = pos;

          /* If every character matching the current position has been
             accounted for, we're done. */
          if (! matchesf)
            break;
        }

      /* If we've passed the last group, and there are still characters
         unaccounted for, then we'll have to create a new group. */
      if (j == ngrps)
        {
          copyset(matches, labels[ngrps]);
          zeroset(matches);
          MALLOC(grps[ngrps].elems, position, d->nleaves);
          grps[ngrps].nelem = 1;
          grps[ngrps].elems[0] = pos;
          ++ngrps;
        }
    }

  MALLOC(follows.elems, position, d->nleaves);
  MALLOC(tmp.elems, position, d->nleaves);

  /* If we are a searching matcher, the default transition is to a state
     containing the positions of state 0, otherwise the default transition
     is to fail miserably. */
  if (d->searchflag)
    {
      wants_newline = 0;
      wants_letter = 0;
      for (i = 0; i < d->states[0].elems.nelem; ++i)
        {
          if (PREV_NEWLINE_DEPENDENT(d->states[0].elems.elems[i].constraint))
            wants_newline = 1;
          if (PREV_LETTER_DEPENDENT(d->states[0].elems.elems[i].constraint))
            wants_letter = 1;
        }
      copy(&d->states[0].elems, &follows);
      state = state_index(d, &follows, 0, 0);
      if (wants_newline)
        state_newline = state_index(d, &follows, 1, 0);
      else
        state_newline = state;
      if (wants_letter)
        state_letter = state_index(d, &follows, 0, 1);
      else
        state_letter = state;
      for (i = 0; i < NOTCHAR; ++i)
        trans[i] = (IS_WORD_CONSTITUENT(i)) ? state_letter : state;
      trans[eolbyte] = state_newline;
    }
  else
    for (i = 0; i < NOTCHAR; ++i)
      trans[i] = -1;

  for (i = 0; i < ngrps; ++i)
    {
      follows.nelem = 0;

      /* Find the union of the follows of the positions of the group.
         This is a hideously inefficient loop.  Fix it someday. */
      for (j = 0; j < grps[i].nelem; ++j)
        for (k = 0; k < d->follows[grps[i].elems[j].index].nelem; ++k)
          insert(d->follows[grps[i].elems[j].index].elems[k], &follows);

#if MBS_SUPPORT
      if (d->mb_cur_max > 1)
        {
          /* If a token in follows.elems is not 1st byte of a multibyte
             character, or the states of follows must accept the bytes
             which are not 1st byte of the multibyte character.
             Then, if a state of follows encounter a byte, it must not be
             a 1st byte of a multibyte character nor single byte character.
             We cansel to add state[0].follows to next state, because
             state[0] must accept 1st-byte

             For example, we assume <sb a> is a certain single byte
             character, <mb A> is a certain multibyte character, and the
             codepoint of <sb a> equals the 2nd byte of the codepoint of
             <mb A>.
             When state[0] accepts <sb a>, state[i] transit to state[i+1]
             by accepting accepts 1st byte of <mb A>, and state[i+1]
             accepts 2nd byte of <mb A>, if state[i+1] encounter the
             codepoint of <sb a>, it must not be <sb a> but 2nd byte of
             <mb A>, so we can not add state[0].  */

          next_isnt_1st_byte = 0;
          for (j = 0; j < follows.nelem; ++j)
            {
              if (!(d->multibyte_prop[follows.elems[j].index] & 1))
                {
                  next_isnt_1st_byte = 1;
                  break;
                }
            }
        }
#endif

      /* If we are building a searching matcher, throw in the positions
         of state 0 as well. */
#if MBS_SUPPORT
      if (d->searchflag && (d->mb_cur_max == 1 || !next_isnt_1st_byte))
#else
      if (d->searchflag)
#endif
        for (j = 0; j < d->states[0].elems.nelem; ++j)
          insert(d->states[0].elems.elems[j], &follows);

      /* Find out if the new state will want any context information. */
      wants_newline = 0;
      if (tstbit(eolbyte, labels[i]))
        for (j = 0; j < follows.nelem; ++j)
          if (PREV_NEWLINE_DEPENDENT(follows.elems[j].constraint))
            wants_newline = 1;

      wants_letter = 0;
      for (j = 0; j < CHARCLASS_INTS; ++j)
        if (labels[i][j] & letters[j])
          break;
      if (j < CHARCLASS_INTS)
        for (j = 0; j < follows.nelem; ++j)
          if (PREV_LETTER_DEPENDENT(follows.elems[j].constraint))
            wants_letter = 1;

      /* Find the state(s) corresponding to the union of the follows. */
      state = state_index(d, &follows, 0, 0);
      if (wants_newline)
        state_newline = state_index(d, &follows, 1, 0);
      else
        state_newline = state;
      if (wants_letter)
        state_letter = state_index(d, &follows, 0, 1);
      else
        state_letter = state;

      /* Set the transitions for each character in the current label. */
      for (j = 0; j < CHARCLASS_INTS; ++j)
        for (k = 0; k < INTBITS; ++k)
          if (labels[i][j] & 1 << k)
            {
              int c = j * INTBITS + k;

              if (c == eolbyte)
                trans[c] = state_newline;
              else if (IS_WORD_CONSTITUENT(c))
                trans[c] = state_letter;
              else if (c < NOTCHAR)
                trans[c] = state;
            }
    }

  for (i = 0; i < ngrps; ++i)
    free(grps[i].elems);
  free(follows.elems);
  free(tmp.elems);
  free(grps);
  free(labels);
}

/* Some routines for manipulating a compiled dfa's transition tables.
   Each state may or may not have a transition table; if it does, and it
   is a non-accepting state, then d->trans[state] points to its table.
   If it is an accepting state then d->fails[state] points to its table.
   If it has no table at all, then d->trans[state] is NULL.
   TODO: Improve this comment, get rid of the unnecessary redundancy. */

static void
build_state (int s, struct dfa *d)
{
  int *trans;			/* The new transition table. */
  int i;

  /* Set an upper limit on the number of transition tables that will ever
     exist at once.  1024 is arbitrary.  The idea is that the frequently
     used transition tables will be quickly rebuilt, whereas the ones that
     were only needed once or twice will be cleared away. */
  if (d->trcount >= 1024)
    {
      for (i = 0; i < d->tralloc; ++i)
        {
          free(d->trans[i]);
          free(d->fails[i]);
          d->trans[i] = d->fails[i] = NULL;
        }
      d->trcount = 0;
    }

  ++d->trcount;

  /* Set up the success bits for this state. */
  d->success[s] = 0;
  if (ACCEPTS_IN_CONTEXT(d->states[s].newline, 1, d->states[s].letter, 0,
      s, *d))
    d->success[s] |= 4;
  if (ACCEPTS_IN_CONTEXT(d->states[s].newline, 0, d->states[s].letter, 1,
      s, *d))
    d->success[s] |= 2;
  if (ACCEPTS_IN_CONTEXT(d->states[s].newline, 0, d->states[s].letter, 0,
      s, *d))
    d->success[s] |= 1;

  MALLOC(trans, int, NOTCHAR);
  dfastate(s, d, trans);

  /* Now go through the new transition table, and make sure that the trans
     and fail arrays are allocated large enough to hold a pointer for the
     largest state mentioned in the table. */
  for (i = 0; i < NOTCHAR; ++i)
    if (trans[i] >= d->tralloc)
      {
        int oldalloc = d->tralloc;

        while (trans[i] >= d->tralloc)
          d->tralloc *= 2;
        REALLOC(d->realtrans, int *, d->tralloc + 1);
        d->trans = d->realtrans + 1;
        REALLOC(d->fails, int *, d->tralloc);
        REALLOC(d->success, int, d->tralloc);
        REALLOC(d->newlines, int, d->tralloc);
        while (oldalloc < d->tralloc)
          {
            d->trans[oldalloc] = NULL;
            d->fails[oldalloc++] = NULL;
          }
      }

  /* Keep the newline transition in a special place so we can use it as
     a sentinel. */
  d->newlines[s] = trans[eolbyte];
  trans[eolbyte] = -1;

  if (ACCEPTING(s, *d))
    d->fails[s] = trans;
  else
    d->trans[s] = trans;
}

static void
build_state_zero (struct dfa *d)
{
  d->tralloc = 1;
  d->trcount = 0;
  CALLOC(d->realtrans, int *, d->tralloc + 1);
  d->trans = d->realtrans + 1;
  CALLOC(d->fails, int *, d->tralloc);
  MALLOC(d->success, int, d->tralloc);
  MALLOC(d->newlines, int, d->tralloc);
  build_state(0, d);
}

#if MBS_SUPPORT
/* Multibyte character handling sub-routines for dfaexec.  */

/* Initial state may encounter the byte which is not a single byte character
   nor 1st byte of a multibyte character.  But it is incorrect for initial
   state to accept such a byte.
   For example, in sjis encoding the regular expression like "\\" accepts
   the codepoint 0x5c, but should not accept the 2nd byte of the codepoint
   0x815c. Then Initial state must skip the bytes which are not a single byte
   character nor 1st byte of a multibyte character.  */
#define SKIP_REMAINS_MB_IF_INITIAL_STATE(s, p)		\
  if (s == 0)						\
    {							\
      while (inputwcs[p - buf_begin] == 0		\
            && mblen_buf[p - buf_begin] > 0		\
            && (unsigned char const *) p < buf_end)	\
        ++p;						\
      if ((char *) p >= end)				\
        {						\
          free(mblen_buf);				\
          free(inputwcs);				\
          *end = saved_end;				\
          return NULL;					\
        }						\
    }

static void
realloc_trans_if_necessary(struct dfa *d, int new_state)
{
  /* Make sure that the trans and fail arrays are allocated large enough
     to hold a pointer for the new state. */
  if (new_state >= d->tralloc)
    {
      int oldalloc = d->tralloc;

      while (new_state >= d->tralloc)
        d->tralloc *= 2;
      REALLOC(d->realtrans, int *, d->tralloc + 1);
      d->trans = d->realtrans + 1;
      REALLOC(d->fails, int *, d->tralloc);
      REALLOC(d->success, int, d->tralloc);
      REALLOC(d->newlines, int, d->tralloc);
      while (oldalloc < d->tralloc)
        {
          d->trans[oldalloc] = NULL;
          d->fails[oldalloc++] = NULL;
        }
    }
}

/* Return values of transit_state_singlebyte(), and
   transit_state_consume_1char.  */
typedef enum
{
  TRANSIT_STATE_IN_PROGRESS,	/* State transition has not finished.  */
  TRANSIT_STATE_DONE,		/* State transition has finished.  */
  TRANSIT_STATE_END_BUFFER	/* Reach the end of the buffer.  */
} status_transit_state;

/* Consume a single byte and transit state from 's' to '*next_state'.
   This function is almost same as the state transition routin in dfaexec().
   But state transition is done just once, otherwise matching succeed or
   reach the end of the buffer.  */
static status_transit_state
transit_state_singlebyte (struct dfa *d, int s, unsigned char const *p,
                                  int *next_state)
{
  int *t;
  int works = s;

  status_transit_state rval = TRANSIT_STATE_IN_PROGRESS;

  while (rval == TRANSIT_STATE_IN_PROGRESS)
    {
      if ((t = d->trans[works]) != NULL)
        {
          works = t[*p];
          rval = TRANSIT_STATE_DONE;
          if (works < 0)
            works = 0;
        }
      else if (works < 0)
        {
          if (p == buf_end)
            {
              /* At the moment, it must not happen.  */
              abort ();
            }
          works = 0;
        }
      else if (d->fails[works])
        {
          works = d->fails[works][*p];
          rval = TRANSIT_STATE_DONE;
        }
      else
        {
          build_state(works, d);
        }
    }
  *next_state = works;
  return rval;
}

/* Check whether period can match or not in the current context.  If it can,
   return the amount of the bytes with which period can match, otherwise
   return 0.
   `pos' is the position of the period.  `idx' is the index from the
   buf_begin, and it is the current position in the buffer.  */
static int
match_anychar (struct dfa *d, int s, position pos, int idx)
{
  int newline = 0;
  int letter = 0;
  wchar_t wc;
  int mbclen;

  wc = inputwcs[idx];
  mbclen = (mblen_buf[idx] == 0)? 1 : mblen_buf[idx];

  /* Check context.  */
  if (wc == (wchar_t)eolbyte)
    {
      if (!(syntax_bits & RE_DOT_NEWLINE))
        return 0;
      newline = 1;
    }
  else if (wc == (wchar_t)'\0')
    {
      if (syntax_bits & RE_DOT_NOT_NULL)
        return 0;
      newline = 1;
    }

  if (iswalnum(wc) || wc == L'_')
    letter = 1;

  if (!SUCCEEDS_IN_CONTEXT(pos.constraint, d->states[s].newline,
                           newline, d->states[s].letter, letter))
    return 0;

  return mbclen;
}

/* Check whether bracket expression can match or not in the current context.
   If it can, return the amount of the bytes with which expression can match,
   otherwise return 0.
   `pos' is the position of the bracket expression.  `idx' is the index
   from the buf_begin, and it is the current position in the buffer.  */
static int
match_mb_charset (struct dfa *d, int s, position pos, int idx)
{
  int i;
  int match;		/* Flag which represent that matching succeed.  */
  int match_len;	/* Length of the character (or collating element)
                           with which this operator match.  */
  int op_len;		/* Length of the operator.  */
  char buffer[128];
  wchar_t wcbuf[6];

  /* Pointer to the structure to which we are currently refering.  */
  struct mb_char_classes *work_mbc;

  int newline = 0;
  int letter = 0;
  wchar_t wc;		/* Current refering character.  */

  wc = inputwcs[idx];

  /* Check context.  */
  if (wc == (wchar_t)eolbyte)
    {
      if (!(syntax_bits & RE_DOT_NEWLINE))
        return 0;
      newline = 1;
    }
  else if (wc == (wchar_t)'\0')
    {
      if (syntax_bits & RE_DOT_NOT_NULL)
        return 0;
      newline = 1;
    }
  if (iswalnum(wc) || wc == L'_')
    letter = 1;
  if (!SUCCEEDS_IN_CONTEXT(pos.constraint, d->states[s].newline,
                           newline, d->states[s].letter, letter))
    return 0;

  /* Assign the current refering operator to work_mbc.  */
  work_mbc = &(d->mbcsets[(d->multibyte_prop[pos.index]) >> 2]);
  match = !work_mbc->invert;
  match_len = (mblen_buf[idx] == 0)? 1 : mblen_buf[idx];

  /* Match in range 0-255?  */
  if (wc < NOTCHAR && work_mbc->cset != -1
      && tstbit((unsigned char)wc, d->charclasses[work_mbc->cset]))
    goto charset_matched;

  /* match with a character class?  */
  for (i = 0; i<work_mbc->nch_classes; i++)
    {
      if (iswctype((wint_t)wc, work_mbc->ch_classes[i]))
        goto charset_matched;
    }

  strncpy(buffer, (char const *) buf_begin + idx, match_len);
  buffer[match_len] = '\0';

  /* match with an equivalent class?  */
  for (i = 0; i<work_mbc->nequivs; i++)
    {
      op_len = strlen(work_mbc->equivs[i]);
      strncpy(buffer, (char const *) buf_begin + idx, op_len);
      buffer[op_len] = '\0';
      if (strcoll(work_mbc->equivs[i], buffer) == 0)
        {
          match_len = op_len;
          goto charset_matched;
        }
    }

  /* match with a collating element?  */
  for (i = 0; i<work_mbc->ncoll_elems; i++)
    {
      op_len = strlen(work_mbc->coll_elems[i]);
      strncpy(buffer, (char const *) buf_begin + idx, op_len);
      buffer[op_len] = '\0';

      if (strcoll(work_mbc->coll_elems[i], buffer) == 0)
        {
          match_len = op_len;
          goto charset_matched;
        }
    }

  wcbuf[0] = wc;
  wcbuf[1] = wcbuf[3] = wcbuf[5] = '\0';

  /* match with a range?  */
  for (i = 0; i<work_mbc->nranges; i++)
    {
      wcbuf[2] = work_mbc->range_sts[i];
      wcbuf[4] = work_mbc->range_ends[i];

      if (wcscoll(wcbuf, wcbuf+2) >= 0 &&
          wcscoll(wcbuf+4, wcbuf) >= 0)
        goto charset_matched;
    }

  /* match with a character?  */
  for (i = 0; i<work_mbc->nchars; i++)
    {
      if (wc == work_mbc->chars[i])
        goto charset_matched;
    }

  match = !match;

 charset_matched:
  return match ? match_len : 0;
}

/* Check each of `d->states[s].mbps.elem' can match or not. Then return the
   array which corresponds to `d->states[s].mbps.elem' and each element of
   the array contains the amount of the bytes with which the element can
   match.
   `idx' is the index from the buf_begin, and it is the current position
   in the buffer.
   Caller MUST free the array which this function return.  */
static int*
check_matching_with_multibyte_ops (struct dfa *d, int s, int idx)
{
  int i;
  int* rarray;

  MALLOC(rarray, int, d->states[s].mbps.nelem);
  for (i = 0; i < d->states[s].mbps.nelem; ++i)
    {
      position pos = d->states[s].mbps.elems[i];
      switch(d->tokens[pos.index])
        {
        case ANYCHAR:
          rarray[i] = match_anychar(d, s, pos, idx);
          break;
        case MBCSET:
          rarray[i] = match_mb_charset(d, s, pos, idx);
          break;
        default:
          break; /* can not happen.  */
        }
    }
  return rarray;
}

/* Consume a single character and enumerate all of the positions which can
   be next position from the state `s'.
   `match_lens' is the input. It can be NULL, but it can also be the output
   of check_matching_with_multibyte_ops() for optimization.
   `mbclen' and `pps' are the output.  `mbclen' is the length of the
   character consumed, and `pps' is the set this function enumerate.  */
static status_transit_state
transit_state_consume_1char (struct dfa *d, int s, unsigned char const **pp,
                             int *match_lens, int *mbclen, position_set *pps)
{
  int i, j;
  int s1, s2;
  int* work_mbls;
  status_transit_state rs = TRANSIT_STATE_DONE;

  /* Calculate the length of the (single/multi byte) character
     to which p points.  */
  *mbclen = (mblen_buf[*pp - buf_begin] == 0)? 1
    : mblen_buf[*pp - buf_begin];

  /* Calculate the state which can be reached from the state `s' by
     consuming `*mbclen' single bytes from the buffer.  */
  s1 = s;
  for (i = 0; i < *mbclen; i++)
    {
      s2 = s1;
      rs = transit_state_singlebyte(d, s2, (*pp)++, &s1);
    }
  /* Copy the positions contained by `s1' to the set `pps'.  */
  copy(&(d->states[s1].elems), pps);

  /* Check (inputed)match_lens, and initialize if it is NULL.  */
  if (match_lens == NULL && d->states[s].mbps.nelem != 0)
    work_mbls = check_matching_with_multibyte_ops(d, s, *pp - buf_begin);
  else
    work_mbls = match_lens;

  /* Add all of the positions which can be reached from `s' by consuming
     a single character.  */
  for (i = 0; i < d->states[s].mbps.nelem ; i++)
   {
      if (work_mbls[i] == *mbclen)
        for (j = 0; j < d->follows[d->states[s].mbps.elems[i].index].nelem;
             j++)
          insert(d->follows[d->states[s].mbps.elems[i].index].elems[j],
                 pps);
    }

  if (match_lens == NULL && work_mbls != NULL)
    free(work_mbls);
  return rs;
}

/* Transit state from s, then return new state and update the pointer of the
   buffer.  This function is for some operator which can match with a multi-
   byte character or a collating element (which may be multi characters).  */
static int
transit_state (struct dfa *d, int s, unsigned char const **pp)
{
  int s1;
  int mbclen;		/* The length of current input multibyte character. */
  int maxlen = 0;
  int i, j;
  int *match_lens = NULL;
  int nelem = d->states[s].mbps.nelem; /* Just a alias.  */
  position_set follows;
  unsigned char const *p1 = *pp;
  wchar_t wc;

  if (nelem > 0)
    /* This state has (a) multibyte operator(s).
       We check whether each of them can match or not.  */
    {
      /* Note: caller must free the return value of this function.  */
      match_lens = check_matching_with_multibyte_ops(d, s, *pp - buf_begin);

      for (i = 0; i < nelem; i++)
        /* Search the operator which match the longest string,
           in this state.  */
        {
          if (match_lens[i] > maxlen)
            maxlen = match_lens[i];
        }
    }

  if (nelem == 0 || maxlen == 0)
    /* This state has no multibyte operator which can match.
       We need to check only one single byte character.  */
    {
      status_transit_state rs;
      rs = transit_state_singlebyte(d, s, *pp, &s1);

      /* We must update the pointer if state transition succeeded.  */
      if (rs == TRANSIT_STATE_DONE)
        ++*pp;

      free(match_lens);
      return s1;
    }

  /* This state has some operators which can match a multibyte character.  */
  follows.nelem = 0;
  MALLOC(follows.elems, position, d->nleaves);

  /* `maxlen' may be longer than the length of a character, because it may
     not be a character but a (multi character) collating element.
     We enumerate all of the positions which `s' can reach by consuming
     `maxlen' bytes.  */
  transit_state_consume_1char(d, s, pp, match_lens, &mbclen, &follows);

  wc = inputwcs[*pp - mbclen - buf_begin];
  s1 = state_index(d, &follows, wc == L'\n', iswalnum(wc));
  realloc_trans_if_necessary(d, s1);

  while (*pp - p1 < maxlen)
    {
      follows.nelem = 0;
      transit_state_consume_1char(d, s1, pp, NULL, &mbclen, &follows);

      for (i = 0; i < nelem ; i++)
        {
          if (match_lens[i] == *pp - p1)
            for (j = 0;
                 j < d->follows[d->states[s1].mbps.elems[i].index].nelem; j++)
              insert(d->follows[d->states[s1].mbps.elems[i].index].elems[j],
                     &follows);
        }

      wc = inputwcs[*pp - mbclen - buf_begin];
      s1 = state_index(d, &follows, wc == L'\n', iswalnum(wc));
      realloc_trans_if_necessary(d, s1);
    }
  free(match_lens);
  free(follows.elems);
  return s1;
}

#endif /* MBS_SUPPORT */

/* Initialize mblen_buf and inputwcs with data from the next line.  */

static void
prepare_wc_buf (const char *begin, const char *end)
{
  unsigned char eol = eolbyte;
  size_t remain_bytes, i;

  buf_begin = (unsigned char *) begin;

  remain_bytes = 0;
  for (i = 0; i < end - begin + 1; i++)
    {
      if (remain_bytes == 0)
        {
          remain_bytes
            = mbrtowc(inputwcs + i, begin + i, end - begin - i + 1, &mbs);
          if (remain_bytes < 1
              || remain_bytes == (size_t) -1
              || remain_bytes == (size_t) -2
              || (remain_bytes == 1 && inputwcs[i] == (wchar_t)begin[i]))
            {
              remain_bytes = 0;
              inputwcs[i] = (wchar_t)begin[i];
              mblen_buf[i] = 0;
              if (begin[i] == eol)
                break;
            }
          else
            {
              mblen_buf[i] = remain_bytes;
              remain_bytes--;
            }
        }
      else
        {
          mblen_buf[i] = remain_bytes;
          inputwcs[i] = 0;
          remain_bytes--;
        }
    }

  buf_end = (unsigned char *) (begin + i);
  mblen_buf[i] = 0;
  inputwcs[i] = 0; /* sentinel */
}

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
char *
dfaexec (struct dfa *d, char const *begin, char *end,
         int newline, int *count, int *backref)
{
  int s, s1, tmp;	/* Current state. */
  unsigned char const *p; /* Current input character. */
  int **trans, *t;	/* Copy of d->trans so it can be optimized
                                   into a register. */
  unsigned char eol = eolbyte;	/* Likewise for eolbyte.  */
  unsigned char saved_end;
  static int sbit[NOTCHAR];	/* Table for anding with d->success. */
  static int sbit_init;

  if (! sbit_init)
    {
      unsigned int i;

      sbit_init = 1;
      for (i = 0; i < NOTCHAR; ++i)
        sbit[i] = (IS_WORD_CONSTITUENT(i)) ? 2 : 1;
      sbit[eol] = 4;
    }

  if (! d->tralloc)
    build_state_zero(d);

  s = s1 = 0;
  p = (unsigned char const *) begin;
  trans = d->trans;
  saved_end = *(unsigned char *) end;
  *end = eol;

#if MBS_SUPPORT
  if (d->mb_cur_max > 1)
    {
      MALLOC(mblen_buf, unsigned char, end - begin + 2);
      MALLOC(inputwcs, wchar_t, end - begin + 2);
      memset(&mbs, 0, sizeof(mbstate_t));
      prepare_wc_buf ((const char *) p, end);
    }
#endif /* MBS_SUPPORT */

  for (;;)
    {
#if MBS_SUPPORT
      if (d->mb_cur_max > 1)
        while ((t = trans[s]))
          {
            if (p > buf_end)
              break;
            s1 = s;
            SKIP_REMAINS_MB_IF_INITIAL_STATE(s, p);

            if (d->states[s].mbps.nelem == 0)
              {
                s = t[*p++];
                continue;
              }

            /* Falling back to the glibc matcher in this case gives
               better performance (up to 25% better on [a-z], for
               example) and enables support for collating symbols and
               equivalence classes.  */
            if (backref)
              {
                *backref = 1;
                free(mblen_buf);
                free(inputwcs);
                *end = saved_end;
                return (char *) p;
              }

            /* Can match with a multibyte character (and multi character
               collating element).  Transition table might be updated.  */
            s = transit_state(d, s, &p);
            trans = d->trans;
          }
      else
#endif /* MBS_SUPPORT */
      while ((t = trans[s]) != 0) { /* hand-optimized loop */
        s1 = t[*p++];
        if ((t = trans[s1]) == 0) {
          tmp = s ; s = s1 ; s1 = tmp ; /* swap */
          break;
        }
        s = t[*p++];
      }

      if (s >= 0 && (char *) p <= end && d->fails[s])
        {
          if (d->success[s] & sbit[*p])
            {
              if (backref)
                *backref = (d->states[s].backref != 0);
#if MBS_SUPPORT
              if (d->mb_cur_max > 1)
                {
                  free(mblen_buf);
                  free(inputwcs);
                }
#endif /* MBS_SUPPORT */
              *end = saved_end;
              return (char *) p;
            }

          s1 = s;
#if MBS_SUPPORT
          if (d->mb_cur_max > 1)
            {
              /* Can match with a multibyte character (and multicharacter
                 collating element).  Transition table might be updated.  */
              s = transit_state(d, s, &p);
              trans = d->trans;
            }
          else
#endif /* MBS_SUPPORT */
          s = d->fails[s][*p++];
          continue;
        }

      /* If the previous character was a newline, count it. */
      if ((char *) p <= end && p[-1] == eol)
        {
          if (count)
            ++*count;

#if MBS_SUPPORT
          if (d->mb_cur_max > 1)
            prepare_wc_buf ((const char *) p, end);
#endif
        }

      /* Check if we've run off the end of the buffer. */
      if ((char *) p > end)
        {
#if MBS_SUPPORT
          if (d->mb_cur_max > 1)
            {
              free(mblen_buf);
              free(inputwcs);
            }
#endif /* MBS_SUPPORT */
          *end = saved_end;
          return NULL;
        }

      if (s >= 0)
        {
          build_state(s, d);
          trans = d->trans;
          continue;
        }

      if (p[-1] == eol && newline)
        {
          s = d->newlines[s1];
          continue;
        }

      s = 0;
    }
}

#if MBS_SUPPORT
static void
free_mbdata (struct dfa *d)
{
  unsigned int i;

  free(d->multibyte_prop);
  d->multibyte_prop = NULL;

  for (i = 0; i < d->nmbcsets; ++i)
    {
      unsigned int j;
      struct mb_char_classes *p = &(d->mbcsets[i]);
      free(p->chars);
      free(p->ch_classes);
      free(p->range_sts);
      free(p->range_ends);

      for (j = 0; j < p->nequivs; ++j)
        free(p->equivs[j]);
      free(p->equivs);

      for (j = 0; j < p->ncoll_elems; ++j)
        free(p->coll_elems[j]);
      free(p->coll_elems);
    }

  free(d->mbcsets);
  d->mbcsets = NULL;
  d->nmbcsets = 0;
}
#endif

/* Initialize the components of a dfa that the other routines don't
   initialize for themselves. */
void
dfainit (struct dfa *d)
{
  memset (d, 0, sizeof *d);

  d->calloc = 1;
  MALLOC(d->charclasses, charclass, d->calloc);

  d->talloc = 1;
  MALLOC(d->tokens, token, d->talloc);

#if MBS_SUPPORT
  d->mb_cur_max = MB_CUR_MAX;
  if (d->mb_cur_max > 1)
    {
      d->nmultibyte_prop = 1;
      MALLOC(d->multibyte_prop, int, d->nmultibyte_prop);
      d->mbcsets_alloc = 1;
      MALLOC(d->mbcsets, struct mb_char_classes, d->mbcsets_alloc);
    }
#endif
}

#if MBS_SUPPORT
static void
dfaoptimize (struct dfa *d)
{
  unsigned int i;
  if (!using_utf8())
    return;

  for (i = 0; i < d->tindex; ++i)
    {
      switch(d->tokens[i])
        {
        case ANYCHAR:
          /* Lowered.  */
          abort ();
        case MBCSET:
          /* Requires multi-byte algorithm.  */
          return;
        default:
          break;
        }
    }

  free_mbdata (d);
  d->mb_cur_max = 1;
}
#endif

/* Parse and analyze a single string of the given length. */
void
dfacomp (char const *s, size_t len, struct dfa *d, int searchflag)
{
  dfainit(d);
  dfaparse(s, len, d);
  dfamust(d);
#if MBS_SUPPORT
  dfaoptimize(d);
#endif
  dfaanalyze(d, searchflag);
}

/* Free the storage held by the components of a dfa. */
void
dfafree (struct dfa *d)
{
  int i;
  struct dfamust *dm, *ndm;

  free(d->charclasses);
  free(d->tokens);

#if MBS_SUPPORT
  if (d->mb_cur_max > 1)
    free_mbdata(d);
#endif /* MBS_SUPPORT */

  for (i = 0; i < d->sindex; ++i) {
    free(d->states[i].elems.elems);
#if MBS_SUPPORT
    free(d->states[i].mbps.elems);
#endif /* MBS_SUPPORT */
  }
  free(d->states);
  for (i = 0; i < d->tindex; ++i)
    free(d->follows[i].elems);
  free(d->follows);
  for (i = 0; i < d->tralloc; ++i)
    {
      free(d->trans[i]);
      free(d->fails[i]);
    }
  free(d->realtrans);
  free(d->fails);
  free(d->newlines);
  free(d->success);
  for (dm = d->musts; dm; dm = ndm)
    {
      ndm = dm->next;
      free(dm->must);
      free(dm);
    }
}

/* Having found the postfix representation of the regular expression,
   try to find a long sequence of characters that must appear in any line
   containing the r.e.
   Finding a "longest" sequence is beyond the scope here;
   we take an easy way out and hope for the best.
   (Take "(ab|a)b"--please.)

   We do a bottom-up calculation of sequences of characters that must appear
   in matches of r.e.'s represented by trees rooted at the nodes of the postfix
   representation:
        sequences that must appear at the left of the match ("left")
        sequences that must appear at the right of the match ("right")
        lists of sequences that must appear somewhere in the match ("in")
        sequences that must constitute the match ("is")

   When we get to the root of the tree, we use one of the longest of its
   calculated "in" sequences as our answer.  The sequence we find is returned in
   d->must (where "d" is the single argument passed to "dfamust");
   the length of the sequence is returned in d->mustn.

   The sequences calculated for the various types of node (in pseudo ANSI c)
   are shown below.  "p" is the operand of unary operators (and the left-hand
   operand of binary operators); "q" is the right-hand operand of binary
   operators.

   "ZERO" means "a zero-length sequence" below.

        Type	left		right		is		in
        ----	----		-----		--		--
        char c	# c		# c		# c		# c

        ANYCHAR	ZERO		ZERO		ZERO		ZERO

        MBCSET	ZERO		ZERO		ZERO		ZERO

        CSET	ZERO		ZERO		ZERO		ZERO

        STAR	ZERO		ZERO		ZERO		ZERO

        QMARK	ZERO		ZERO		ZERO		ZERO

        PLUS	p->left		p->right	ZERO		p->in

        CAT	(p->is==ZERO)?	(q->is==ZERO)?	(p->is!=ZERO &&	p->in plus
                p->left :	q->right :	q->is!=ZERO) ?	q->in plus
                p->is##q->left	p->right##q->is	p->is##q->is :	p->right##q->left
                                                ZERO

        OR	longest common	longest common	(do p->is and	substrings common to
                leading		trailing	q->is have same	p->in and q->in
                (sub)sequence	(sub)sequence	length and
                of p->left	of p->right	content) ?
                and q->left	and q->right	p->is : NULL

   If there's anything else we recognize in the tree, all four sequences get set
   to zero-length sequences.  If there's something we don't recognize in the tree,
   we just return a zero-length sequence.

   Break ties in favor of infrequent letters (choosing 'zzz' in preference to
   'aaa')?

   And. . .is it here or someplace that we might ponder "optimizations" such as
        egrep 'psi|epsilon'	->	egrep 'psi'
        egrep 'pepsi|epsilon'	->	egrep 'epsi'
                                        (Yes, we now find "epsi" as a "string
                                        that must occur", but we might also
                                        simplify the *entire* r.e. being sought)
        grep '[c]'		->	grep 'c'
        grep '(ab|a)b'		->	grep 'ab'
        grep 'ab*'		->	grep 'a'
        grep 'a*b'		->	grep 'b'

   There are several issues:

   Is optimization easy (enough)?

   Does optimization actually accomplish anything,
   or is the automaton you get from "psi|epsilon" (for example)
   the same as the one you get from "psi" (for example)?

   Are optimizable r.e.'s likely to be used in real-life situations
   (something like 'ab*' is probably unlikely; something like is
   'psi|epsilon' is likelier)? */

static char *
icatalloc (char const *old, char const *new)
{
  char *result;
  size_t oldsize, newsize;

  newsize = (new == NULL) ? 0 : strlen(new);
  if (old == NULL)
    oldsize = 0;
  else if (newsize == 0)
    return (char *) old;
  else	oldsize = strlen(old);
  if (old == NULL)
    result = malloc(newsize + 1);
  else
    result = realloc((void *) old, oldsize + newsize + 1);
  if (result != NULL && new != NULL)
    (void) strcpy(result + oldsize, new);
  return result;
}

static char *
icpyalloc (char const *string)
{
  return icatalloc (NULL, string);
}

static char *
istrstr (char const *lookin, char const *lookfor)
{
  char const *cp;
  size_t len;

  len = strlen(lookfor);
  for (cp = lookin; *cp != '\0'; ++cp)
    if (strncmp(cp, lookfor, len) == 0)
      return (char *) cp;
  return NULL;
}

static void
freelist (char **cpp)
{
  int i;

  if (cpp == NULL)
    return;
  for (i = 0; cpp[i] != NULL; ++i)
    {
      free(cpp[i]);
      cpp[i] = NULL;
    }
}

static char **
enlist (char **cpp, char *new, size_t len)
{
  int i, j;

  if (cpp == NULL)
    return NULL;
  if ((new = icpyalloc(new)) == NULL)
    {
      freelist(cpp);
      return NULL;
    }
  new[len] = '\0';
  /* Is there already something in the list that's new (or longer)? */
  for (i = 0; cpp[i] != NULL; ++i)
    if (istrstr(cpp[i], new) != NULL)
      {
        free(new);
        return cpp;
      }
  /* Eliminate any obsoleted strings. */
  j = 0;
  while (cpp[j] != NULL)
    if (istrstr(new, cpp[j]) == NULL)
      ++j;
    else
      {
        free(cpp[j]);
        if (--i == j)
          break;
        cpp[j] = cpp[i];
        cpp[i] = NULL;
      }
  /* Add the new string. */
  cpp = realloc((char *) cpp, (i + 2) * sizeof *cpp);
  if (cpp == NULL)
    return NULL;
  cpp[i] = new;
  cpp[i + 1] = NULL;
  return cpp;
}

/* Given pointers to two strings, return a pointer to an allocated
   list of their distinct common substrings. Return NULL if something
   seems wild. */
static char **
comsubs (char *left, char const *right)
{
  char **cpp;
  char *lcp;
  char *rcp;
  size_t i, len;

  if (left == NULL || right == NULL)
    return NULL;
  cpp = malloc(sizeof *cpp);
  if (cpp == NULL)
    return NULL;
  cpp[0] = NULL;
  for (lcp = left; *lcp != '\0'; ++lcp)
    {
      len = 0;
      rcp = strchr (right, *lcp);
      while (rcp != NULL)
        {
          for (i = 1; lcp[i] != '\0' && lcp[i] == rcp[i]; ++i)
            continue;
          if (i > len)
            len = i;
          rcp = strchr (rcp + 1, *lcp);
        }
      if (len == 0)
        continue;
      if ((cpp = enlist(cpp, lcp, len)) == NULL)
        break;
    }
  return cpp;
}

static char **
addlists (char **old, char **new)
{
  int i;

  if (old == NULL || new == NULL)
    return NULL;
  for (i = 0; new[i] != NULL; ++i)
    {
      old = enlist(old, new[i], strlen(new[i]));
      if (old == NULL)
        break;
    }
  return old;
}

/* Given two lists of substrings, return a new list giving substrings
   common to both. */
static char **
inboth (char **left, char **right)
{
  char **both;
  char **temp;
  int lnum, rnum;

  if (left == NULL || right == NULL)
    return NULL;
  both = malloc(sizeof *both);
  if (both == NULL)
    return NULL;
  both[0] = NULL;
  for (lnum = 0; left[lnum] != NULL; ++lnum)
    {
      for (rnum = 0; right[rnum] != NULL; ++rnum)
        {
          temp = comsubs(left[lnum], right[rnum]);
          if (temp == NULL)
            {
              freelist(both);
              return NULL;
            }
          both = addlists(both, temp);
          freelist(temp);
          free(temp);
          if (both == NULL)
            return NULL;
        }
    }
  return both;
}

typedef struct
{
  char **in;
  char *left;
  char *right;
  char *is;
} must;

static void
resetmust (must *mp)
{
  mp->left[0] = mp->right[0] = mp->is[0] = '\0';
  freelist(mp->in);
}

static void
dfamust (struct dfa *d)
{
  must *musts;
  must *mp;
  char *result;
  int ri;
  int i;
  int exact;
  token t;
  static must must0;
  struct dfamust *dm;
  static char empty_string[] = "";

  result = empty_string;
  exact = 0;
  musts = malloc((d->tindex + 1) * sizeof *musts);
  if (musts == NULL)
    return;
  mp = musts;
  for (i = 0; i <= d->tindex; ++i)
    mp[i] = must0;
  for (i = 0; i <= d->tindex; ++i)
    {
      mp[i].in = malloc(sizeof *mp[i].in);
      mp[i].left = malloc(2);
      mp[i].right = malloc(2);
      mp[i].is = malloc(2);
      if (mp[i].in == NULL || mp[i].left == NULL ||
          mp[i].right == NULL || mp[i].is == NULL)
        goto done;
      mp[i].left[0] = mp[i].right[0] = mp[i].is[0] = '\0';
      mp[i].in[0] = NULL;
    }
#ifdef DEBUG
  fprintf(stderr, "dfamust:\n");
  for (i = 0; i < d->tindex; ++i)
    {
      fprintf(stderr, " %d:", i);
      prtok(d->tokens[i]);
    }
  putc('\n', stderr);
#endif
  for (ri = 0; ri < d->tindex; ++ri)
    {
      switch (t = d->tokens[ri])
        {
        case LPAREN:
        case RPAREN:
          assert (!"neither LPAREN nor RPAREN may appear here");
        case EMPTY:
        case BEGLINE:
        case ENDLINE:
        case BEGWORD:
        case ENDWORD:
        case LIMWORD:
        case NOTLIMWORD:
        case BACKREF:
          resetmust(mp);
          break;
        case STAR:
        case QMARK:
          assert (musts < mp);
          --mp;
          resetmust(mp);
          break;
        case OR:
          assert (&musts[2] <= mp);
          {
            char **new;
            must *lmp;
            must *rmp;
            int j, ln, rn, n;

            rmp = --mp;
            lmp = --mp;
            /* Guaranteed to be.  Unlikely, but. . . */
            if (!STREQ (lmp->is, rmp->is))
              lmp->is[0] = '\0';
            /* Left side--easy */
            i = 0;
            while (lmp->left[i] != '\0' && lmp->left[i] == rmp->left[i])
              ++i;
            lmp->left[i] = '\0';
            /* Right side */
            ln = strlen(lmp->right);
            rn = strlen(rmp->right);
            n = ln;
            if (n > rn)
              n = rn;
            for (i = 0; i < n; ++i)
              if (lmp->right[ln - i - 1] != rmp->right[rn - i - 1])
                break;
            for (j = 0; j < i; ++j)
              lmp->right[j] = lmp->right[(ln - i) + j];
            lmp->right[j] = '\0';
            new = inboth(lmp->in, rmp->in);
            if (new == NULL)
              goto done;
            freelist(lmp->in);
            free(lmp->in);
            lmp->in = new;
          }
          break;
        case PLUS:
          assert (musts < mp);
          --mp;
          mp->is[0] = '\0';
          break;
        case END:
          assert (mp == &musts[1]);
          for (i = 0; musts[0].in[i] != NULL; ++i)
            if (strlen(musts[0].in[i]) > strlen(result))
              result = musts[0].in[i];
          if (STREQ (result, musts[0].is))
            exact = 1;
          goto done;
        case CAT:
          assert (&musts[2] <= mp);
          {
            must *lmp;
            must *rmp;

            rmp = --mp;
            lmp = --mp;
            /* In.  Everything in left, plus everything in
               right, plus catenation of
               left's right and right's left. */
            lmp->in = addlists(lmp->in, rmp->in);
            if (lmp->in == NULL)
              goto done;
            if (lmp->right[0] != '\0' &&
                rmp->left[0] != '\0')
              {
                char *tp;

                tp = icpyalloc(lmp->right);
                if (tp == NULL)
                  goto done;
                tp = icatalloc(tp, rmp->left);
                if (tp == NULL)
                  goto done;
                lmp->in = enlist(lmp->in, tp,
                                 strlen(tp));
                free(tp);
                if (lmp->in == NULL)
                  goto done;
              }
            /* Left-hand */
            if (lmp->is[0] != '\0')
              {
                lmp->left = icatalloc(lmp->left,
                                      rmp->left);
                if (lmp->left == NULL)
                  goto done;
              }
            /* Right-hand */
            if (rmp->is[0] == '\0')
              lmp->right[0] = '\0';
            lmp->right = icatalloc(lmp->right, rmp->right);
            if (lmp->right == NULL)
              goto done;
            /* Guaranteed to be */
            if (lmp->is[0] != '\0' && rmp->is[0] != '\0')
              {
                lmp->is = icatalloc(lmp->is, rmp->is);
                if (lmp->is == NULL)
                  goto done;
              }
            else
              lmp->is[0] = '\0';
          }
          break;
        default:
          if (t < END)
            {
              assert (!"oops! t >= END");
            }
          else if (t == '\0')
            {
              /* not on *my* shift */
              goto done;
            }
          else if (t >= CSET
#if MBS_SUPPORT
                   || t == ANYCHAR
                   || t == MBCSET
#endif /* MBS_SUPPORT */
                   )
            {
              /* easy enough */
              resetmust(mp);
            }
          else
            {
              /* plain character */
              resetmust(mp);
              mp->is[0] = mp->left[0] = mp->right[0] = t;
              mp->is[1] = mp->left[1] = mp->right[1] = '\0';
              mp->in = enlist(mp->in, mp->is, (size_t)1);
              if (mp->in == NULL)
                goto done;
            }
          break;
        }
#ifdef DEBUG
      fprintf(stderr, " node: %d:", ri);
      prtok(d->tokens[ri]);
      fprintf(stderr, "\n  in:");
      for (i = 0; mp->in[i]; ++i)
        fprintf(stderr, " \"%s\"", mp->in[i]);
      fprintf(stderr, "\n  is: \"%s\"\n", mp->is);
      fprintf(stderr, "  left: \"%s\"\n", mp->left);
      fprintf(stderr, "  right: \"%s\"\n", mp->right);
#endif
      ++mp;
    }
 done:
  if (strlen(result))
    {
      MALLOC(dm, struct dfamust, 1);
      dm->exact = exact;
      MALLOC(dm->must, char, strlen(result) + 1);
      strcpy(dm->must, result);
      dm->next = d->musts;
      d->musts = dm;
    }
  mp = musts;
  for (i = 0; i <= d->tindex; ++i)
    {
      freelist(mp[i].in);
      free(mp[i].in);
      free(mp[i].left);
      free(mp[i].right);
      free(mp[i].is);
    }
  free(mp);
}

struct dfa *
dfaalloc (void)
{
  return xmalloc (sizeof (struct dfa));
}

struct dfamust *
dfamusts (struct dfa const *d)
{
  return d->musts;
}

/* vim:set shiftwidth=2: */
