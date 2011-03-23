/* A Bison parser, made by GNU Bison 2.4.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

      Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006,
   2009, 2010 Free Software Foundation, Inc.

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

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.4.3"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Copy the first part of user declarations.  */

/* Line 189 of yacc.c  */
#line 26 "yyscript.y"


#include "config.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "script-c.h"



/* Line 189 of yacc.c  */
#line 86 "yyscript.c"

/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 1
#endif

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif


/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     OREQ = 258,
     ANDEQ = 259,
     RSHIFTEQ = 260,
     LSHIFTEQ = 261,
     DIVEQ = 262,
     MULTEQ = 263,
     MINUSEQ = 264,
     PLUSEQ = 265,
     OROR = 266,
     ANDAND = 267,
     NE = 268,
     EQ = 269,
     GE = 270,
     LE = 271,
     RSHIFT = 272,
     LSHIFT = 273,
     UNARY = 274,
     STRING = 275,
     QUOTED_STRING = 276,
     INTEGER = 277,
     ABSOLUTE = 278,
     ADDR = 279,
     ALIGN_K = 280,
     ALIGNOF = 281,
     ASSERT_K = 282,
     AS_NEEDED = 283,
     AT = 284,
     BIND = 285,
     BLOCK = 286,
     BYTE = 287,
     CONSTANT = 288,
     CONSTRUCTORS = 289,
     COPY = 290,
     CREATE_OBJECT_SYMBOLS = 291,
     DATA_SEGMENT_ALIGN = 292,
     DATA_SEGMENT_END = 293,
     DATA_SEGMENT_RELRO_END = 294,
     DEFINED = 295,
     DSECT = 296,
     ENTRY = 297,
     EXCLUDE_FILE = 298,
     EXTERN = 299,
     FILL = 300,
     FLOAT = 301,
     FORCE_COMMON_ALLOCATION = 302,
     GLOBAL = 303,
     GROUP = 304,
     HLL = 305,
     INCLUDE = 306,
     INHIBIT_COMMON_ALLOCATION = 307,
     INFO = 308,
     INPUT = 309,
     KEEP = 310,
     LEN = 311,
     LENGTH = 312,
     LOADADDR = 313,
     LOCAL = 314,
     LONG = 315,
     MAP = 316,
     MAX_K = 317,
     MEMORY = 318,
     MIN_K = 319,
     NEXT = 320,
     NOCROSSREFS = 321,
     NOFLOAT = 322,
     NOLOAD = 323,
     ONLY_IF_RO = 324,
     ONLY_IF_RW = 325,
     ORG = 326,
     ORIGIN = 327,
     OUTPUT = 328,
     OUTPUT_ARCH = 329,
     OUTPUT_FORMAT = 330,
     OVERLAY = 331,
     PHDRS = 332,
     PROVIDE = 333,
     PROVIDE_HIDDEN = 334,
     QUAD = 335,
     SEARCH_DIR = 336,
     SECTIONS = 337,
     SEGMENT_START = 338,
     SHORT = 339,
     SIZEOF = 340,
     SIZEOF_HEADERS = 341,
     SORT_BY_ALIGNMENT = 342,
     SORT_BY_NAME = 343,
     SPECIAL = 344,
     SQUAD = 345,
     STARTUP = 346,
     SUBALIGN = 347,
     SYSLIB = 348,
     TARGET_K = 349,
     TRUNCATE = 350,
     VERSIONK = 351,
     OPTION = 352,
     PARSING_LINKER_SCRIPT = 353,
     PARSING_VERSION_SCRIPT = 354,
     PARSING_DEFSYM = 355,
     PARSING_DYNAMIC_LIST = 356
   };
#endif
/* Tokens.  */
#define OREQ 258
#define ANDEQ 259
#define RSHIFTEQ 260
#define LSHIFTEQ 261
#define DIVEQ 262
#define MULTEQ 263
#define MINUSEQ 264
#define PLUSEQ 265
#define OROR 266
#define ANDAND 267
#define NE 268
#define EQ 269
#define GE 270
#define LE 271
#define RSHIFT 272
#define LSHIFT 273
#define UNARY 274
#define STRING 275
#define QUOTED_STRING 276
#define INTEGER 277
#define ABSOLUTE 278
#define ADDR 279
#define ALIGN_K 280
#define ALIGNOF 281
#define ASSERT_K 282
#define AS_NEEDED 283
#define AT 284
#define BIND 285
#define BLOCK 286
#define BYTE 287
#define CONSTANT 288
#define CONSTRUCTORS 289
#define COPY 290
#define CREATE_OBJECT_SYMBOLS 291
#define DATA_SEGMENT_ALIGN 292
#define DATA_SEGMENT_END 293
#define DATA_SEGMENT_RELRO_END 294
#define DEFINED 295
#define DSECT 296
#define ENTRY 297
#define EXCLUDE_FILE 298
#define EXTERN 299
#define FILL 300
#define FLOAT 301
#define FORCE_COMMON_ALLOCATION 302
#define GLOBAL 303
#define GROUP 304
#define HLL 305
#define INCLUDE 306
#define INHIBIT_COMMON_ALLOCATION 307
#define INFO 308
#define INPUT 309
#define KEEP 310
#define LEN 311
#define LENGTH 312
#define LOADADDR 313
#define LOCAL 314
#define LONG 315
#define MAP 316
#define MAX_K 317
#define MEMORY 318
#define MIN_K 319
#define NEXT 320
#define NOCROSSREFS 321
#define NOFLOAT 322
#define NOLOAD 323
#define ONLY_IF_RO 324
#define ONLY_IF_RW 325
#define ORG 326
#define ORIGIN 327
#define OUTPUT 328
#define OUTPUT_ARCH 329
#define OUTPUT_FORMAT 330
#define OVERLAY 331
#define PHDRS 332
#define PROVIDE 333
#define PROVIDE_HIDDEN 334
#define QUAD 335
#define SEARCH_DIR 336
#define SECTIONS 337
#define SEGMENT_START 338
#define SHORT 339
#define SIZEOF 340
#define SIZEOF_HEADERS 341
#define SORT_BY_ALIGNMENT 342
#define SORT_BY_NAME 343
#define SPECIAL 344
#define SQUAD 345
#define STARTUP 346
#define SUBALIGN 347
#define SYSLIB 348
#define TARGET_K 349
#define TRUNCATE 350
#define VERSIONK 351
#define OPTION 352
#define PARSING_LINKER_SCRIPT 353
#define PARSING_VERSION_SCRIPT 354
#define PARSING_DEFSYM 355
#define PARSING_DYNAMIC_LIST 356




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
{

/* Line 214 of yacc.c  */
#line 53 "yyscript.y"

  /* A string.  */
  struct Parser_string string;
  /* A number.  */
  uint64_t integer;
  /* An expression.  */
  Expression_ptr expr;
  /* An output section header.  */
  struct Parser_output_section_header output_section_header;
  /* An output section trailer.  */
  struct Parser_output_section_trailer output_section_trailer;
  /* A section constraint.  */
  enum Section_constraint constraint;
  /* A complete input section specification.  */
  struct Input_section_spec input_section_spec;
  /* A list of wildcard specifications, with exclusions.  */
  struct Wildcard_sections wildcard_sections;
  /* A single wildcard specification.  */
  struct Wildcard_section wildcard_section;
  /* A list of strings.  */
  String_list_ptr string_list;
  /* Information for a program header.  */
  struct Phdr_info phdr_info;
  /* Used for version scripts and within VERSION {}.  */
  struct Version_dependency_list* deplist;
  struct Version_expression_list* versyms;
  struct Version_tree* versnode;
  enum Script_section_type section_type;



/* Line 214 of yacc.c  */
#line 356 "yyscript.c"
} YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif


/* Copy the second part of user declarations.  */


/* Line 264 of yacc.c  */
#line 368 "yyscript.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int yyi)
#else
static int
YYID (yyi)
    int yyi;
#endif
{
  return yyi;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)				\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack_alloc, Stack, yysize);			\
	Stack = &yyptr->Stack_alloc;					\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  20
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   1253

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  125
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  71
/* YYNRULES -- Number of rules.  */
#define YYNRULES  232
/* YYNRULES -- Number of states.  */
#define YYNSTATES  523

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   356

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   121,     2,     2,     2,    31,    18,     2,
     115,   116,    29,    27,   119,    28,     2,    30,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    13,   120,
      21,     3,    22,    12,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,    17,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,   123,     2,
       2,   122,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   117,    16,   118,   124,     2,     2,     2,
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
       2,     2,     2,     2,     2,     2,     1,     2,     4,     5,
       6,     7,     8,     9,    10,    11,    14,    15,    19,    20,
      23,    24,    25,    26,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     6,     9,    12,    15,    18,    19,    24,
      26,    27,    33,    35,    40,    45,    50,    55,    64,    69,
      74,    75,    81,    86,    87,    93,    95,    97,    99,   104,
     105,   108,   110,   113,   117,   119,   123,   125,   128,   129,
     135,   138,   139,   141,   142,   150,   151,   152,   160,   162,
     166,   169,   174,   179,   185,   187,   189,   191,   193,   195,
     196,   201,   202,   207,   208,   213,   214,   216,   218,   220,
     226,   229,   230,   234,   235,   239,   240,   243,   244,   245,
     248,   251,   253,   258,   265,   270,   272,   277,   279,   281,
     283,   285,   287,   289,   291,   296,   298,   303,   305,   310,
     314,   316,   323,   328,   330,   335,   340,   344,   346,   348,
     350,   352,   357,   360,   367,   371,   372,   383,   386,   387,
     391,   396,   397,   399,   401,   403,   405,   407,   409,   412,
     413,   418,   420,   422,   423,   426,   429,   435,   441,   445,
     449,   453,   457,   461,   465,   469,   473,   477,   484,   491,
     492,   495,   499,   502,   505,   508,   511,   515,   519,   523,
     527,   531,   535,   539,   543,   547,   551,   555,   559,   563,
     567,   571,   575,   579,   583,   589,   591,   593,   600,   607,
     612,   614,   619,   624,   629,   634,   639,   644,   649,   654,
     659,   666,   671,   678,   685,   690,   697,   704,   708,   710,
     712,   715,   721,   723,   725,   728,   733,   739,   746,   748,
     751,   752,   755,   760,   765,   774,   776,   778,   782,   786,
     787,   795,   796,   806,   808,   812,   814,   816,   818,   820,
     822,   823,   825
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
     126,     0,    -1,   111,   127,    -1,   112,   184,    -1,   113,
     180,    -1,   114,   181,    -1,   127,   128,    -1,    -1,    57,
     115,   133,   116,    -1,    60,    -1,    -1,    62,   129,   115,
     136,   116,    -1,    65,    -1,    67,   115,   136,   116,    -1,
      76,   117,   167,   118,    -1,   110,   115,   192,   116,    -1,
      88,   115,   192,   116,    -1,    88,   115,   192,   119,   192,
     119,   192,   116,    -1,    90,   117,   172,   118,    -1,    94,
     115,   192,   116,    -1,    -1,    95,   117,   130,   139,   118,
      -1,   107,   115,   192,   116,    -1,    -1,   109,   117,   131,
     184,   118,    -1,   166,    -1,   132,    -1,   120,    -1,    87,
     115,   192,   116,    -1,    -1,   134,   135,    -1,   192,    -1,
     135,   192,    -1,   135,   119,   192,    -1,   137,    -1,   136,
     195,   137,    -1,   192,    -1,    28,    33,    -1,    -1,    41,
     138,   115,   136,   116,    -1,   139,   140,    -1,    -1,   166,
      -1,    -1,   192,   142,   141,   117,   156,   118,   151,    -1,
      -1,    -1,   143,   145,   147,   148,   149,   144,   150,    -1,
      13,    -1,   115,   116,    13,    -1,   179,    13,    -1,   179,
     115,   116,    13,    -1,   115,   146,   116,    13,    -1,   179,
     115,   146,   116,    13,    -1,    81,    -1,    54,    -1,    48,
      -1,    66,    -1,    89,    -1,    -1,    42,   115,   179,   116,
      -1,    -1,    38,   115,   179,   116,    -1,    -1,   105,   115,
     179,   116,    -1,    -1,    82,    -1,    83,    -1,   102,    -1,
     152,   153,   154,   155,   195,    -1,    22,   192,    -1,    -1,
      42,    22,   192,    -1,    -1,   154,    13,   192,    -1,    -1,
       3,   177,    -1,    -1,    -1,   156,   157,    -1,   176,   193,
      -1,   159,    -1,   158,   115,   177,   116,    -1,    40,   115,
     177,   119,   192,   116,    -1,    58,   115,   177,   116,    -1,
      47,    -1,   101,   115,    47,   116,    -1,   120,    -1,    93,
      -1,   103,    -1,    73,    -1,    97,    -1,    45,    -1,   160,
      -1,    68,   115,   160,   116,    -1,   192,    -1,   161,   115,
     162,   116,    -1,   165,    -1,   101,   115,   165,   116,    -1,
     162,   195,   163,    -1,   163,    -1,   162,   195,    56,   115,
     164,   116,    -1,    56,   115,   164,   116,    -1,   165,    -1,
     101,   115,   163,   116,    -1,   100,   115,   163,   116,    -1,
     164,   195,   165,    -1,   165,    -1,   192,    -1,    29,    -1,
      12,    -1,    55,   115,   192,   116,    -1,   176,   193,    -1,
      40,   115,   177,   119,   192,   116,    -1,   167,   195,   168,
      -1,    -1,   192,   169,    13,   170,     3,   177,   195,   171,
       3,   177,    -1,    64,   192,    -1,    -1,   115,   192,   116,
      -1,   115,   121,   192,   116,    -1,    -1,    85,    -1,    84,
      -1,   122,    -1,    70,    -1,    69,    -1,   123,    -1,   172,
     173,    -1,    -1,   192,   174,   175,   120,    -1,   192,    -1,
      35,    -1,    -1,   192,   175,    -1,    90,   175,    -1,   192,
     115,    35,   116,   175,    -1,    42,   115,   177,   116,   175,
      -1,   192,     3,   177,    -1,   192,    11,   177,    -1,   192,
      10,   177,    -1,   192,     9,   177,    -1,   192,     8,   177,
      -1,   192,     7,   177,    -1,   192,     6,   177,    -1,   192,
       5,   177,    -1,   192,     4,   177,    -1,    91,   115,   192,
       3,   177,   116,    -1,    92,   115,   192,     3,   177,   116,
      -1,    -1,   178,   179,    -1,   115,   179,   116,    -1,    28,
     179,    -1,   121,   179,    -1,   124,   179,    -1,    27,   179,
      -1,   179,    29,   179,    -1,   179,    30,   179,    -1,   179,
      31,   179,    -1,   179,    27,   179,    -1,   179,    28,   179,
      -1,   179,    26,   179,    -1,   179,    25,   179,    -1,   179,
      20,   179,    -1,   179,    19,   179,    -1,   179,    24,   179,
      -1,   179,    23,   179,    -1,   179,    21,   179,    -1,   179,
      22,   179,    -1,   179,    18,   179,    -1,   179,    17,   179,
      -1,   179,    16,   179,    -1,   179,    15,   179,    -1,   179,
      14,   179,    -1,   179,    12,   179,    13,   179,    -1,    35,
      -1,   192,    -1,    75,   115,   179,   119,   179,   116,    -1,
      77,   115,   179,   119,   179,   116,    -1,    53,   115,   192,
     116,    -1,    99,    -1,    39,   115,   192,   116,    -1,    98,
     115,   192,   116,    -1,    37,   115,   192,   116,    -1,    71,
     115,   192,   116,    -1,    85,   115,   192,   116,    -1,    70,
     115,   192,   116,    -1,    46,   115,   192,   116,    -1,    36,
     115,   179,   116,    -1,    38,   115,   179,   116,    -1,    38,
     115,   179,   119,   179,   116,    -1,    44,   115,   179,   116,
      -1,    50,   115,   179,   119,   179,   116,    -1,    52,   115,
     179,   119,   179,   116,    -1,    51,   115,   179,   116,    -1,
      96,   115,   192,   119,   179,   116,    -1,    40,   115,   179,
     119,   192,   116,    -1,   192,     3,   177,    -1,   182,    -1,
     183,    -1,   182,   183,    -1,   117,   189,   120,   118,   120,
      -1,   185,    -1,   186,    -1,   185,   186,    -1,   117,   188,
     118,   120,    -1,   192,   117,   188,   118,   120,    -1,   192,
     117,   188,   118,   187,   120,    -1,   192,    -1,   187,   192,
      -1,    -1,   189,   120,    -1,    61,    13,   189,   120,    -1,
      72,    13,   189,   120,    -1,    61,    13,   189,   120,    72,
      13,   189,   120,    -1,    33,    -1,    34,    -1,   189,   120,
      33,    -1,   189,   120,    34,    -1,    -1,    57,   192,   117,
     190,   189,   194,   118,    -1,    -1,   189,   120,    57,   192,
     117,   191,   189,   194,   118,    -1,    57,    -1,   189,   120,
      57,    -1,    33,    -1,    34,    -1,   120,    -1,   119,    -1,
     120,    -1,    -1,   119,    -1,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   231,   231,   232,   233,   234,   239,   240,   245,   246,
     249,   248,   252,   254,   255,   256,   258,   264,   271,   272,
     275,   274,   278,   281,   280,   284,   285,   286,   294,   302,
     302,   308,   310,   312,   318,   319,   324,   326,   329,   328,
     336,   337,   342,   344,   343,   352,   354,   352,   371,   376,
     381,   386,   391,   396,   405,   407,   412,   417,   422,   432,
     433,   440,   441,   448,   449,   456,   457,   459,   461,   467,
     476,   478,   483,   485,   490,   493,   499,   502,   507,   509,
     515,   516,   517,   519,   521,   523,   530,   531,   537,   539,
     541,   543,   545,   552,   554,   560,   567,   576,   581,   590,
     595,   600,   605,   614,   619,   638,   661,   663,   670,   672,
     677,   687,   689,   690,   696,   697,   702,   706,   708,   713,
     716,   719,   723,   725,   727,   731,   733,   735,   740,   741,
     746,   755,   757,   764,   765,   773,   778,   789,   798,   800,
     806,   812,   818,   824,   830,   836,   842,   848,   850,   856,
     856,   866,   868,   870,   872,   874,   876,   878,   880,   882,
     884,   886,   888,   890,   892,   894,   896,   898,   900,   902,
     904,   906,   908,   910,   912,   914,   916,   918,   920,   922,
     924,   926,   928,   930,   932,   934,   936,   938,   940,   942,
     944,   946,   948,   953,   958,   960,   968,   974,   984,   987,
     988,   992,   998,  1002,  1003,  1007,  1011,  1016,  1023,  1027,
    1035,  1036,  1038,  1040,  1042,  1051,  1056,  1061,  1066,  1073,
    1072,  1083,  1082,  1089,  1094,  1104,  1106,  1113,  1114,  1119,
    1120,  1125,  1126
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "'='", "OREQ", "ANDEQ", "RSHIFTEQ",
  "LSHIFTEQ", "DIVEQ", "MULTEQ", "MINUSEQ", "PLUSEQ", "'?'", "':'", "OROR",
  "ANDAND", "'|'", "'^'", "'&'", "NE", "EQ", "'<'", "'>'", "GE", "LE",
  "RSHIFT", "LSHIFT", "'+'", "'-'", "'*'", "'/'", "'%'", "UNARY", "STRING",
  "QUOTED_STRING", "INTEGER", "ABSOLUTE", "ADDR", "ALIGN_K", "ALIGNOF",
  "ASSERT_K", "AS_NEEDED", "AT", "BIND", "BLOCK", "BYTE", "CONSTANT",
  "CONSTRUCTORS", "COPY", "CREATE_OBJECT_SYMBOLS", "DATA_SEGMENT_ALIGN",
  "DATA_SEGMENT_END", "DATA_SEGMENT_RELRO_END", "DEFINED", "DSECT",
  "ENTRY", "EXCLUDE_FILE", "EXTERN", "FILL", "FLOAT",
  "FORCE_COMMON_ALLOCATION", "GLOBAL", "GROUP", "HLL", "INCLUDE",
  "INHIBIT_COMMON_ALLOCATION", "INFO", "INPUT", "KEEP", "LEN", "LENGTH",
  "LOADADDR", "LOCAL", "LONG", "MAP", "MAX_K", "MEMORY", "MIN_K", "NEXT",
  "NOCROSSREFS", "NOFLOAT", "NOLOAD", "ONLY_IF_RO", "ONLY_IF_RW", "ORG",
  "ORIGIN", "OUTPUT", "OUTPUT_ARCH", "OUTPUT_FORMAT", "OVERLAY", "PHDRS",
  "PROVIDE", "PROVIDE_HIDDEN", "QUAD", "SEARCH_DIR", "SECTIONS",
  "SEGMENT_START", "SHORT", "SIZEOF", "SIZEOF_HEADERS",
  "SORT_BY_ALIGNMENT", "SORT_BY_NAME", "SPECIAL", "SQUAD", "STARTUP",
  "SUBALIGN", "SYSLIB", "TARGET_K", "TRUNCATE", "VERSIONK", "OPTION",
  "PARSING_LINKER_SCRIPT", "PARSING_VERSION_SCRIPT", "PARSING_DEFSYM",
  "PARSING_DYNAMIC_LIST", "'('", "')'", "'{'", "'}'", "','", "';'", "'!'",
  "'o'", "'l'", "'~'", "$accept", "top", "linker_script", "file_cmd",
  "$@1", "$@2", "$@3", "ignore_cmd", "extern_name_list", "$@4",
  "extern_name_list_body", "input_list", "input_list_element", "$@5",
  "sections_block", "section_block_cmd", "$@6", "section_header", "$@7",
  "$@8", "opt_address_and_section_type", "section_type", "opt_at",
  "opt_align", "opt_subalign", "opt_constraint", "section_trailer",
  "opt_memspec", "opt_at_memspec", "opt_phdr", "opt_fill", "section_cmds",
  "section_cmd", "data_length", "input_section_spec",
  "input_section_no_keep", "wildcard_file", "wildcard_sections",
  "wildcard_section", "exclude_names", "wildcard_name",
  "file_or_sections_cmd", "memory_defs", "memory_def", "memory_attr",
  "memory_origin", "memory_length", "phdrs_defs", "phdr_def", "phdr_type",
  "phdr_info", "assignment", "parse_exp", "$@9", "exp", "defsym_expr",
  "dynamic_list_expr", "dynamic_list_nodes", "dynamic_list_node",
  "version_script", "vers_nodes", "vers_node", "verdep", "vers_tag",
  "vers_defns", "$@10", "$@11", "string", "end", "opt_semicolon",
  "opt_comma", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,    61,   258,   259,   260,   261,   262,   263,
     264,   265,    63,    58,   266,   267,   124,    94,    38,   268,
     269,    60,    62,   270,   271,   272,   273,    43,    45,    42,
      47,    37,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,   286,   287,   288,   289,   290,   291,
     292,   293,   294,   295,   296,   297,   298,   299,   300,   301,
     302,   303,   304,   305,   306,   307,   308,   309,   310,   311,
     312,   313,   314,   315,   316,   317,   318,   319,   320,   321,
     322,   323,   324,   325,   326,   327,   328,   329,   330,   331,
     332,   333,   334,   335,   336,   337,   338,   339,   340,   341,
     342,   343,   344,   345,   346,   347,   348,   349,   350,   351,
     352,   353,   354,   355,   356,    40,    41,   123,   125,    44,
      59,    33,   111,   108,   126
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   125,   126,   126,   126,   126,   127,   127,   128,   128,
     129,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     130,   128,   128,   131,   128,   128,   128,   128,   132,   134,
     133,   135,   135,   135,   136,   136,   137,   137,   138,   137,
     139,   139,   140,   141,   140,   143,   144,   142,   145,   145,
     145,   145,   145,   145,   146,   146,   146,   146,   146,   147,
     147,   148,   148,   149,   149,   150,   150,   150,   150,   151,
     152,   152,   153,   153,   154,   154,   155,   155,   156,   156,
     157,   157,   157,   157,   157,   157,   157,   157,   158,   158,
     158,   158,   158,   159,   159,   160,   160,   161,   161,   162,
     162,   162,   162,   163,   163,   163,   164,   164,   165,   165,
     165,   166,   166,   166,   167,   167,   168,   168,   168,   169,
     169,   169,   170,   170,   170,   171,   171,   171,   172,   172,
     173,   174,   174,   175,   175,   175,   175,   175,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   178,
     177,   179,   179,   179,   179,   179,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   180,   181,   182,
     182,   183,   184,   185,   185,   186,   186,   186,   187,   187,
     188,   188,   188,   188,   188,   189,   189,   189,   189,   190,
     189,   191,   189,   189,   189,   192,   192,   193,   193,   194,
     194,   195,   195
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     2,     2,     2,     2,     2,     0,     4,     1,
       0,     5,     1,     4,     4,     4,     4,     8,     4,     4,
       0,     5,     4,     0,     5,     1,     1,     1,     4,     0,
       2,     1,     2,     3,     1,     3,     1,     2,     0,     5,
       2,     0,     1,     0,     7,     0,     0,     7,     1,     3,
       2,     4,     4,     5,     1,     1,     1,     1,     1,     0,
       4,     0,     4,     0,     4,     0,     1,     1,     1,     5,
       2,     0,     3,     0,     3,     0,     2,     0,     0,     2,
       2,     1,     4,     6,     4,     1,     4,     1,     1,     1,
       1,     1,     1,     1,     4,     1,     4,     1,     4,     3,
       1,     6,     4,     1,     4,     4,     3,     1,     1,     1,
       1,     4,     2,     6,     3,     0,    10,     2,     0,     3,
       4,     0,     1,     1,     1,     1,     1,     1,     2,     0,
       4,     1,     1,     0,     2,     2,     5,     5,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     6,     6,     0,
       2,     3,     2,     2,     2,     2,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     5,     1,     1,     6,     6,     4,
       1,     4,     4,     4,     4,     4,     4,     4,     4,     4,
       6,     4,     6,     6,     4,     6,     6,     3,     1,     1,
       2,     5,     1,     1,     2,     4,     5,     6,     1,     2,
       0,     2,     4,     4,     8,     1,     1,     3,     3,     0,
       7,     0,     9,     1,     3,     1,     1,     1,     1,     1,
       0,     1,     0
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     7,     0,     0,     0,     0,     2,   225,   226,   210,
       3,   202,   203,     0,     4,     0,     0,     5,   198,   199,
       1,     0,     0,     0,     9,    10,    12,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    27,
       6,    26,    25,     0,     0,   215,   216,   223,     0,     0,
       0,     0,   204,   210,   149,     0,   200,   149,     0,    29,
       0,     0,   115,     0,     0,   129,     0,     0,     0,    20,
       0,    23,     0,   228,   227,   112,   149,   149,   149,   149,
     149,   149,   149,   149,   149,     0,     0,     0,     0,   211,
       0,   197,     0,     0,     0,     0,     0,     0,     0,     0,
      38,   232,    34,    36,   232,     0,     0,     0,     0,     0,
       0,    41,     0,     0,     0,   138,   146,   145,   144,   143,
     142,   141,   140,   139,   219,     0,     0,   205,   217,   218,
     224,     0,     0,     0,   175,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   180,     0,     0,     0,   150,   176,     0,
       0,   111,     8,    30,    31,   232,    37,     0,    13,   231,
       0,    14,   118,    28,    16,     0,    18,   128,     0,   149,
     149,    19,     0,    22,     0,    15,     0,   212,   213,     0,
     206,     0,   208,   155,   152,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   153,   154,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   201,     0,     0,    32,    11,
       0,    35,     0,   114,   121,     0,   132,   133,   131,     0,
       0,    21,    40,    42,    45,    24,   230,     0,   221,   207,
     209,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   151,
       0,   173,   172,   171,   170,   169,   164,   163,   167,   168,
     166,   165,   162,   161,   159,   160,   156,   157,   158,   113,
      33,   232,   117,     0,     0,     0,     0,   133,     0,   133,
     147,   148,    43,     0,   229,     0,     0,     0,   188,   183,
     189,     0,   181,     0,   191,   187,     0,   194,     0,   179,
     186,   184,     0,     0,   185,     0,   182,     0,    39,     0,
       0,     0,     0,   149,   135,   130,     0,   134,     0,    48,
       0,    59,     0,   220,     0,   230,     0,     0,     0,     0,
       0,     0,     0,   174,     0,   119,   123,   122,   124,     0,
      17,     0,     0,    78,    56,    55,    57,    54,    58,     0,
       0,     0,    61,    50,     0,   214,     0,   190,   196,   192,
     193,   177,   178,   195,   120,   149,   133,   133,     0,    49,
       0,     0,     0,    63,     0,     0,   222,   232,   137,   136,
     110,   109,     0,    92,    85,     0,     0,    90,    88,    91,
       0,    89,    71,    87,    79,     0,    81,    93,     0,    97,
       0,    95,    52,     0,     0,     0,    46,    51,     0,     0,
     149,   149,     0,     0,     0,    44,    73,   149,     0,    80,
      60,     0,     0,    65,    53,   126,   125,   127,     0,     0,
       0,     0,     0,    95,     0,     0,   108,    70,     0,    75,
       0,     0,     0,     0,   232,   100,   103,    62,     0,    66,
      67,    68,    47,   149,     0,    84,     0,    94,    86,    98,
       0,    77,    82,     0,     0,     0,    96,     0,    64,   116,
       0,    72,   149,     0,   232,   232,   107,     0,     0,     0,
      99,    83,    76,    74,    69,   102,     0,   105,   104,     0,
     106,   232,   101
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     5,     6,    40,    60,   111,   113,    41,    96,    97,
     163,   101,   102,   167,   182,   252,   348,   312,   313,   453,
     351,   380,   382,   403,   436,   482,   445,   446,   469,   491,
     504,   398,   424,   425,   426,   427,   428,   474,   475,   505,
     476,    42,   104,   243,   304,   369,   458,   107,   177,   247,
     308,    43,    91,    92,   213,    14,    17,    18,    19,    10,
      11,    12,   191,    50,    51,   186,   317,   158,    75,   315,
     170
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -438
static const yytype_int16 yypact[] =
{
     211,  -438,    57,   251,  -112,    13,  1113,  -438,  -438,   277,
    -438,    57,  -438,   -99,  -438,    17,    76,  -438,  -112,  -438,
    -438,   -80,   -78,   -60,  -438,  -438,  -438,   -48,   -42,   -26,
     -16,    38,    16,    44,    62,    89,    66,    99,    78,  -438,
    -438,  -438,  -438,   178,   951,  -438,  -438,   251,   205,   219,
      -4,   121,  -438,   277,  -438,   123,  -438,  -438,   251,  -438,
     119,   233,  -438,   251,   251,  -438,   251,   251,   251,  -438,
     251,  -438,   251,  -438,  -438,  -438,  -438,  -438,  -438,  -438,
    -438,  -438,  -438,  -438,  -438,   130,    76,    76,   132,   168,
     136,  -438,  1073,    14,   141,   146,   149,   251,   233,   235,
    -438,   -70,  -438,  -438,   217,   159,   -47,   -18,   289,   297,
     186,  -438,   196,    57,   234,  -438,  -438,  -438,  -438,  -438,
    -438,  -438,  -438,  -438,  -438,   207,   231,  -438,  -438,  -438,
     251,    -1,  1073,  1073,  -438,   238,   240,   241,   242,   245,
     247,   248,   250,   253,   254,   256,   266,   268,   269,   273,
     274,   275,   276,  -438,  1073,  1073,  1073,   977,  -438,   246,
     251,  -438,  -438,    -3,  -438,    63,  -438,   278,  -438,  -438,
     233,  -438,   164,  -438,  -438,   251,  -438,  -438,   282,  -438,
    -438,  -438,   181,  -438,   279,  -438,    76,    20,   168,   281,
    -438,    10,  -438,  -438,  -438,  1073,   251,  1073,   251,  1073,
    1073,   251,  1073,  1073,  1073,   251,   251,   251,  1073,  1073,
     251,   251,   251,   648,  -438,  -438,  1073,  1073,  1073,  1073,
    1073,  1073,  1073,  1073,  1073,  1073,  1073,  1073,  1073,  1073,
    1073,  1073,  1073,  1073,  1073,  -438,   283,   251,  -438,  -438,
     233,  -438,   251,  -438,   285,   290,  -438,   129,  -438,   286,
     292,  -438,  -438,  -438,   951,  -438,   293,   388,  -438,  -438,
    -438,   668,   294,   436,   296,   504,   688,   298,   542,   716,
     562,   300,   303,   305,   582,   610,   306,   299,   307,  -438,
    1222,   627,   520,   731,   450,   832,   481,   481,   349,   349,
     349,   349,   302,   302,   315,   315,  -438,  -438,  -438,  -438,
    -438,    69,  -438,   -27,   411,   251,   310,   129,   309,   133,
    -438,  -438,  -438,   243,   168,   308,    76,    76,  -438,  -438,
    -438,  1073,  -438,   251,  -438,  -438,  1073,  -438,  1073,  -438,
    -438,  -438,  1073,  1073,  -438,  1073,  -438,  1073,  -438,   251,
     311,   127,   317,  -438,  -438,  -438,   395,  -438,   318,  -438,
    1006,   389,   999,  -438,   316,   293,   751,   321,   771,   791,
     819,   854,   874,   977,   322,  -438,  -438,  -438,  -438,   440,
    -438,   323,   328,  -438,  -438,  -438,  -438,  -438,  -438,   432,
     333,   368,   447,  -438,   106,   168,   369,  -438,  -438,  -438,
    -438,  -438,  -438,  -438,  -438,  -438,   129,   129,   314,  -438,
     473,  1073,   374,   385,   483,   378,  -438,   379,  -438,  -438,
    -438,  -438,   382,  -438,  -438,   384,   398,  -438,  -438,  -438,
     400,  -438,   478,  -438,  -438,   438,  -438,  -438,   460,  -438,
     178,    77,  -438,   894,  1073,   480,  -438,  -438,   601,   114,
    -438,  -438,     5,   257,   251,  -438,   573,  -438,     7,  -438,
    -438,   922,  1073,    21,  -438,  -438,  -438,  -438,   613,   498,
     502,   505,   503,   506,   543,   604,  -438,  -438,   699,  -438,
     606,   608,   609,   611,   101,  -438,  -438,  -438,   957,  -438,
    -438,  -438,  -438,  -438,   251,  -438,   230,  -438,  -438,  -438,
     251,    39,  -438,   230,    64,    64,  -438,   157,  -438,  -438,
     707,  -438,  -438,   251,   379,   108,  -438,   708,   709,   612,
    -438,  -438,  -438,  -438,  -438,  -438,   230,  -438,  -438,   230,
    -438,   110,  -438
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -438,  -438,  -438,  -438,  -438,  -438,  -438,  -438,  -438,  -438,
    -438,   -87,   555,  -438,  -438,  -438,  -438,  -438,  -438,  -438,
    -438,   442,  -438,  -438,  -438,  -438,  -438,  -438,  -438,  -438,
    -438,  -438,  -438,  -438,  -438,   386,  -438,  -438,  -437,   345,
    -286,   645,  -438,  -438,  -438,  -438,  -438,  -438,  -438,  -438,
    -295,   431,   -55,  -438,   -82,  -438,  -438,  -438,   812,   752,
    -438,   915,  -438,   875,    -8,  -438,  -438,    -2,   497,   574,
    -101
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -109
static const yytype_int16 yytable[] =
{
      13,    15,    94,   172,    44,    16,     7,     8,    55,    13,
     157,   165,   344,    20,   347,     7,     8,   410,    53,   410,
      54,   115,   116,   117,   118,   119,   120,   121,   122,   123,
       7,     8,     7,     8,   411,    57,   411,    58,     7,     8,
       7,     8,   502,     7,     8,    85,   168,   128,   129,   169,
     193,   194,   503,   128,   129,    59,    95,   507,   508,   103,
     510,   105,   106,   471,   108,   109,   110,    61,   112,   174,
     114,   130,   175,   214,   215,    62,   410,   130,   125,   126,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    63,
       7,     8,   257,   411,   339,   164,   103,     7,     8,    64,
     176,   408,   409,   479,   480,   178,   461,   472,   473,    45,
      46,    13,   429,   261,    88,   263,   237,   265,   266,   190,
     268,   269,   270,   481,   249,   250,   274,   275,   189,   192,
     259,    66,   159,    47,   280,   281,   282,   283,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   301,   374,    65,   429,   465,   236,    67,
     375,   238,     7,     8,   472,   473,     7,     8,   103,   410,
     244,   306,   376,   245,     9,   306,   248,    68,   256,   239,
     254,    70,   169,   455,   456,   338,   411,   377,   169,   260,
       7,     8,  -108,    72,   262,   378,   264,     7,     8,   267,
     465,   128,   129,   271,   272,   273,    69,   506,   276,   277,
     278,   366,   367,   509,     7,     8,    71,   496,    86,   307,
     169,    21,   404,   307,   515,   130,   522,   169,   242,   169,
     520,   352,    87,   506,    98,   300,    22,   457,   103,   356,
     302,    89,   410,    93,   358,   309,   359,   124,   346,   368,
     360,   361,   127,   362,   131,   363,   349,   472,   473,   411,
     160,    99,   161,     7,     8,   162,     7,     8,   166,   410,
     132,   133,    32,    33,   100,   173,     7,     8,   134,   135,
     136,   137,   138,   139,     7,     8,   411,   140,   371,   141,
       7,     8,   179,   142,   143,   144,   145,    73,    74,   251,
     180,   340,   181,   342,   464,   309,   439,   309,   354,   355,
      45,    46,   183,   146,   147,     7,     8,   246,   148,   433,
     149,   357,     1,     2,     3,     4,   410,   187,   150,   230,
     231,   232,   233,   234,    47,   171,   169,   364,    48,   151,
     407,   152,   153,   411,   232,   233,   234,     7,     8,    49,
     185,   188,   451,   195,   412,   196,   197,   198,   350,   413,
     199,   414,   200,   201,   155,   202,   235,   156,   203,   204,
     478,   205,   415,   497,   228,   229,   230,   231,   232,   233,
     234,   206,   416,   207,   208,   459,   460,   417,   209,   210,
     211,   212,   470,   240,   309,   309,   431,   255,   258,   299,
     303,   316,   310,   514,   516,    32,    33,   418,   311,   305,
     319,   419,   322,   314,   325,   420,   329,   421,   335,   330,
     516,   331,   334,   336,   341,   343,   353,   365,   499,   345,
     372,   381,   422,   370,   423,   373,   385,   388,   394,   396,
     463,   466,   467,   395,   397,   399,   466,   512,   216,   400,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   221,   222,
     223,   224,   225,   226,   227,   228,   229,   230,   231,   232,
     233,   234,   500,   401,   466,   402,   432,   406,   501,   434,
     435,   466,   466,   466,   438,   466,   437,   440,   169,   441,
     444,   513,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   442,   466,   443,   216,   466,   217,   218,
     219,   220,   221,   222,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,   233,   234,   219,   220,   221,   222,
     223,   224,   225,   226,   227,   228,   229,   230,   231,   232,
     233,   234,   320,   447,   216,   321,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   216,   448,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   216,   452,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   454,   468,   483,   484,   485,   487,
     486,  -108,   216,   323,   217,   218,   219,   220,   221,   222,
     223,   224,   225,   226,   227,   228,   229,   230,   231,   232,
     233,   234,   218,   219,   220,   221,   222,   223,   224,   225,
     226,   227,   228,   229,   230,   231,   232,   233,   234,   488,
     216,   326,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     216,   328,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     216,   332,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     489,   490,   492,   493,   494,   241,   495,   519,   216,   333,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   216,   279,   217,   218,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   216,   318,   217,   218,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   216,   324,   217,   218,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   511,   517,   518,   405,   253,   462,   430,
      56,   216,   327,   217,   218,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   521,   184,   216,   387,   217,   218,
     219,   220,   221,   222,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,   233,   234,   216,   389,   217,   218,
     219,   220,   221,   222,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,   233,   234,   216,   390,   217,   218,
     219,   220,   221,   222,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,   233,   234,    52,   449,    90,   386,
       0,     0,     0,     0,   216,   391,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,    76,    77,    78,    79,    80,    81,
      82,    83,    84,     0,     0,     0,     0,     0,     0,   216,
     392,   217,   218,   219,   220,   221,   222,   223,   224,   225,
     226,   227,   228,   229,   230,   231,   232,   233,   234,   216,
     393,   217,   218,   219,   220,   221,   222,   223,   224,   225,
     226,   227,   228,   229,   230,   231,   232,   233,   234,     0,
     450,   216,   383,   217,   218,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,     0,     0,   132,   133,     0,     0,     0,   477,     7,
       8,   134,   135,   136,   137,   138,   139,     0,     0,     0,
     140,     0,   141,     0,   374,     0,   142,   143,   144,   145,
     375,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   376,   498,     0,     0,   146,   147,     0,     0,
       0,   148,     0,   149,     0,     0,     0,   377,     0,     0,
       0,   150,     0,     0,     0,   378,     0,     0,     0,     0,
     132,   133,   151,     0,   152,   153,     7,     8,   134,   135,
     136,   137,   138,   139,   384,     0,     0,   140,     0,   141,
       0,   154,   379,   142,   143,   144,   145,   155,     0,     0,
     156,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   146,   147,     0,     7,     8,   148,     0,
     149,     0,     0,    21,     0,     0,     0,     0,   150,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    22,   151,
      23,   152,   153,    24,     0,    25,     0,     0,    26,     0,
      27,     0,     0,     0,     0,     0,     0,     0,   154,    28,
       0,     0,     0,     0,   155,     0,     0,   156,     0,     0,
      29,    30,     0,    31,    32,    33,     0,    34,    35,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      36,     0,    37,    38,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    39,   216,   337,   217,   218,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234
};

static const yytype_int16 yycheck[] =
{
       2,     3,    57,   104,     6,   117,    33,    34,    16,    11,
      92,    98,   307,     0,   309,    33,    34,    12,   117,    12,
       3,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      33,    34,    33,    34,    29,   115,    29,   115,    33,    34,
      33,    34,     3,    33,    34,    47,   116,    33,    34,   119,
     132,   133,    13,    33,    34,   115,    58,   494,   495,    61,
     497,    63,    64,    56,    66,    67,    68,   115,    70,   116,
      72,    57,   119,   155,   156,   117,    12,    57,    86,    87,
       3,     4,     5,     6,     7,     8,     9,    10,    11,   115,
      33,    34,    72,    29,   121,    97,    98,    33,    34,   115,
     118,   396,   397,    82,    83,   107,   101,   100,   101,    33,
      34,   113,   398,   195,   118,   197,   119,   199,   200,   120,
     202,   203,   204,   102,   179,   180,   208,   209,   130,   131,
     120,   115,   118,    57,   216,   217,   218,   219,   220,   221,
     222,   223,   224,   225,   226,   227,   228,   229,   230,   231,
     232,   233,   234,   240,    48,   117,   442,   443,   160,   115,
      54,   163,    33,    34,   100,   101,    33,    34,   170,    12,
     172,    42,    66,   175,   117,    42,   178,   115,   186,   116,
     182,   115,   119,    69,    70,   116,    29,    81,   119,   191,
      33,    34,   115,   115,   196,    89,   198,    33,    34,   201,
     486,    33,    34,   205,   206,   207,   117,   493,   210,   211,
     212,    84,    85,    56,    33,    34,   117,   116,    13,    90,
     119,    40,   116,    90,   116,    57,   116,   119,    64,   119,
     516,   313,    13,   519,   115,   237,    55,   123,   240,   321,
     242,   120,    12,   120,   326,   247,   328,   117,   115,   122,
     332,   333,   120,   335,   118,   337,    13,   100,   101,    29,
     119,    28,   116,    33,    34,   116,    33,    34,    33,    12,
      27,    28,    91,    92,    41,   116,    33,    34,    35,    36,
      37,    38,    39,    40,    33,    34,    29,    44,   343,    46,
      33,    34,     3,    50,    51,    52,    53,   119,   120,   118,
       3,   303,   116,   305,    47,   307,   407,   309,   316,   317,
      33,    34,   116,    70,    71,    33,    34,    35,    75,   401,
      77,   323,   111,   112,   113,   114,    12,   120,    85,    27,
      28,    29,    30,    31,    57,   118,   119,   339,    61,    96,
     395,    98,    99,    29,    29,    30,    31,    33,    34,    72,
     116,   120,   434,   115,    40,   115,   115,   115,   115,    45,
     115,    47,   115,   115,   121,   115,   120,   124,   115,   115,
     452,   115,    58,   474,    25,    26,    27,    28,    29,    30,
      31,   115,    68,   115,   115,   440,   441,    73,   115,   115,
     115,   115,   447,   115,   396,   397,   398,   118,   117,   116,
     115,    13,   116,   504,   505,    91,    92,    93,   116,   119,
     116,    97,   116,   120,   116,   101,   116,   103,   119,   116,
     521,   116,   116,   116,    13,   115,   118,   116,   483,   120,
      35,    42,   118,   116,   120,   117,   120,   116,   116,   116,
     442,   443,   444,     3,   116,    13,   448,   502,    12,   116,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,   484,   115,   486,    38,    13,   118,   490,   115,
     105,   493,   494,   495,   116,   497,    13,   115,   119,   115,
      22,   503,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,   115,   516,   115,    12,   519,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,   116,   115,    12,   119,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    12,   115,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    12,   115,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    13,    42,     3,   119,   116,   116,
     115,   115,    12,   119,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,   116,
      12,   119,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      12,   119,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      12,   119,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
     116,    22,   116,   115,   115,   170,   115,   115,    12,   119,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    12,   116,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    12,   116,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    12,   116,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,   116,   116,   116,   384,   182,   442,   398,
      18,    12,   116,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,   519,   113,    12,   116,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    12,   116,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    12,   116,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    11,   430,    53,   355,
      -1,    -1,    -1,    -1,    12,   116,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    -1,    -1,    -1,    -1,    -1,    -1,    12,
     116,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    12,
     116,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    -1,
     116,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    -1,    -1,    27,    28,    -1,    -1,    -1,   116,    33,
      34,    35,    36,    37,    38,    39,    40,    -1,    -1,    -1,
      44,    -1,    46,    -1,    48,    -1,    50,    51,    52,    53,
      54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    66,   116,    -1,    -1,    70,    71,    -1,    -1,
      -1,    75,    -1,    77,    -1,    -1,    -1,    81,    -1,    -1,
      -1,    85,    -1,    -1,    -1,    89,    -1,    -1,    -1,    -1,
      27,    28,    96,    -1,    98,    99,    33,    34,    35,    36,
      37,    38,    39,    40,   115,    -1,    -1,    44,    -1,    46,
      -1,   115,   116,    50,    51,    52,    53,   121,    -1,    -1,
     124,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    70,    71,    -1,    33,    34,    75,    -1,
      77,    -1,    -1,    40,    -1,    -1,    -1,    -1,    85,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    55,    96,
      57,    98,    99,    60,    -1,    62,    -1,    -1,    65,    -1,
      67,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   115,    76,
      -1,    -1,    -1,    -1,   121,    -1,    -1,   124,    -1,    -1,
      87,    88,    -1,    90,    91,    92,    -1,    94,    95,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     107,    -1,   109,   110,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   120,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,   111,   112,   113,   114,   126,   127,    33,    34,   117,
     184,   185,   186,   192,   180,   192,   117,   181,   182,   183,
       0,    40,    55,    57,    60,    62,    65,    67,    76,    87,
      88,    90,    91,    92,    94,    95,   107,   109,   110,   120,
     128,   132,   166,   176,   192,    33,    34,    57,    61,    72,
     188,   189,   186,   117,     3,   189,   183,   115,   115,   115,
     129,   115,   117,   115,   115,   117,   115,   115,   115,   117,
     115,   117,   115,   119,   120,   193,     3,     4,     5,     6,
       7,     8,     9,    10,    11,   192,    13,    13,   118,   120,
     188,   177,   178,   120,   177,   192,   133,   134,   115,    28,
      41,   136,   137,   192,   167,   192,   192,   172,   192,   192,
     192,   130,   192,   131,   192,   177,   177,   177,   177,   177,
     177,   177,   177,   177,   117,   189,   189,   120,    33,    34,
      57,   118,    27,    28,    35,    36,    37,    38,    39,    40,
      44,    46,    50,    51,    52,    53,    70,    71,    75,    77,
      85,    96,    98,    99,   115,   121,   124,   179,   192,   118,
     119,   116,   116,   135,   192,   136,    33,   138,   116,   119,
     195,   118,   195,   116,   116,   119,   118,   173,   192,     3,
       3,   116,   139,   116,   184,   116,   190,   120,   120,   192,
     120,   187,   192,   179,   179,   115,   115,   115,   115,   115,
     115,   115,   115,   115,   115,   115,   115,   115,   115,   115,
     115,   115,   115,   179,   179,   179,    12,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,   120,   192,   119,   192,   116,
     115,   137,    64,   168,   192,   192,    35,   174,   192,   177,
     177,   118,   140,   166,   192,   118,   189,    72,   117,   120,
     192,   179,   192,   179,   192,   179,   179,   192,   179,   179,
     179,   192,   192,   192,   179,   179,   192,   192,   192,   116,
     179,   179,   179,   179,   179,   179,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   179,   179,   116,
     192,   136,   192,   115,   169,   119,    42,    90,   175,   192,
     116,   116,   142,   143,   120,   194,    13,   191,   116,   116,
     116,   119,   116,   119,   116,   116,   119,   116,   119,   116,
     116,   116,   119,   119,   116,   119,   116,    13,   116,   121,
     192,    13,   192,   115,   175,   120,   115,   175,   141,    13,
     115,   145,   179,   118,   189,   189,   179,   192,   179,   179,
     179,   179,   179,   179,   192,   116,    84,    85,   122,   170,
     116,   177,    35,   117,    48,    54,    66,    81,    89,   116,
     146,    42,   147,    13,   115,   120,   194,   116,   116,   116,
     116,   116,   116,   116,   116,     3,   116,   116,   156,    13,
     116,   115,    38,   148,   116,   146,   118,   177,   175,   175,
      12,    29,    40,    45,    47,    58,    68,    73,    93,    97,
     101,   103,   118,   120,   157,   158,   159,   160,   161,   165,
     176,   192,    13,   179,   115,   105,   149,    13,   116,   195,
     115,   115,   115,   115,    22,   151,   152,   115,   115,   193,
     116,   179,   115,   144,    13,    69,    70,   123,   171,   177,
     177,   101,   160,   192,    47,   165,   192,   192,    42,   153,
     177,    56,   100,   101,   162,   163,   165,   116,   179,    82,
      83,   102,   150,     3,   119,   116,   115,   116,   116,   116,
      22,   154,   116,   115,   115,   115,   116,   195,   116,   177,
     192,   192,     3,    13,   155,   164,   165,   163,   163,    56,
     163,   116,   177,   192,   195,   116,   195,   116,   116,   115,
     165,   164,   116
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  However,
   YYFAIL appears to be in use.  Nevertheless, it is formally deprecated
   in Bison 2.4.2's NEWS entry, where a plan to phase it out is
   discussed.  */

#define YYFAIL		goto yyerrlab
#if defined YYFAIL
  /* This is here to suppress warnings from the GCC cpp's
     -Wunused-macros.  Normally we don't worry about that warning, but
     some users do, and we want to make it easy for users to remove
     YYFAIL uses, which will produce warnings from Bison 2.5.  */
#endif

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (closure, YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (&yylval, YYLEX_PARAM)
#else
# define YYLEX yylex (&yylval, closure)
#endif

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
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value, closure); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, void* closure)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep, closure)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    void* closure;
#endif
{
  if (!yyvaluep)
    return;
  YYUSE (closure);
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, void* closure)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep, closure)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    void* closure;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep, closure);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
#else
static void
yy_stack_print (yybottom, yytop)
    yytype_int16 *yybottom;
    yytype_int16 *yytop;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, int yyrule, void* closure)
#else
static void
yy_reduce_print (yyvsp, yyrule, closure)
    YYSTYPE *yyvsp;
    int yyrule;
    void* closure;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
				       , closure);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, Rule, closure); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, void* closure)
#else
static void
yydestruct (yymsg, yytype, yyvaluep, closure)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
    void* closure;
#endif
{
  YYUSE (yyvaluep);
  YYUSE (closure);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {

      default:
	break;
    }
}

/* Prevent warnings from -Wmissing-prototypes.  */
#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void* closure);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */





/*-------------------------.
| yyparse or yypush_parse.  |
`-------------------------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void* closure)
#else
int
yyparse (closure)
    void* closure;
#endif
#endif
{
/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;

    /* Number of syntax errors so far.  */
    int yynerrs;

    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       `yyss': related to states.
       `yyvs': related to semantic values.

       Refer to the stacks thru separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yytoken = 0;
  yyss = yyssa;
  yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */
  yyssp = yyss;
  yyvsp = yyvs;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss_alloc, yyss);
	YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;

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

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 9:

/* Line 1464 of yacc.c  */
#line 247 "yyscript.y"
    { script_set_common_allocation(closure, 1); }
    break;

  case 10:

/* Line 1464 of yacc.c  */
#line 249 "yyscript.y"
    { script_start_group(closure); }
    break;

  case 11:

/* Line 1464 of yacc.c  */
#line 251 "yyscript.y"
    { script_end_group(closure); }
    break;

  case 12:

/* Line 1464 of yacc.c  */
#line 253 "yyscript.y"
    { script_set_common_allocation(closure, 0); }
    break;

  case 15:

/* Line 1464 of yacc.c  */
#line 257 "yyscript.y"
    { script_parse_option(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 16:

/* Line 1464 of yacc.c  */
#line 259 "yyscript.y"
    {
	      if (!script_check_output_format(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length,
					      NULL, 0, NULL, 0))
		YYABORT;
	    }
    break;

  case 17:

/* Line 1464 of yacc.c  */
#line 265 "yyscript.y"
    {
	      if (!script_check_output_format(closure, (yyvsp[(3) - (8)].string).value, (yyvsp[(3) - (8)].string).length,
					      (yyvsp[(5) - (8)].string).value, (yyvsp[(5) - (8)].string).length,
					      (yyvsp[(7) - (8)].string).value, (yyvsp[(7) - (8)].string).length))
		YYABORT;
	    }
    break;

  case 19:

/* Line 1464 of yacc.c  */
#line 273 "yyscript.y"
    { script_add_search_dir(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 20:

/* Line 1464 of yacc.c  */
#line 275 "yyscript.y"
    { script_start_sections(closure); }
    break;

  case 21:

/* Line 1464 of yacc.c  */
#line 277 "yyscript.y"
    { script_finish_sections(closure); }
    break;

  case 22:

/* Line 1464 of yacc.c  */
#line 279 "yyscript.y"
    { script_set_target(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 23:

/* Line 1464 of yacc.c  */
#line 281 "yyscript.y"
    { script_push_lex_into_version_mode(closure); }
    break;

  case 24:

/* Line 1464 of yacc.c  */
#line 283 "yyscript.y"
    { script_pop_lex_mode(closure); }
    break;

  case 29:

/* Line 1464 of yacc.c  */
#line 302 "yyscript.y"
    { script_push_lex_into_expression_mode(closure); }
    break;

  case 30:

/* Line 1464 of yacc.c  */
#line 304 "yyscript.y"
    { script_pop_lex_mode(closure); }
    break;

  case 31:

/* Line 1464 of yacc.c  */
#line 309 "yyscript.y"
    { script_add_extern(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 32:

/* Line 1464 of yacc.c  */
#line 311 "yyscript.y"
    { script_add_extern(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 33:

/* Line 1464 of yacc.c  */
#line 313 "yyscript.y"
    { script_add_extern(closure, (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length); }
    break;

  case 36:

/* Line 1464 of yacc.c  */
#line 325 "yyscript.y"
    { script_add_file(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 37:

/* Line 1464 of yacc.c  */
#line 327 "yyscript.y"
    { script_add_library(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 38:

/* Line 1464 of yacc.c  */
#line 329 "yyscript.y"
    { script_start_as_needed(closure); }
    break;

  case 39:

/* Line 1464 of yacc.c  */
#line 331 "yyscript.y"
    { script_end_as_needed(closure); }
    break;

  case 43:

/* Line 1464 of yacc.c  */
#line 344 "yyscript.y"
    { script_start_output_section(closure, (yyvsp[(1) - (2)].string).value, (yyvsp[(1) - (2)].string).length, &(yyvsp[(2) - (2)].output_section_header)); }
    break;

  case 44:

/* Line 1464 of yacc.c  */
#line 346 "yyscript.y"
    { script_finish_output_section(closure, &(yyvsp[(7) - (7)].output_section_trailer)); }
    break;

  case 45:

/* Line 1464 of yacc.c  */
#line 352 "yyscript.y"
    { script_push_lex_into_expression_mode(closure); }
    break;

  case 46:

/* Line 1464 of yacc.c  */
#line 354 "yyscript.y"
    { script_pop_lex_mode(closure); }
    break;

  case 47:

/* Line 1464 of yacc.c  */
#line 356 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(2) - (7)].output_section_header).address;
	      (yyval.output_section_header).section_type = (yyvsp[(2) - (7)].output_section_header).section_type;
	      (yyval.output_section_header).load_address = (yyvsp[(3) - (7)].expr);
	      (yyval.output_section_header).align = (yyvsp[(4) - (7)].expr);
	      (yyval.output_section_header).subalign = (yyvsp[(5) - (7)].expr);
	      (yyval.output_section_header).constraint = (yyvsp[(7) - (7)].constraint);
	    }
    break;

  case 48:

/* Line 1464 of yacc.c  */
#line 372 "yyscript.y"
    {
	      (yyval.output_section_header).address = NULL;
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 49:

/* Line 1464 of yacc.c  */
#line 377 "yyscript.y"
    {
	      (yyval.output_section_header).address = NULL;
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 50:

/* Line 1464 of yacc.c  */
#line 382 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(1) - (2)].expr);
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 51:

/* Line 1464 of yacc.c  */
#line 387 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(1) - (4)].expr);
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 52:

/* Line 1464 of yacc.c  */
#line 392 "yyscript.y"
    {
	      (yyval.output_section_header).address = NULL;
	      (yyval.output_section_header).section_type = (yyvsp[(2) - (4)].section_type);
	    }
    break;

  case 53:

/* Line 1464 of yacc.c  */
#line 397 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(1) - (5)].expr);
	      (yyval.output_section_header).section_type = (yyvsp[(3) - (5)].section_type);
	    }
    break;

  case 54:

/* Line 1464 of yacc.c  */
#line 406 "yyscript.y"
    { (yyval.section_type) = SCRIPT_SECTION_TYPE_NOLOAD; }
    break;

  case 55:

/* Line 1464 of yacc.c  */
#line 408 "yyscript.y"
    {
	      yyerror(closure, "DSECT section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_DSECT;
	    }
    break;

  case 56:

/* Line 1464 of yacc.c  */
#line 413 "yyscript.y"
    {
	      yyerror(closure, "COPY section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_COPY;
	    }
    break;

  case 57:

/* Line 1464 of yacc.c  */
#line 418 "yyscript.y"
    {
	      yyerror(closure, "INFO section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_INFO;
	    }
    break;

  case 58:

/* Line 1464 of yacc.c  */
#line 423 "yyscript.y"
    {
	      yyerror(closure, "OVERLAY section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_OVERLAY;
	    }
    break;

  case 59:

/* Line 1464 of yacc.c  */
#line 432 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 60:

/* Line 1464 of yacc.c  */
#line 434 "yyscript.y"
    { (yyval.expr) = (yyvsp[(3) - (4)].expr); }
    break;

  case 61:

/* Line 1464 of yacc.c  */
#line 440 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 62:

/* Line 1464 of yacc.c  */
#line 442 "yyscript.y"
    { (yyval.expr) = (yyvsp[(3) - (4)].expr); }
    break;

  case 63:

/* Line 1464 of yacc.c  */
#line 448 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 64:

/* Line 1464 of yacc.c  */
#line 450 "yyscript.y"
    { (yyval.expr) = (yyvsp[(3) - (4)].expr); }
    break;

  case 65:

/* Line 1464 of yacc.c  */
#line 456 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_NONE; }
    break;

  case 66:

/* Line 1464 of yacc.c  */
#line 458 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_ONLY_IF_RO; }
    break;

  case 67:

/* Line 1464 of yacc.c  */
#line 460 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_ONLY_IF_RW; }
    break;

  case 68:

/* Line 1464 of yacc.c  */
#line 462 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_SPECIAL; }
    break;

  case 69:

/* Line 1464 of yacc.c  */
#line 468 "yyscript.y"
    {
	      (yyval.output_section_trailer).fill = (yyvsp[(4) - (5)].expr);
	      (yyval.output_section_trailer).phdrs = (yyvsp[(3) - (5)].string_list);
	    }
    break;

  case 70:

/* Line 1464 of yacc.c  */
#line 477 "yyscript.y"
    { script_set_section_region(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length, 1); }
    break;

  case 72:

/* Line 1464 of yacc.c  */
#line 484 "yyscript.y"
    { script_set_section_region(closure, (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length, 0); }
    break;

  case 74:

/* Line 1464 of yacc.c  */
#line 491 "yyscript.y"
    { (yyval.string_list) = script_string_list_push_back((yyvsp[(1) - (3)].string_list), (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length); }
    break;

  case 75:

/* Line 1464 of yacc.c  */
#line 493 "yyscript.y"
    { (yyval.string_list) = NULL; }
    break;

  case 76:

/* Line 1464 of yacc.c  */
#line 500 "yyscript.y"
    { (yyval.expr) = (yyvsp[(2) - (2)].expr); }
    break;

  case 77:

/* Line 1464 of yacc.c  */
#line 502 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 82:

/* Line 1464 of yacc.c  */
#line 518 "yyscript.y"
    { script_add_data(closure, (yyvsp[(1) - (4)].integer), (yyvsp[(3) - (4)].expr)); }
    break;

  case 83:

/* Line 1464 of yacc.c  */
#line 520 "yyscript.y"
    { script_add_assertion(closure, (yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].string).value, (yyvsp[(5) - (6)].string).length); }
    break;

  case 84:

/* Line 1464 of yacc.c  */
#line 522 "yyscript.y"
    { script_add_fill(closure, (yyvsp[(3) - (4)].expr)); }
    break;

  case 85:

/* Line 1464 of yacc.c  */
#line 524 "yyscript.y"
    {
	      /* The GNU linker uses CONSTRUCTORS for the a.out object
		 file format.  It does nothing when using ELF.  Since
		 some ELF linker scripts use it although it does
		 nothing, we accept it and ignore it.  */
	    }
    break;

  case 88:

/* Line 1464 of yacc.c  */
#line 538 "yyscript.y"
    { (yyval.integer) = QUAD; }
    break;

  case 89:

/* Line 1464 of yacc.c  */
#line 540 "yyscript.y"
    { (yyval.integer) = SQUAD; }
    break;

  case 90:

/* Line 1464 of yacc.c  */
#line 542 "yyscript.y"
    { (yyval.integer) = LONG; }
    break;

  case 91:

/* Line 1464 of yacc.c  */
#line 544 "yyscript.y"
    { (yyval.integer) = SHORT; }
    break;

  case 92:

/* Line 1464 of yacc.c  */
#line 546 "yyscript.y"
    { (yyval.integer) = BYTE; }
    break;

  case 93:

/* Line 1464 of yacc.c  */
#line 553 "yyscript.y"
    { script_add_input_section(closure, &(yyvsp[(1) - (1)].input_section_spec), 0); }
    break;

  case 94:

/* Line 1464 of yacc.c  */
#line 555 "yyscript.y"
    { script_add_input_section(closure, &(yyvsp[(3) - (4)].input_section_spec), 1); }
    break;

  case 95:

/* Line 1464 of yacc.c  */
#line 561 "yyscript.y"
    {
	      (yyval.input_section_spec).file.name = (yyvsp[(1) - (1)].string);
	      (yyval.input_section_spec).file.sort = SORT_WILDCARD_NONE;
	      (yyval.input_section_spec).input_sections.sections = NULL;
	      (yyval.input_section_spec).input_sections.exclude = NULL;
	    }
    break;

  case 96:

/* Line 1464 of yacc.c  */
#line 568 "yyscript.y"
    {
	      (yyval.input_section_spec).file = (yyvsp[(1) - (4)].wildcard_section);
	      (yyval.input_section_spec).input_sections = (yyvsp[(3) - (4)].wildcard_sections);
	    }
    break;

  case 97:

/* Line 1464 of yacc.c  */
#line 577 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(1) - (1)].string);
	      (yyval.wildcard_section).sort = SORT_WILDCARD_NONE;
	    }
    break;

  case 98:

/* Line 1464 of yacc.c  */
#line 582 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(3) - (4)].string);
	      (yyval.wildcard_section).sort = SORT_WILDCARD_BY_NAME;
	    }
    break;

  case 99:

/* Line 1464 of yacc.c  */
#line 591 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = script_string_sort_list_add((yyvsp[(1) - (3)].wildcard_sections).sections, &(yyvsp[(3) - (3)].wildcard_section));
	      (yyval.wildcard_sections).exclude = (yyvsp[(1) - (3)].wildcard_sections).exclude;
	    }
    break;

  case 100:

/* Line 1464 of yacc.c  */
#line 596 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = script_new_string_sort_list(&(yyvsp[(1) - (1)].wildcard_section));
	      (yyval.wildcard_sections).exclude = NULL;
	    }
    break;

  case 101:

/* Line 1464 of yacc.c  */
#line 601 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = (yyvsp[(1) - (6)].wildcard_sections).sections;
	      (yyval.wildcard_sections).exclude = script_string_list_append((yyvsp[(1) - (6)].wildcard_sections).exclude, (yyvsp[(5) - (6)].string_list));
	    }
    break;

  case 102:

/* Line 1464 of yacc.c  */
#line 606 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = NULL;
	      (yyval.wildcard_sections).exclude = (yyvsp[(3) - (4)].string_list);
	    }
    break;

  case 103:

/* Line 1464 of yacc.c  */
#line 615 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(1) - (1)].string);
	      (yyval.wildcard_section).sort = SORT_WILDCARD_NONE;
	    }
    break;

  case 104:

/* Line 1464 of yacc.c  */
#line 620 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(3) - (4)].wildcard_section).name;
	      switch ((yyvsp[(3) - (4)].wildcard_section).sort)
		{
		case SORT_WILDCARD_NONE:
		  (yyval.wildcard_section).sort = SORT_WILDCARD_BY_NAME;
		  break;
		case SORT_WILDCARD_BY_NAME:
		case SORT_WILDCARD_BY_NAME_BY_ALIGNMENT:
		  break;
		case SORT_WILDCARD_BY_ALIGNMENT:
		case SORT_WILDCARD_BY_ALIGNMENT_BY_NAME:
		  (yyval.wildcard_section).sort = SORT_WILDCARD_BY_NAME_BY_ALIGNMENT;
		  break;
		default:
		  abort();
		}
	    }
    break;

  case 105:

/* Line 1464 of yacc.c  */
#line 639 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(3) - (4)].wildcard_section).name;
	      switch ((yyvsp[(3) - (4)].wildcard_section).sort)
		{
		case SORT_WILDCARD_NONE:
		  (yyval.wildcard_section).sort = SORT_WILDCARD_BY_ALIGNMENT;
		  break;
		case SORT_WILDCARD_BY_ALIGNMENT:
		case SORT_WILDCARD_BY_ALIGNMENT_BY_NAME:
		  break;
		case SORT_WILDCARD_BY_NAME:
		case SORT_WILDCARD_BY_NAME_BY_ALIGNMENT:
		  (yyval.wildcard_section).sort = SORT_WILDCARD_BY_ALIGNMENT_BY_NAME;
		  break;
		default:
		  abort();
		}
	    }
    break;

  case 106:

/* Line 1464 of yacc.c  */
#line 662 "yyscript.y"
    { (yyval.string_list) = script_string_list_push_back((yyvsp[(1) - (3)].string_list), (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length); }
    break;

  case 107:

/* Line 1464 of yacc.c  */
#line 664 "yyscript.y"
    { (yyval.string_list) = script_new_string_list((yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 108:

/* Line 1464 of yacc.c  */
#line 671 "yyscript.y"
    { (yyval.string) = (yyvsp[(1) - (1)].string); }
    break;

  case 109:

/* Line 1464 of yacc.c  */
#line 673 "yyscript.y"
    {
	      (yyval.string).value = "*";
	      (yyval.string).length = 1;
	    }
    break;

  case 110:

/* Line 1464 of yacc.c  */
#line 678 "yyscript.y"
    {
	      (yyval.string).value = "?";
	      (yyval.string).length = 1;
	    }
    break;

  case 111:

/* Line 1464 of yacc.c  */
#line 688 "yyscript.y"
    { script_set_entry(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 113:

/* Line 1464 of yacc.c  */
#line 691 "yyscript.y"
    { script_add_assertion(closure, (yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].string).value, (yyvsp[(5) - (6)].string).length); }
    break;

  case 116:

/* Line 1464 of yacc.c  */
#line 703 "yyscript.y"
    { script_add_memory(closure, (yyvsp[(1) - (10)].string).value, (yyvsp[(1) - (10)].string).length, (yyvsp[(2) - (10)].integer), (yyvsp[(6) - (10)].expr), (yyvsp[(10) - (10)].expr)); }
    break;

  case 117:

/* Line 1464 of yacc.c  */
#line 707 "yyscript.y"
    { script_include_directive(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 119:

/* Line 1464 of yacc.c  */
#line 714 "yyscript.y"
    { (yyval.integer) = script_parse_memory_attr(closure, (yyvsp[(2) - (3)].string).value, (yyvsp[(2) - (3)].string).length, 0); }
    break;

  case 120:

/* Line 1464 of yacc.c  */
#line 717 "yyscript.y"
    { (yyval.integer) = script_parse_memory_attr(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length, 1); }
    break;

  case 121:

/* Line 1464 of yacc.c  */
#line 719 "yyscript.y"
    { (yyval.integer) = 0; }
    break;

  case 130:

/* Line 1464 of yacc.c  */
#line 747 "yyscript.y"
    { script_add_phdr(closure, (yyvsp[(1) - (4)].string).value, (yyvsp[(1) - (4)].string).length, (yyvsp[(2) - (4)].integer), &(yyvsp[(3) - (4)].phdr_info)); }
    break;

  case 131:

/* Line 1464 of yacc.c  */
#line 756 "yyscript.y"
    { (yyval.integer) = script_phdr_string_to_type(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 132:

/* Line 1464 of yacc.c  */
#line 758 "yyscript.y"
    { (yyval.integer) = (yyvsp[(1) - (1)].integer); }
    break;

  case 133:

/* Line 1464 of yacc.c  */
#line 764 "yyscript.y"
    { memset(&(yyval.phdr_info), 0, sizeof(struct Phdr_info)); }
    break;

  case 134:

/* Line 1464 of yacc.c  */
#line 766 "yyscript.y"
    {
	      (yyval.phdr_info) = (yyvsp[(2) - (2)].phdr_info);
	      if ((yyvsp[(1) - (2)].string).length == 7 && strncmp((yyvsp[(1) - (2)].string).value, "FILEHDR", 7) == 0)
		(yyval.phdr_info).includes_filehdr = 1;
	      else
		yyerror(closure, "PHDRS syntax error");
	    }
    break;

  case 135:

/* Line 1464 of yacc.c  */
#line 774 "yyscript.y"
    {
	      (yyval.phdr_info) = (yyvsp[(2) - (2)].phdr_info);
	      (yyval.phdr_info).includes_phdrs = 1;
	    }
    break;

  case 136:

/* Line 1464 of yacc.c  */
#line 779 "yyscript.y"
    {
	      (yyval.phdr_info) = (yyvsp[(5) - (5)].phdr_info);
	      if ((yyvsp[(1) - (5)].string).length == 5 && strncmp((yyvsp[(1) - (5)].string).value, "FLAGS", 5) == 0)
		{
		  (yyval.phdr_info).is_flags_valid = 1;
		  (yyval.phdr_info).flags = (yyvsp[(3) - (5)].integer);
		}
	      else
		yyerror(closure, "PHDRS syntax error");
	    }
    break;

  case 137:

/* Line 1464 of yacc.c  */
#line 790 "yyscript.y"
    {
	      (yyval.phdr_info) = (yyvsp[(5) - (5)].phdr_info);
	      (yyval.phdr_info).load_address = (yyvsp[(3) - (5)].expr);
	    }
    break;

  case 138:

/* Line 1464 of yacc.c  */
#line 799 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, (yyvsp[(3) - (3)].expr), 0, 0); }
    break;

  case 139:

/* Line 1464 of yacc.c  */
#line 801 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_add(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 140:

/* Line 1464 of yacc.c  */
#line 807 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_sub(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 141:

/* Line 1464 of yacc.c  */
#line 813 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_mult(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 142:

/* Line 1464 of yacc.c  */
#line 819 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_div(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 143:

/* Line 1464 of yacc.c  */
#line 825 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_lshift(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 144:

/* Line 1464 of yacc.c  */
#line 831 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_rshift(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 145:

/* Line 1464 of yacc.c  */
#line 837 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_bitwise_and(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 146:

/* Line 1464 of yacc.c  */
#line 843 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_bitwise_or(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 147:

/* Line 1464 of yacc.c  */
#line 849 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(3) - (6)].string).value, (yyvsp[(3) - (6)].string).length, (yyvsp[(5) - (6)].expr), 1, 0); }
    break;

  case 148:

/* Line 1464 of yacc.c  */
#line 851 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(3) - (6)].string).value, (yyvsp[(3) - (6)].string).length, (yyvsp[(5) - (6)].expr), 1, 1); }
    break;

  case 149:

/* Line 1464 of yacc.c  */
#line 856 "yyscript.y"
    { script_push_lex_into_expression_mode(closure); }
    break;

  case 150:

/* Line 1464 of yacc.c  */
#line 858 "yyscript.y"
    {
	      script_pop_lex_mode(closure);
	      (yyval.expr) = (yyvsp[(2) - (2)].expr);
	    }
    break;

  case 151:

/* Line 1464 of yacc.c  */
#line 867 "yyscript.y"
    { (yyval.expr) = (yyvsp[(2) - (3)].expr); }
    break;

  case 152:

/* Line 1464 of yacc.c  */
#line 869 "yyscript.y"
    { (yyval.expr) = script_exp_unary_minus((yyvsp[(2) - (2)].expr)); }
    break;

  case 153:

/* Line 1464 of yacc.c  */
#line 871 "yyscript.y"
    { (yyval.expr) = script_exp_unary_logical_not((yyvsp[(2) - (2)].expr)); }
    break;

  case 154:

/* Line 1464 of yacc.c  */
#line 873 "yyscript.y"
    { (yyval.expr) = script_exp_unary_bitwise_not((yyvsp[(2) - (2)].expr)); }
    break;

  case 155:

/* Line 1464 of yacc.c  */
#line 875 "yyscript.y"
    { (yyval.expr) = (yyvsp[(2) - (2)].expr); }
    break;

  case 156:

/* Line 1464 of yacc.c  */
#line 877 "yyscript.y"
    { (yyval.expr) = script_exp_binary_mult((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 157:

/* Line 1464 of yacc.c  */
#line 879 "yyscript.y"
    { (yyval.expr) = script_exp_binary_div((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 158:

/* Line 1464 of yacc.c  */
#line 881 "yyscript.y"
    { (yyval.expr) = script_exp_binary_mod((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 159:

/* Line 1464 of yacc.c  */
#line 883 "yyscript.y"
    { (yyval.expr) = script_exp_binary_add((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 160:

/* Line 1464 of yacc.c  */
#line 885 "yyscript.y"
    { (yyval.expr) = script_exp_binary_sub((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 161:

/* Line 1464 of yacc.c  */
#line 887 "yyscript.y"
    { (yyval.expr) = script_exp_binary_lshift((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 162:

/* Line 1464 of yacc.c  */
#line 889 "yyscript.y"
    { (yyval.expr) = script_exp_binary_rshift((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 163:

/* Line 1464 of yacc.c  */
#line 891 "yyscript.y"
    { (yyval.expr) = script_exp_binary_eq((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 164:

/* Line 1464 of yacc.c  */
#line 893 "yyscript.y"
    { (yyval.expr) = script_exp_binary_ne((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 165:

/* Line 1464 of yacc.c  */
#line 895 "yyscript.y"
    { (yyval.expr) = script_exp_binary_le((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 166:

/* Line 1464 of yacc.c  */
#line 897 "yyscript.y"
    { (yyval.expr) = script_exp_binary_ge((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 167:

/* Line 1464 of yacc.c  */
#line 899 "yyscript.y"
    { (yyval.expr) = script_exp_binary_lt((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 168:

/* Line 1464 of yacc.c  */
#line 901 "yyscript.y"
    { (yyval.expr) = script_exp_binary_gt((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 169:

/* Line 1464 of yacc.c  */
#line 903 "yyscript.y"
    { (yyval.expr) = script_exp_binary_bitwise_and((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 170:

/* Line 1464 of yacc.c  */
#line 905 "yyscript.y"
    { (yyval.expr) = script_exp_binary_bitwise_xor((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 171:

/* Line 1464 of yacc.c  */
#line 907 "yyscript.y"
    { (yyval.expr) = script_exp_binary_bitwise_or((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 172:

/* Line 1464 of yacc.c  */
#line 909 "yyscript.y"
    { (yyval.expr) = script_exp_binary_logical_and((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 173:

/* Line 1464 of yacc.c  */
#line 911 "yyscript.y"
    { (yyval.expr) = script_exp_binary_logical_or((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 174:

/* Line 1464 of yacc.c  */
#line 913 "yyscript.y"
    { (yyval.expr) = script_exp_trinary_cond((yyvsp[(1) - (5)].expr), (yyvsp[(3) - (5)].expr), (yyvsp[(5) - (5)].expr)); }
    break;

  case 175:

/* Line 1464 of yacc.c  */
#line 915 "yyscript.y"
    { (yyval.expr) = script_exp_integer((yyvsp[(1) - (1)].integer)); }
    break;

  case 176:

/* Line 1464 of yacc.c  */
#line 917 "yyscript.y"
    { (yyval.expr) = script_symbol(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 177:

/* Line 1464 of yacc.c  */
#line 919 "yyscript.y"
    { (yyval.expr) = script_exp_function_max((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr)); }
    break;

  case 178:

/* Line 1464 of yacc.c  */
#line 921 "yyscript.y"
    { (yyval.expr) = script_exp_function_min((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr)); }
    break;

  case 179:

/* Line 1464 of yacc.c  */
#line 923 "yyscript.y"
    { (yyval.expr) = script_exp_function_defined((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 180:

/* Line 1464 of yacc.c  */
#line 925 "yyscript.y"
    { (yyval.expr) = script_exp_function_sizeof_headers(); }
    break;

  case 181:

/* Line 1464 of yacc.c  */
#line 927 "yyscript.y"
    { (yyval.expr) = script_exp_function_alignof((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 182:

/* Line 1464 of yacc.c  */
#line 929 "yyscript.y"
    { (yyval.expr) = script_exp_function_sizeof((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 183:

/* Line 1464 of yacc.c  */
#line 931 "yyscript.y"
    { (yyval.expr) = script_exp_function_addr((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 184:

/* Line 1464 of yacc.c  */
#line 933 "yyscript.y"
    { (yyval.expr) = script_exp_function_loadaddr((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 185:

/* Line 1464 of yacc.c  */
#line 935 "yyscript.y"
    { (yyval.expr) = script_exp_function_origin(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 186:

/* Line 1464 of yacc.c  */
#line 937 "yyscript.y"
    { (yyval.expr) = script_exp_function_length(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 187:

/* Line 1464 of yacc.c  */
#line 939 "yyscript.y"
    { (yyval.expr) = script_exp_function_constant((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 188:

/* Line 1464 of yacc.c  */
#line 941 "yyscript.y"
    { (yyval.expr) = script_exp_function_absolute((yyvsp[(3) - (4)].expr)); }
    break;

  case 189:

/* Line 1464 of yacc.c  */
#line 943 "yyscript.y"
    { (yyval.expr) = script_exp_function_align(script_exp_string(".", 1), (yyvsp[(3) - (4)].expr)); }
    break;

  case 190:

/* Line 1464 of yacc.c  */
#line 945 "yyscript.y"
    { (yyval.expr) = script_exp_function_align((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr)); }
    break;

  case 191:

/* Line 1464 of yacc.c  */
#line 947 "yyscript.y"
    { (yyval.expr) = script_exp_function_align(script_exp_string(".", 1), (yyvsp[(3) - (4)].expr)); }
    break;

  case 192:

/* Line 1464 of yacc.c  */
#line 949 "yyscript.y"
    {
	      script_data_segment_align(closure);
	      (yyval.expr) = script_exp_function_data_segment_align((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr));
	    }
    break;

  case 193:

/* Line 1464 of yacc.c  */
#line 954 "yyscript.y"
    {
	      script_data_segment_relro_end(closure);
	      (yyval.expr) = script_exp_function_data_segment_relro_end((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr));
	    }
    break;

  case 194:

/* Line 1464 of yacc.c  */
#line 959 "yyscript.y"
    { (yyval.expr) = script_exp_function_data_segment_end((yyvsp[(3) - (4)].expr)); }
    break;

  case 195:

/* Line 1464 of yacc.c  */
#line 961 "yyscript.y"
    {
	      (yyval.expr) = script_exp_function_segment_start((yyvsp[(3) - (6)].string).value, (yyvsp[(3) - (6)].string).length, (yyvsp[(5) - (6)].expr));
	      /* We need to take note of any SEGMENT_START expressions
		 because they change the behaviour of -Ttext, -Tdata and
		 -Tbss options.  */
	      script_saw_segment_start_expression(closure);
	    }
    break;

  case 196:

/* Line 1464 of yacc.c  */
#line 969 "yyscript.y"
    { (yyval.expr) = script_exp_function_assert((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].string).value, (yyvsp[(5) - (6)].string).length); }
    break;

  case 197:

/* Line 1464 of yacc.c  */
#line 975 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, (yyvsp[(3) - (3)].expr), 0, 0); }
    break;

  case 201:

/* Line 1464 of yacc.c  */
#line 993 "yyscript.y"
    { script_new_vers_node (closure, NULL, (yyvsp[(2) - (5)].versyms)); }
    break;

  case 205:

/* Line 1464 of yacc.c  */
#line 1008 "yyscript.y"
    {
	      script_register_vers_node (closure, NULL, 0, (yyvsp[(2) - (4)].versnode), NULL);
	    }
    break;

  case 206:

/* Line 1464 of yacc.c  */
#line 1012 "yyscript.y"
    {
	      script_register_vers_node (closure, (yyvsp[(1) - (5)].string).value, (yyvsp[(1) - (5)].string).length, (yyvsp[(3) - (5)].versnode),
					 NULL);
	    }
    break;

  case 207:

/* Line 1464 of yacc.c  */
#line 1017 "yyscript.y"
    {
	      script_register_vers_node (closure, (yyvsp[(1) - (6)].string).value, (yyvsp[(1) - (6)].string).length, (yyvsp[(3) - (6)].versnode), (yyvsp[(5) - (6)].deplist));
	    }
    break;

  case 208:

/* Line 1464 of yacc.c  */
#line 1024 "yyscript.y"
    {
	      (yyval.deplist) = script_add_vers_depend (closure, NULL, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length);
	    }
    break;

  case 209:

/* Line 1464 of yacc.c  */
#line 1028 "yyscript.y"
    {
	      (yyval.deplist) = script_add_vers_depend (closure, (yyvsp[(1) - (2)].deplist), (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length);
	    }
    break;

  case 210:

/* Line 1464 of yacc.c  */
#line 1035 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, NULL, NULL); }
    break;

  case 211:

/* Line 1464 of yacc.c  */
#line 1037 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, (yyvsp[(1) - (2)].versyms), NULL); }
    break;

  case 212:

/* Line 1464 of yacc.c  */
#line 1039 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, (yyvsp[(3) - (4)].versyms), NULL); }
    break;

  case 213:

/* Line 1464 of yacc.c  */
#line 1041 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, NULL, (yyvsp[(3) - (4)].versyms)); }
    break;

  case 214:

/* Line 1464 of yacc.c  */
#line 1043 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, (yyvsp[(3) - (8)].versyms), (yyvsp[(7) - (8)].versyms)); }
    break;

  case 215:

/* Line 1464 of yacc.c  */
#line 1052 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, NULL, (yyvsp[(1) - (1)].string).value,
					    (yyvsp[(1) - (1)].string).length, 0);
	    }
    break;

  case 216:

/* Line 1464 of yacc.c  */
#line 1057 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, NULL, (yyvsp[(1) - (1)].string).value,
					    (yyvsp[(1) - (1)].string).length, 1);
	    }
    break;

  case 217:

/* Line 1464 of yacc.c  */
#line 1062 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, (yyvsp[(1) - (3)].versyms), (yyvsp[(3) - (3)].string).value,
                                            (yyvsp[(3) - (3)].string).length, 0);
	    }
    break;

  case 218:

/* Line 1464 of yacc.c  */
#line 1067 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, (yyvsp[(1) - (3)].versyms), (yyvsp[(3) - (3)].string).value,
                                            (yyvsp[(3) - (3)].string).length, 1);
	    }
    break;

  case 219:

/* Line 1464 of yacc.c  */
#line 1073 "yyscript.y"
    { version_script_push_lang (closure, (yyvsp[(2) - (3)].string).value, (yyvsp[(2) - (3)].string).length); }
    break;

  case 220:

/* Line 1464 of yacc.c  */
#line 1075 "yyscript.y"
    {
	      (yyval.versyms) = (yyvsp[(5) - (7)].versyms);
	      version_script_pop_lang(closure);
	    }
    break;

  case 221:

/* Line 1464 of yacc.c  */
#line 1083 "yyscript.y"
    { version_script_push_lang (closure, (yyvsp[(4) - (5)].string).value, (yyvsp[(4) - (5)].string).length); }
    break;

  case 222:

/* Line 1464 of yacc.c  */
#line 1085 "yyscript.y"
    {
	      (yyval.versyms) = script_merge_expressions ((yyvsp[(1) - (9)].versyms), (yyvsp[(7) - (9)].versyms));
	      version_script_pop_lang(closure);
	    }
    break;

  case 223:

/* Line 1464 of yacc.c  */
#line 1090 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, NULL, "extern",
					    sizeof("extern") - 1, 1);
	    }
    break;

  case 224:

/* Line 1464 of yacc.c  */
#line 1095 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, (yyvsp[(1) - (3)].versyms), "extern",
					    sizeof("extern") - 1, 1);
	    }
    break;

  case 225:

/* Line 1464 of yacc.c  */
#line 1105 "yyscript.y"
    { (yyval.string) = (yyvsp[(1) - (1)].string); }
    break;

  case 226:

/* Line 1464 of yacc.c  */
#line 1107 "yyscript.y"
    { (yyval.string) = (yyvsp[(1) - (1)].string); }
    break;



/* Line 1464 of yacc.c  */
#line 3696 "yyscript.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;

  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (closure, YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (closure, yymsg);
	  }
	else
	  {
	    yyerror (closure, YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval, closure);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;


      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp, closure);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  *++yyvsp = yylval;


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

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

#if !defined(yyoverflow) || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (closure, YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval, closure);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp, closure);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}



/* Line 1684 of yacc.c  */
#line 1129 "yyscript.y"
