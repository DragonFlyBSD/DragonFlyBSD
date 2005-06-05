/* A Bison parser, made from /scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y
   by GNU bison 1.35.  */

#define YYBISON 1  /* Identify Bison output.  */

# define	ENT_TYPEDEF_STRUCT	257
# define	ENT_STRUCT	258
# define	ENT_EXTERNSTATIC	259
# define	ENT_YACCUNION	260
# define	GTY_TOKEN	261
# define	UNION	262
# define	STRUCT	263
# define	ENUM	264
# define	ALIAS	265
# define	NESTED_PTR	266
# define	PARAM_IS	267
# define	NUM	268
# define	PERCENTPERCENT	269
# define	SCALAR	270
# define	ID	271
# define	STRING	272
# define	ARRAY	273
# define	PERCENT_ID	274
# define	CHAR	275

#line 22 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "gengtype.h"
#define YYERROR_VERBOSE

#line 31 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
#ifndef YYSTYPE
typedef union {
  type_p t;
  pair_p p;
  options_p o;
  const char *s;
} yystype;
# define YYSTYPE yystype
# define YYSTYPE_IS_TRIVIAL 1
#endif
#ifndef YYDEBUG
# define YYDEBUG 0
#endif



#define	YYFINAL		127
#define	YYFLAG		-32768
#define	YYNTBASE	33

/* YYTRANSLATE(YYLEX) -- Bison token number corresponding to YYLEX. */
#define YYTRANSLATE(x) ((unsigned)(x) <= 275 ? yytranslate[x] : 55)

/* YYTRANSLATE[YYLEX] -- Bison token number corresponding to YYLEX. */
static const char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      31,    32,    29,     2,    30,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    28,    24,
      26,    25,    27,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    22,     2,    23,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,    21
};

#if YYDEBUG
static const short yyprhs[] =
{
       0,     0,     1,     4,     7,    10,    11,    20,    21,    29,
      35,    42,    50,    52,    54,    56,    63,    64,    68,    75,
      76,    79,    82,    83,    90,    97,   105,   111,   112,   115,
     117,   119,   121,   123,   126,   132,   135,   141,   144,   147,
     153,   154,   160,   164,   167,   168,   170,   177,   179,   181,
     183,   188,   193,   202,   204,   208,   209,   211,   213
};
static const short yyrhs[] =
{
      -1,    34,    33,     0,    37,    33,     0,    40,    33,     0,
       0,     3,    49,    22,    43,    23,    17,    35,    24,     0,
       0,     4,    49,    22,    43,    23,    36,    24,     0,     5,
      49,    38,    17,    39,     0,     5,    49,    38,    17,    19,
      39,     0,     5,    49,    38,    17,    19,    19,    39,     0,
      46,     0,    24,     0,    25,     0,     6,    49,    43,    23,
      41,    15,     0,     0,    41,    20,    42,     0,    41,    20,
      26,    17,    27,    42,     0,     0,    42,    17,     0,    42,
      21,     0,     0,    46,    48,    17,    44,    24,    43,     0,
      46,    48,    17,    19,    24,    43,     0,    46,    48,    17,
      19,    19,    24,    43,     0,    46,    28,    45,    24,    43,
       0,     0,    28,    45,     0,    14,     0,    17,     0,    16,
       0,    17,     0,    46,    29,     0,     9,    17,    22,    43,
      23,     0,     9,    17,     0,     8,    17,    22,    43,    23,
       0,     8,    17,     0,    10,    17,     0,    10,    17,    22,
      47,    23,     0,     0,    17,    25,    14,    30,    47,     0,
      17,    30,    47,     0,    17,    47,     0,     0,    49,     0,
       7,    31,    31,    53,    32,    32,     0,    11,     0,    13,
       0,    17,     0,    17,    31,    54,    32,     0,    50,    31,
      46,    32,     0,    12,    31,    46,    30,    54,    30,    54,
      32,     0,    51,     0,    52,    30,    51,     0,     0,    52,
       0,    18,     0,    54,    18,     0
};

#endif

#if YYDEBUG
/* YYRLINE[YYN] -- source line where rule number YYN was defined. */
static const short yyrline[] =
{
       0,    65,    66,    67,    68,    71,    71,    80,    80,    90,
      95,   100,   108,   115,   116,   119,   126,   128,   141,   159,
     161,   172,   185,   186,   196,   206,   216,   220,   221,   224,
     224,   228,   230,   232,   234,   239,   241,   246,   248,   250,
     254,   255,   257,   259,   263,   264,   267,   271,   273,   277,
     279,   281,   283,   295,   300,   307,   308,   311,   313
};
#endif


#if (YYDEBUG) || defined YYERROR_VERBOSE

/* YYTNAME[TOKEN_NUM] -- String name of the token TOKEN_NUM. */
static const char *const yytname[] =
{
  "$", "error", "$undefined.", "ENT_TYPEDEF_STRUCT", "ENT_STRUCT", 
  "ENT_EXTERNSTATIC", "ENT_YACCUNION", "GTY_TOKEN", "UNION", "STRUCT", 
  "ENUM", "ALIAS", "NESTED_PTR", "PARAM_IS", "NUM", "\"%%\"", "SCALAR", 
  "ID", "STRING", "ARRAY", "PERCENT_ID", "CHAR", "'{'", "'}'", "';'", 
  "'='", "'<'", "'>'", "':'", "'*'", "','", "'('", "')'", "start", 
  "typedef_struct", "@1", "@2", "externstatic", "lasttype", "semiequal", 
  "yacc_union", "yacc_typematch", "yacc_ids", "struct_fields", 
  "bitfieldopt", "bitfieldlen", "type", "enum_items", "optionsopt", 
  "options", "type_option", "option", "optionseq", "optionseqopt", 
  "stringseq", 0
};
#endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives. */
static const short yyr1[] =
{
       0,    33,    33,    33,    33,    35,    34,    36,    34,    37,
      37,    37,    38,    39,    39,    40,    41,    41,    41,    42,
      42,    42,    43,    43,    43,    43,    43,    44,    44,    45,
      45,    46,    46,    46,    46,    46,    46,    46,    46,    46,
      47,    47,    47,    47,    48,    48,    49,    50,    50,    51,
      51,    51,    51,    52,    52,    53,    53,    54,    54
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN. */
static const short yyr2[] =
{
       0,     0,     2,     2,     2,     0,     8,     0,     7,     5,
       6,     7,     1,     1,     1,     6,     0,     3,     6,     0,
       2,     2,     0,     6,     6,     7,     5,     0,     2,     1,
       1,     1,     1,     2,     5,     2,     5,     2,     2,     5,
       0,     5,     3,     2,     0,     1,     6,     1,     1,     1,
       4,     4,     8,     1,     3,     0,     1,     1,     2
};

/* YYDEFACT[S] -- default rule to reduce with in state S when YYTABLE
   doesn't specify something else to do.  Zero means the default is an
   error. */
static const short yydefact[] =
{
       1,     0,     0,     0,     0,     1,     1,     1,     0,     0,
       0,     0,    22,     2,     3,     4,     0,    22,    22,     0,
       0,     0,    31,    32,     0,    12,     0,    44,    55,     0,
       0,    37,    35,    38,     0,    33,    16,     0,     0,    45,
      47,     0,    48,    49,     0,    53,    56,     0,     0,     7,
      22,    22,    40,     0,    13,    14,     9,     0,    29,    30,
       0,    27,     0,     0,     0,     0,     0,     5,     0,     0,
       0,    40,     0,     0,    10,    15,    19,    22,     0,     0,
       0,     0,    57,     0,     0,    54,    46,     0,     8,    36,
      34,     0,    40,    43,    39,    11,     0,    17,    26,     0,
      22,    28,    22,     0,    58,    50,    51,     6,     0,    42,
       0,    20,    21,    22,    24,    23,     0,    40,    19,    25,
       0,    41,    18,     0,    52,     0,     0,     0
};

static const short yydefgoto[] =
{
      13,     5,    87,    68,     6,    24,    56,     7,    57,    97,
      26,    80,    60,    27,    72,    38,     9,    44,    45,    46,
      47,    83
};

static const short yypact[] =
{
      63,    31,    31,    31,    31,    63,    63,    63,    25,    40,
      49,    27,    27,-32768,-32768,-32768,    41,    27,    27,    57,
      58,    59,-32768,-32768,    60,    50,    55,     3,    29,    61,
      64,    66,    67,    68,    26,-32768,-32768,    56,    65,-32768,
  -32768,    62,-32768,    69,    70,-32768,    51,    48,    74,-32768,
      27,    27,    75,    33,-32768,-32768,-32768,     1,-32768,-32768,
      71,    11,    27,    76,    27,    29,    54,-32768,    73,    79,
      80,    -8,    81,    23,-32768,-32768,    72,    27,    35,    56,
      82,    34,-32768,    -6,   -14,-32768,-32768,    83,-32768,-32768,
  -32768,    85,    75,-32768,-32768,-32768,    88,    44,-32768,    84,
      27,-32768,    27,    76,-32768,-32768,-32768,-32768,    86,-32768,
      87,-32768,-32768,    27,-32768,-32768,    -7,    75,-32768,-32768,
      76,-32768,    44,    -4,-32768,   109,   110,-32768
};

static const short yypgoto[] =
{
      13,-32768,-32768,-32768,-32768,-32768,   -46,-32768,-32768,    -5,
     -17,-32768,    32,    -9,   -68,-32768,     2,-32768,    47,-32768,
  -32768,   -95
};


#define	YYLAST		116


static const short yytable[] =
{
      29,    30,    25,    93,    10,    11,    12,    74,   116,    71,
       8,   104,   104,   125,   104,    35,    75,    91,   106,    14,
      15,    76,    92,   120,   109,   123,   105,    95,   124,    39,
      78,    37,    35,    69,    70,    19,    20,    21,     8,    79,
      40,    41,    42,    22,    23,    53,    43,    54,    55,   121,
      54,    55,    73,    81,    99,    84,    16,    54,    55,   100,
      98,   111,    17,    35,   103,   112,     1,     2,     3,     4,
      58,    18,    28,    59,    31,    32,    33,    34,    36,    35,
      66,    65,    61,   114,    48,   115,    86,    49,    50,    51,
      52,    67,    71,    62,    82,    77,   119,    88,    96,   108,
      63,    64,    89,    90,    94,   110,   102,   107,   113,   126,
     127,   101,    85,   122,   118,     0,   117
};

static const short yycheck[] =
{
      17,    18,    11,    71,     2,     3,     4,    53,   103,    17,
       7,    18,    18,     0,    18,    29,    15,    25,    32,     6,
       7,    20,    30,    30,    92,   120,    32,    73,    32,    27,
      19,    28,    29,    50,    51,     8,     9,    10,     7,    28,
      11,    12,    13,    16,    17,    19,    17,    24,    25,   117,
      24,    25,    19,    62,    19,    64,    31,    24,    25,    24,
      77,    17,    22,    29,    30,    21,     3,     4,     5,     6,
      14,    22,    31,    17,    17,    17,    17,    17,    23,    29,
      32,    30,    17,   100,    23,   102,    32,    23,    22,    22,
      22,    17,    17,    31,    18,    24,   113,    24,    26,    14,
      31,    31,    23,    23,    23,    17,    24,    24,    24,     0,
       0,    79,    65,   118,    27,    -1,    30
};
/* -*-C-*-  Note some compilers choke on comments on `#line' lines.  */
#line 3 "/usr/share/bison/bison.simple"

/* Skeleton output parser for bison,

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002 Free Software
   Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* This is the parser code that is written into each bison parser when
   the %semantic_parser declaration is not specified in the grammar.
   It was written by Richard Stallman by simplifying the hairy parser
   used when %semantic_parser is specified.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

#if ! defined (yyoverflow) || defined (YYERROR_VERBOSE)

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# if YYSTACK_USE_ALLOCA
#  define YYSTACK_ALLOC alloca
# else
#  ifndef YYSTACK_USE_ALLOCA
#   if defined (alloca) || defined (_ALLOCA_H)
#    define YYSTACK_ALLOC alloca
#   else
#    ifdef __GNUC__
#     define YYSTACK_ALLOC __builtin_alloca
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning. */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
# else
#  if defined (__STDC__) || defined (__cplusplus)
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   define YYSIZE_T size_t
#  endif
#  define YYSTACK_ALLOC malloc
#  define YYSTACK_FREE free
# endif
#endif /* ! defined (yyoverflow) || defined (YYERROR_VERBOSE) */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (YYLTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short yyss;
  YYSTYPE yyvs;
# if YYLSP_NEEDED
  YYLTYPE yyls;
# endif
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAX (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# if YYLSP_NEEDED
#  define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE) + sizeof (YYLTYPE))	\
      + 2 * YYSTACK_GAP_MAX)
# else
#  define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE))				\
      + YYSTACK_GAP_MAX)
# endif

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  register YYSIZE_T yyi;		\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (0)
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAX;	\
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (0)

#endif


#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# if defined (__STDC__) || defined (__cplusplus)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# endif
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		-2
#define YYEOF		0
#define YYACCEPT	goto yyacceptlab
#define YYABORT 	goto yyabortlab
#define YYERROR		goto yyerrlab1
/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */
#define YYFAIL		goto yyerrlab
#define YYRECOVERING()  (!!yyerrstatus)
#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yychar1 = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { 								\
      yyerror ("syntax error: cannot back up");			\
      YYERROR;							\
    }								\
while (0)

#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Compute the default location (before the actions
   are run).

   When YYLLOC_DEFAULT is run, CURRENT is set the location of the
   first token.  By default, to implement support for ranges, extend
   its range to the last symbol.  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)       	\
   Current.last_line   = Rhs[N].last_line;	\
   Current.last_column = Rhs[N].last_column;
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#if YYPURE
# if YYLSP_NEEDED
#  ifdef YYLEX_PARAM
#   define YYLEX		yylex (&yylval, &yylloc, YYLEX_PARAM)
#  else
#   define YYLEX		yylex (&yylval, &yylloc)
#  endif
# else /* !YYLSP_NEEDED */
#  ifdef YYLEX_PARAM
#   define YYLEX		yylex (&yylval, YYLEX_PARAM)
#  else
#   define YYLEX		yylex (&yylval)
#  endif
# endif /* !YYLSP_NEEDED */
#else /* !YYPURE */
# define YYLEX			yylex ()
#endif /* !YYPURE */


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (0)
/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
#endif /* !YYDEBUG */

/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   SIZE_MAX < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#if YYMAXDEPTH == 0
# undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif

#ifdef YYERROR_VERBOSE

# ifndef yystrlen
#  if defined (__GLIBC__) && defined (_STRING_H)
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
#   if defined (__STDC__) || defined (__cplusplus)
yystrlen (const char *yystr)
#   else
yystrlen (yystr)
     const char *yystr;
#   endif
{
  register const char *yys = yystr;

  while (*yys++ != '\0')
    continue;

  return yys - yystr - 1;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined (__GLIBC__) && defined (_STRING_H) && defined (_GNU_SOURCE)
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
#   if defined (__STDC__) || defined (__cplusplus)
yystpcpy (char *yydest, const char *yysrc)
#   else
yystpcpy (yydest, yysrc)
     char *yydest;
     const char *yysrc;
#   endif
{
  register char *yyd = yydest;
  register const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif
#endif

#line 315 "/usr/share/bison/bison.simple"


/* The user can define YYPARSE_PARAM as the name of an argument to be passed
   into yyparse.  The argument should have type void *.
   It should actually point to an object.
   Grammar actions can access the variable by casting it
   to the proper pointer type.  */

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
#  define YYPARSE_PARAM_ARG void *YYPARSE_PARAM
#  define YYPARSE_PARAM_DECL
# else
#  define YYPARSE_PARAM_ARG YYPARSE_PARAM
#  define YYPARSE_PARAM_DECL void *YYPARSE_PARAM;
# endif
#else /* !YYPARSE_PARAM */
# define YYPARSE_PARAM_ARG
# define YYPARSE_PARAM_DECL
#endif /* !YYPARSE_PARAM */

/* Prevent warning if -Wstrict-prototypes.  */
#ifdef __GNUC__
# ifdef YYPARSE_PARAM
int yyparse (void *);
# else
int yyparse (void);
# endif
#endif

/* YY_DECL_VARIABLES -- depending whether we use a pure parser,
   variables are global, or local to YYPARSE.  */

#define YY_DECL_NON_LSP_VARIABLES			\
/* The lookahead symbol.  */				\
int yychar;						\
							\
/* The semantic value of the lookahead symbol. */	\
YYSTYPE yylval;						\
							\
/* Number of parse errors so far.  */			\
int yynerrs;

#if YYLSP_NEEDED
# define YY_DECL_VARIABLES			\
YY_DECL_NON_LSP_VARIABLES			\
						\
/* Location data for the lookahead symbol.  */	\
YYLTYPE yylloc;
#else
# define YY_DECL_VARIABLES			\
YY_DECL_NON_LSP_VARIABLES
#endif


/* If nonreentrant, generate the variables here. */

#if !YYPURE
YY_DECL_VARIABLES
#endif  /* !YYPURE */

int
yyparse (YYPARSE_PARAM_ARG)
     YYPARSE_PARAM_DECL
{
  /* If reentrant, generate the variables here. */
#if YYPURE
  YY_DECL_VARIABLES
#endif  /* !YYPURE */

  register int yystate;
  register int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Lookahead token as an internal (translated) token number.  */
  int yychar1 = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack. */
  short	yyssa[YYINITDEPTH];
  short *yyss = yyssa;
  register short *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  register YYSTYPE *yyvsp;

#if YYLSP_NEEDED
  /* The location stack.  */
  YYLTYPE yylsa[YYINITDEPTH];
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;
#endif

#if YYLSP_NEEDED
# define YYPOPSTACK   (yyvsp--, yyssp--, yylsp--)
#else
# define YYPOPSTACK   (yyvsp--, yyssp--)
#endif

  YYSIZE_T yystacksize = YYINITDEPTH;


  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
#if YYLSP_NEEDED
  YYLTYPE yyloc;
#endif

  /* When reducing, the number of symbols on the RHS of the reduced
     rule. */
  int yylen;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;
#if YYLSP_NEEDED
  yylsp = yyls;
#endif
  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed. so pushing a state here evens the stacks.
     */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyssp >= yyss + yystacksize - 1)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short *yyss1 = yyss;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  */
# if YYLSP_NEEDED
	YYLTYPE *yyls1 = yyls;
	/* This used to be a conditional around just the two extra args,
	   but that might be undefined if yyoverflow is a macro.  */
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yyls1, yysize * sizeof (*yylsp),
		    &yystacksize);
	yyls = yyls1;
# else
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yystacksize);
# endif
	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyoverflowlab;
# else
      /* Extend the stack our own way.  */
      if (yystacksize >= YYMAXDEPTH)
	goto yyoverflowlab;
      yystacksize *= 2;
      if (yystacksize > YYMAXDEPTH)
	yystacksize = YYMAXDEPTH;

      {
	short *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyoverflowlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);
# if YYLSP_NEEDED
	YYSTACK_RELOCATE (yyls);
# endif
# undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
#if YYLSP_NEEDED
      yylsp = yyls + yysize - 1;
#endif

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyssp >= yyss + yystacksize - 1)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* yychar is either YYEMPTY or YYEOF
     or a valid token in external form.  */

  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  /* Convert token to internal form (in yychar1) for indexing tables with */

  if (yychar <= 0)		/* This means end of input. */
    {
      yychar1 = 0;
      yychar = YYEOF;		/* Don't call YYLEX any more */

      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yychar1 = YYTRANSLATE (yychar);

#if YYDEBUG
     /* We have to keep this `#if YYDEBUG', since we use variables
	which are defined only if `YYDEBUG' is set.  */
      if (yydebug)
	{
	  YYFPRINTF (stderr, "Next token is %d (%s",
		     yychar, yytname[yychar1]);
	  /* Give the individual parser a way to print the precise
	     meaning of a token, for further debugging info.  */
# ifdef YYPRINT
	  YYPRINT (stderr, yychar, yylval);
# endif
	  YYFPRINTF (stderr, ")\n");
	}
#endif
    }

  yyn += yychar1;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != yychar1)
    goto yydefault;

  yyn = yytable[yyn];

  /* yyn is what to do for this token type in this state.
     Negative => reduce, -yyn is rule number.
     Positive => shift, yyn is new state.
       New state is final state => don't bother to shift,
       just return success.
     0, or most negative number => error.  */

  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrlab;

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */
  YYDPRINTF ((stderr, "Shifting token %d (%s), ",
	      yychar, yytname[yychar1]));

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;
#if YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  yystate = yyn;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to the semantic value of
     the lookahead token.  This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

#if YYLSP_NEEDED
  /* Similarly for the default location.  Let the user run additional
     commands if for instance locations are ranges.  */
  yyloc = yylsp[1-yylen];
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
#endif

#if YYDEBUG
  /* We have to keep this `#if YYDEBUG', since we use variables which
     are defined only if `YYDEBUG' is set.  */
  if (yydebug)
    {
      int yyi;

      YYFPRINTF (stderr, "Reducing via rule %d (line %d), ",
		 yyn, yyrline[yyn]);

      /* Print the symbols being reduced, and their result.  */
      for (yyi = yyprhs[yyn]; yyrhs[yyi] > 0; yyi++)
	YYFPRINTF (stderr, "%s ", yytname[yyrhs[yyi]]);
      YYFPRINTF (stderr, " -> %s\n", yytname[yyr1[yyn]]);
    }
#endif

  switch (yyn) {

case 5:
#line 72 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
		     new_structure (yyvsp[-5].t->u.s.tag, UNION_P (yyvsp[-5].t), &lexer_line,
				    yyvsp[-2].p, yyvsp[-4].o);
		     do_typedef (yyvsp[0].s, yyvsp[-5].t, &lexer_line);
		     lexer_toplevel_done = 1;
		   ;
    break;}
case 6:
#line 79 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{;
    break;}
case 7:
#line 81 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
		     new_structure (yyvsp[-4].t->u.s.tag, UNION_P (yyvsp[-4].t), &lexer_line,
				    yyvsp[-1].p, yyvsp[-3].o);
		     lexer_toplevel_done = 1;
		   ;
    break;}
case 8:
#line 87 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{;
    break;}
case 9:
#line 91 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	           note_variable (yyvsp[-1].s, adjust_field_type (yyvsp[-2].t, yyvsp[-3].o), yyvsp[-3].o,
				  &lexer_line);
	         ;
    break;}
case 10:
#line 96 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	           note_variable (yyvsp[-2].s, create_array (yyvsp[-3].t, yyvsp[-1].s),
	      		    yyvsp[-4].o, &lexer_line);
	         ;
    break;}
case 11:
#line 101 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	           note_variable (yyvsp[-3].s, create_array (create_array (yyvsp[-4].t, yyvsp[-1].s),
	      				      yyvsp[-2].s),
	      		    yyvsp[-5].o, &lexer_line);
	         ;
    break;}
case 12:
#line 109 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	      lexer_toplevel_done = 1;
	      yyval.t = yyvsp[0].t;
	    ;
    break;}
case 15:
#line 121 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	        note_yacc_type (yyvsp[-4].o, yyvsp[-3].p, yyvsp[-1].p, &lexer_line);
	      ;
    break;}
case 16:
#line 127 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.p = NULL; ;
    break;}
case 17:
#line 129 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
		     pair_p p;
		     for (p = yyvsp[0].p; p->next != NULL; p = p->next)
		       {
		         p->name = NULL;
			 p->type = NULL;
		       }
		     p->name = NULL;
		     p->type = NULL;
		     p->next = yyvsp[-2].p;
		     yyval.p = yyvsp[0].p;
		   ;
    break;}
case 18:
#line 142 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
		     pair_p p;
		     type_p newtype = NULL;
		     if (strcmp (yyvsp[-4].s, "type") == 0)
		       newtype = (type_p) 1;
		     for (p = yyvsp[0].p; p->next != NULL; p = p->next)
		       {
		         p->name = yyvsp[-2].s;
		         p->type = newtype;
		       }
		     p->name = yyvsp[-2].s;
		     p->next = yyvsp[-5].p;
		     p->type = newtype;
		     yyval.p = yyvsp[0].p;
		   ;
    break;}
case 19:
#line 160 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.p = NULL; ;
    break;}
case 20:
#line 162 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	  pair_p p = XCNEW (struct pair);
	  p->next = yyvsp[-1].p;
	  p->line = lexer_line;
	  p->opt = XNEW (struct options);
	  p->opt->name = "tag";
	  p->opt->next = NULL;
	  p->opt->info = (char *)yyvsp[0].s;
	  yyval.p = p;
	;
    break;}
case 21:
#line 173 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	  pair_p p = XCNEW (struct pair);
	  p->next = yyvsp[-1].p;
	  p->line = lexer_line;
	  p->opt = XNEW (struct options);
	  p->opt->name = "tag";
	  p->opt->next = NULL;
	  p->opt->info = xasprintf ("'%s'", yyvsp[0].s);
	  yyval.p = p;
	;
    break;}
case 22:
#line 185 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.p = NULL; ;
    break;}
case 23:
#line 187 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	            pair_p p = XNEW (struct pair);
		    p->type = adjust_field_type (yyvsp[-5].t, yyvsp[-4].o);
		    p->opt = yyvsp[-4].o;
		    p->name = yyvsp[-3].s;
		    p->next = yyvsp[0].p;
		    p->line = lexer_line;
		    yyval.p = p;
		  ;
    break;}
case 24:
#line 197 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	            pair_p p = XNEW (struct pair);
		    p->type = adjust_field_type (create_array (yyvsp[-5].t, yyvsp[-2].s), yyvsp[-4].o);
		    p->opt = yyvsp[-4].o;
		    p->name = yyvsp[-3].s;
		    p->next = yyvsp[0].p;
		    p->line = lexer_line;
		    yyval.p = p;
		  ;
    break;}
case 25:
#line 207 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	            pair_p p = XNEW (struct pair);
		    p->type = create_array (create_array (yyvsp[-6].t, yyvsp[-2].s), yyvsp[-3].s);
		    p->opt = yyvsp[-5].o;
		    p->name = yyvsp[-4].s;
		    p->next = yyvsp[0].p;
		    p->line = lexer_line;
		    yyval.p = p;
		  ;
    break;}
case 26:
#line 217 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.p = yyvsp[0].p; ;
    break;}
case 30:
#line 225 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ ;
    break;}
case 31:
#line 229 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.t = yyvsp[0].t; ;
    break;}
case 32:
#line 231 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.t = resolve_typedef (yyvsp[0].s, &lexer_line); ;
    break;}
case 33:
#line 233 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.t = create_pointer (yyvsp[-1].t); ;
    break;}
case 34:
#line 235 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	   new_structure (yyvsp[-3].s, 0, &lexer_line, yyvsp[-1].p, NULL);
           yyval.t = find_structure (yyvsp[-3].s, 0);
	 ;
    break;}
case 35:
#line 240 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.t = find_structure (yyvsp[0].s, 0); ;
    break;}
case 36:
#line 242 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	   new_structure (yyvsp[-3].s, 1, &lexer_line, yyvsp[-1].p, NULL);
           yyval.t = find_structure (yyvsp[-3].s, 1);
	 ;
    break;}
case 37:
#line 247 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.t = find_structure (yyvsp[0].s, 1); ;
    break;}
case 38:
#line 249 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.t = create_scalar_type (yyvsp[0].s, strlen (yyvsp[0].s)); ;
    break;}
case 39:
#line 251 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.t = create_scalar_type (yyvsp[-3].s, strlen (yyvsp[-3].s)); ;
    break;}
case 41:
#line 256 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ ;
    break;}
case 42:
#line 258 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ ;
    break;}
case 43:
#line 260 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ ;
    break;}
case 44:
#line 263 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.o = NULL; ;
    break;}
case 45:
#line 264 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.o = yyvsp[0].o; ;
    break;}
case 46:
#line 268 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.o = yyvsp[-2].o; ;
    break;}
case 47:
#line 272 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.s = "ptr_alias"; ;
    break;}
case 48:
#line 274 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.s = yyvsp[0].s; ;
    break;}
case 49:
#line 278 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.o = create_option (yyvsp[0].s, (void *)""); ;
    break;}
case 50:
#line 280 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.o = create_option (yyvsp[-3].s, (void *)yyvsp[-1].s); ;
    break;}
case 51:
#line 282 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.o = create_option (yyvsp[-3].s, adjust_field_type (yyvsp[-1].t, NULL)); ;
    break;}
case 52:
#line 284 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	      struct nested_ptr_data d;

	      d.type = adjust_field_type (yyvsp[-5].t, NULL);
	      d.convert_to = yyvsp[-3].s;
	      d.convert_from = yyvsp[-1].s;
	      yyval.o = create_option ("nested_ptr",
				  xmemdup (&d, sizeof (d), sizeof (d)));
	    ;
    break;}
case 53:
#line 296 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	        yyvsp[0].o->next = NULL;
		yyval.o = yyvsp[0].o;
	      ;
    break;}
case 54:
#line 301 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	        yyvsp[0].o->next = yyvsp[-2].o;
		yyval.o = yyvsp[0].o;
	      ;
    break;}
case 55:
#line 307 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.o = NULL; ;
    break;}
case 56:
#line 308 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.o = yyvsp[0].o; ;
    break;}
case 57:
#line 312 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{ yyval.s = yyvsp[0].s; ;
    break;}
case 58:
#line 314 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"
{
	       size_t l1 = strlen (yyvsp[-1].s);
	       size_t l2 = strlen (yyvsp[0].s);
	       char *s = XRESIZEVEC (char, yyvsp[-1].s, l1 + l2 + 1);
	       memcpy (s + l1, yyvsp[0].s, l2 + 1);
	       XDELETE (yyvsp[0].s);
	       yyval.s = s;
	     ;
    break;}
}

#line 705 "/usr/share/bison/bison.simple"


  yyvsp -= yylen;
  yyssp -= yylen;
#if YYLSP_NEEDED
  yylsp -= yylen;
#endif

#if YYDEBUG
  if (yydebug)
    {
      short *yyssp1 = yyss - 1;
      YYFPRINTF (stderr, "state stack now");
      while (yyssp1 != yyssp)
	YYFPRINTF (stderr, " %d", *++yyssp1);
      YYFPRINTF (stderr, "\n");
    }
#endif

  *++yyvsp = yyval;
#if YYLSP_NEEDED
  *++yylsp = yyloc;
#endif

  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTBASE] + *yyssp;
  if (yystate >= 0 && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTBASE];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;

#ifdef YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (yyn > YYFLAG && yyn < YYLAST)
	{
	  YYSIZE_T yysize = 0;
	  char *yymsg;
	  int yyx, yycount;

	  yycount = 0;
	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  for (yyx = yyn < 0 ? -yyn : 0;
	       yyx < (int) (sizeof (yytname) / sizeof (char *)); yyx++)
	    if (yycheck[yyx + yyn] == yyx)
	      yysize += yystrlen (yytname[yyx]) + 15, yycount++;
	  yysize += yystrlen ("parse error, unexpected ") + 1;
	  yysize += yystrlen (yytname[YYTRANSLATE (yychar)]);
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "parse error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[YYTRANSLATE (yychar)]);

	      if (yycount < 5)
		{
		  yycount = 0;
		  for (yyx = yyn < 0 ? -yyn : 0;
		       yyx < (int) (sizeof (yytname) / sizeof (char *));
		       yyx++)
		    if (yycheck[yyx + yyn] == yyx)
		      {
			const char *yyq = ! yycount ? ", expecting " : " or ";
			yyp = yystpcpy (yyp, yyq);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yycount++;
		      }
		}
	      yyerror (yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    yyerror ("parse error; also virtual memory exhausted");
	}
      else
#endif /* defined (YYERROR_VERBOSE) */
	yyerror ("parse error");
    }
  goto yyerrlab1;


/*--------------------------------------------------.
| yyerrlab1 -- error raised explicitly by an action |
`--------------------------------------------------*/
yyerrlab1:
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      /* return failure if at end of input */
      if (yychar == YYEOF)
	YYABORT;
      YYDPRINTF ((stderr, "Discarding token %d (%s).\n",
		  yychar, yytname[yychar1]));
      yychar = YYEMPTY;
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */

  yyerrstatus = 3;		/* Each real token shifted decrements this */

  goto yyerrhandle;


/*-------------------------------------------------------------------.
| yyerrdefault -- current state does not do anything special for the |
| error token.                                                       |
`-------------------------------------------------------------------*/
yyerrdefault:
#if 0
  /* This is wrong; only states that explicitly want error tokens
     should shift them.  */

  /* If its default is to accept any token, ok.  Otherwise pop it.  */
  yyn = yydefact[yystate];
  if (yyn)
    goto yydefault;
#endif


/*---------------------------------------------------------------.
| yyerrpop -- pop the current state because it cannot handle the |
| error token                                                    |
`---------------------------------------------------------------*/
yyerrpop:
  if (yyssp == yyss)
    YYABORT;
  yyvsp--;
  yystate = *--yyssp;
#if YYLSP_NEEDED
  yylsp--;
#endif

#if YYDEBUG
  if (yydebug)
    {
      short *yyssp1 = yyss - 1;
      YYFPRINTF (stderr, "Error: state stack now");
      while (yyssp1 != yyssp)
	YYFPRINTF (stderr, " %d", *++yyssp1);
      YYFPRINTF (stderr, "\n");
    }
#endif

/*--------------.
| yyerrhandle.  |
`--------------*/
yyerrhandle:
  yyn = yypact[yystate];
  if (yyn == YYFLAG)
    goto yyerrdefault;

  yyn += YYTERROR;
  if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != YYTERROR)
    goto yyerrdefault;

  yyn = yytable[yyn];
  if (yyn < 0)
    {
      if (yyn == YYFLAG)
	goto yyerrpop;
      yyn = -yyn;
      goto yyreduce;
    }
  else if (yyn == 0)
    goto yyerrpop;

  if (yyn == YYFINAL)
    YYACCEPT;

  YYDPRINTF ((stderr, "Shifting error token, "));

  *++yyvsp = yylval;
#if YYLSP_NEEDED
  *++yylsp = yylloc;
#endif

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

/*---------------------------------------------.
| yyoverflowab -- parser overflow comes here.  |
`---------------------------------------------*/
yyoverflowlab:
  yyerror ("parser stack overflow");
  yyresult = 2;
  /* Fall through.  */

yyreturn:
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}
#line 323 "/scratch/mitchell/gcc-releases/gcc-4.0.0/gcc-4.0.0/gcc/gengtype-yacc.y"

