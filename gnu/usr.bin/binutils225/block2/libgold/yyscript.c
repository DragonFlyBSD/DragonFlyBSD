/* A Bison parser, made by GNU Bison 2.7.12-4996.  */

/* Bison implementation for Yacc-like parsers in C
   
      Copyright (C) 1984, 1989-1990, 2000-2013 Free Software Foundation, Inc.
   
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
#define YYBISON_VERSION "2.7.12-4996"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* Copy the first part of user declarations.  */
/* Line 371 of yacc.c  */
#line 26 "yyscript.y"


#include "config.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "script-c.h"


/* Line 371 of yacc.c  */
#line 81 "yyscript.c"

# ifndef YY_NULL
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULL nullptr
#  else
#   define YY_NULL 0
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 1
#endif

/* In a future release of Bison, this section will be replaced
   by #include "y.tab.h".  */
#ifndef YY_YY_Y_TAB_H_INCLUDED
# define YY_YY_Y_TAB_H_INCLUDED
/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
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
/* Line 387 of yacc.c  */
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


/* Line 387 of yacc.c  */
#line 357 "yyscript.c"
} YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif


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

#endif /* !YY_YY_Y_TAB_H_INCLUDED  */

/* Copy the second part of user declarations.  */

/* Line 390 of yacc.c  */
#line 384 "yyscript.c"

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
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

#ifndef __attribute__
/* This feature is available in gcc versions 2.5 and later.  */
# if (! defined __GNUC__ || __GNUC__ < 2 \
      || (__GNUC__ == 2 && __GNUC_MINOR__ < 5))
#  define __attribute__(Spec) /* empty */
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif


/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(N) (N)
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
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
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
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS && (defined __STDC__ || defined __C99__FUNC__ \
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

# define YYCOPY_NEEDED 1

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

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, (Count) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYSIZE_T yyi;                         \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (YYID (0))
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  20
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   1329

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  125
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  71
/* YYNRULES -- Number of rules.  */
#define YYNRULES  234
/* YYNRULES -- Number of states.  */
#define YYNSTATES  527

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
     248,   251,   253,   258,   265,   270,   272,   277,   280,   282,
     284,   286,   288,   290,   292,   294,   299,   301,   306,   308,
     313,   317,   319,   326,   331,   333,   338,   343,   347,   349,
     351,   353,   355,   360,   363,   370,   373,   377,   378,   389,
     392,   393,   397,   402,   403,   405,   407,   409,   411,   413,
     415,   418,   419,   424,   426,   428,   429,   432,   435,   441,
     447,   451,   455,   459,   463,   467,   471,   475,   479,   483,
     490,   497,   498,   501,   505,   508,   511,   514,   517,   521,
     525,   529,   533,   537,   541,   545,   549,   553,   557,   561,
     565,   569,   573,   577,   581,   585,   589,   595,   597,   599,
     606,   613,   618,   620,   625,   630,   635,   640,   645,   650,
     655,   660,   665,   672,   677,   684,   691,   696,   703,   710,
     714,   716,   718,   721,   727,   729,   731,   734,   739,   745,
     752,   754,   757,   758,   761,   766,   771,   780,   782,   784,
     788,   792,   793,   801,   802,   812,   814,   818,   820,   822,
     824,   826,   828,   829,   831
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
      47,    -1,   101,   115,    47,   116,    -1,    64,   192,    -1,
     120,    -1,    93,    -1,   103,    -1,    73,    -1,    97,    -1,
      45,    -1,   160,    -1,    68,   115,   160,   116,    -1,   192,
      -1,   161,   115,   162,   116,    -1,   165,    -1,   101,   115,
     165,   116,    -1,   162,   195,   163,    -1,   163,    -1,   162,
     195,    56,   115,   164,   116,    -1,    56,   115,   164,   116,
      -1,   165,    -1,   101,   115,   163,   116,    -1,   100,   115,
     163,   116,    -1,   164,   195,   165,    -1,   165,    -1,   192,
      -1,    29,    -1,    12,    -1,    55,   115,   192,   116,    -1,
     176,   193,    -1,    40,   115,   177,   119,   192,   116,    -1,
      64,   192,    -1,   167,   195,   168,    -1,    -1,   192,   169,
      13,   170,     3,   177,   195,   171,     3,   177,    -1,    64,
     192,    -1,    -1,   115,   192,   116,    -1,   115,   121,   192,
     116,    -1,    -1,    85,    -1,    84,    -1,   122,    -1,    70,
      -1,    69,    -1,   123,    -1,   172,   173,    -1,    -1,   192,
     174,   175,   120,    -1,   192,    -1,    35,    -1,    -1,   192,
     175,    -1,    90,   175,    -1,   192,   115,    35,   116,   175,
      -1,    42,   115,   177,   116,   175,    -1,   192,     3,   177,
      -1,   192,    11,   177,    -1,   192,    10,   177,    -1,   192,
       9,   177,    -1,   192,     8,   177,    -1,   192,     7,   177,
      -1,   192,     6,   177,    -1,   192,     5,   177,    -1,   192,
       4,   177,    -1,    91,   115,   192,     3,   177,   116,    -1,
      92,   115,   192,     3,   177,   116,    -1,    -1,   178,   179,
      -1,   115,   179,   116,    -1,    28,   179,    -1,   121,   179,
      -1,   124,   179,    -1,    27,   179,    -1,   179,    29,   179,
      -1,   179,    30,   179,    -1,   179,    31,   179,    -1,   179,
      27,   179,    -1,   179,    28,   179,    -1,   179,    26,   179,
      -1,   179,    25,   179,    -1,   179,    20,   179,    -1,   179,
      19,   179,    -1,   179,    24,   179,    -1,   179,    23,   179,
      -1,   179,    21,   179,    -1,   179,    22,   179,    -1,   179,
      18,   179,    -1,   179,    17,   179,    -1,   179,    16,   179,
      -1,   179,    15,   179,    -1,   179,    14,   179,    -1,   179,
      12,   179,    13,   179,    -1,    35,    -1,   192,    -1,    75,
     115,   179,   119,   179,   116,    -1,    77,   115,   179,   119,
     179,   116,    -1,    53,   115,   192,   116,    -1,    99,    -1,
      39,   115,   192,   116,    -1,    98,   115,   192,   116,    -1,
      37,   115,   192,   116,    -1,    71,   115,   192,   116,    -1,
      85,   115,   192,   116,    -1,    70,   115,   192,   116,    -1,
      46,   115,   192,   116,    -1,    36,   115,   179,   116,    -1,
      38,   115,   179,   116,    -1,    38,   115,   179,   119,   179,
     116,    -1,    44,   115,   179,   116,    -1,    50,   115,   179,
     119,   179,   116,    -1,    52,   115,   179,   119,   179,   116,
      -1,    51,   115,   179,   116,    -1,    96,   115,   192,   119,
     179,   116,    -1,    40,   115,   179,   119,   192,   116,    -1,
     192,     3,   177,    -1,   182,    -1,   183,    -1,   182,   183,
      -1,   117,   189,   120,   118,   120,    -1,   185,    -1,   186,
      -1,   185,   186,    -1,   117,   188,   118,   120,    -1,   192,
     117,   188,   118,   120,    -1,   192,   117,   188,   118,   187,
     120,    -1,   192,    -1,   187,   192,    -1,    -1,   189,   120,
      -1,    61,    13,   189,   120,    -1,    72,    13,   189,   120,
      -1,    61,    13,   189,   120,    72,    13,   189,   120,    -1,
      33,    -1,    34,    -1,   189,   120,    33,    -1,   189,   120,
      34,    -1,    -1,    57,   192,   117,   190,   189,   194,   118,
      -1,    -1,   189,   120,    57,   192,   117,   191,   189,   194,
     118,    -1,    57,    -1,   189,   120,    57,    -1,    33,    -1,
      34,    -1,   120,    -1,   119,    -1,   120,    -1,    -1,   119,
      -1,    -1
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
     515,   516,   517,   519,   521,   523,   530,   531,   533,   539,
     541,   543,   545,   547,   554,   556,   562,   569,   578,   583,
     592,   597,   602,   607,   616,   621,   640,   663,   665,   672,
     674,   679,   689,   691,   692,   694,   700,   701,   706,   710,
     712,   717,   720,   723,   727,   729,   731,   735,   737,   739,
     744,   745,   750,   759,   761,   768,   769,   777,   782,   793,
     802,   804,   810,   816,   822,   828,   834,   840,   846,   852,
     854,   860,   860,   870,   872,   874,   876,   878,   880,   882,
     884,   886,   888,   890,   892,   894,   896,   898,   900,   902,
     904,   906,   908,   910,   912,   914,   916,   918,   920,   922,
     924,   926,   928,   930,   932,   934,   936,   938,   940,   942,
     944,   946,   948,   950,   952,   957,   962,   964,   972,   978,
     988,   991,   992,   996,  1002,  1006,  1007,  1011,  1015,  1020,
    1027,  1031,  1039,  1040,  1042,  1044,  1046,  1055,  1060,  1065,
    1070,  1077,  1076,  1087,  1086,  1093,  1098,  1108,  1110,  1117,
    1118,  1123,  1124,  1129,  1130
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 1
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
  "opt_comma", YY_NULL
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
     157,   157,   157,   157,   157,   157,   157,   157,   157,   158,
     158,   158,   158,   158,   159,   159,   160,   160,   161,   161,
     162,   162,   162,   162,   163,   163,   163,   164,   164,   165,
     165,   165,   166,   166,   166,   166,   167,   167,   168,   168,
     168,   169,   169,   169,   170,   170,   170,   171,   171,   171,
     172,   172,   173,   174,   174,   175,   175,   175,   175,   175,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   178,   177,   179,   179,   179,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   179,   179,   180,
     181,   182,   182,   183,   184,   185,   185,   186,   186,   186,
     187,   187,   188,   188,   188,   188,   188,   189,   189,   189,
     189,   190,   189,   191,   189,   189,   189,   192,   192,   193,
     193,   194,   194,   195,   195
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
       2,     1,     4,     6,     4,     1,     4,     2,     1,     1,
       1,     1,     1,     1,     1,     4,     1,     4,     1,     4,
       3,     1,     6,     4,     1,     4,     4,     3,     1,     1,
       1,     1,     4,     2,     6,     2,     3,     0,    10,     2,
       0,     3,     4,     0,     1,     1,     1,     1,     1,     1,
       2,     0,     4,     1,     1,     0,     2,     2,     5,     5,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     6,
       6,     0,     2,     3,     2,     2,     2,     2,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     5,     1,     1,     6,
       6,     4,     1,     4,     4,     4,     4,     4,     4,     4,
       4,     4,     6,     4,     6,     6,     4,     6,     6,     3,
       1,     1,     2,     5,     1,     1,     2,     4,     5,     6,
       1,     2,     0,     2,     4,     4,     8,     1,     1,     3,
       3,     0,     7,     0,     9,     1,     3,     1,     1,     1,
       1,     1,     0,     1,     0
};

/* YYDEFACT[STATE-NAME] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     7,     0,     0,     0,     0,     2,   227,   228,   212,
       3,   204,   205,     0,     4,     0,     0,     5,   200,   201,
       1,     0,     0,     0,     9,    10,     0,    12,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      27,     6,    26,    25,     0,     0,   217,   218,   225,     0,
       0,     0,     0,   206,   212,   151,     0,   202,   151,     0,
      29,     0,   115,     0,   117,     0,     0,   131,     0,     0,
       0,    20,     0,    23,     0,   230,   229,   113,   151,   151,
     151,   151,   151,   151,   151,   151,   151,     0,     0,     0,
       0,   213,     0,   199,     0,     0,     0,     0,     0,     0,
       0,     0,    38,   234,    34,    36,   234,     0,     0,     0,
       0,     0,     0,    41,     0,     0,     0,   140,   148,   147,
     146,   145,   144,   143,   142,   141,   221,     0,     0,   207,
     219,   220,   226,     0,     0,     0,   177,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   182,     0,     0,     0,   152,
     178,     0,     0,   112,     8,    30,    31,   234,    37,     0,
      13,   233,     0,    14,   120,    28,    16,     0,    18,   130,
       0,   151,   151,    19,     0,    22,     0,    15,     0,   214,
     215,     0,   208,     0,   210,   157,   154,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   155,   156,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   203,     0,     0,
      32,    11,     0,    35,     0,   116,   123,     0,   134,   135,
     133,     0,     0,    21,    40,    42,    45,    24,   232,     0,
     223,   209,   211,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   153,     0,   175,   174,   173,   172,   171,   166,   165,
     169,   170,   168,   167,   164,   163,   161,   162,   158,   159,
     160,   114,    33,   234,   119,     0,     0,     0,     0,   135,
       0,   135,   149,   150,    43,     0,   231,     0,     0,     0,
     190,   185,   191,     0,   183,     0,   193,   189,     0,   196,
       0,   181,   188,   186,     0,     0,   187,     0,   184,     0,
      39,     0,     0,     0,     0,   151,   137,   132,     0,   136,
       0,    48,     0,    59,     0,   222,     0,   232,     0,     0,
       0,     0,     0,     0,     0,   176,     0,   121,   125,   124,
     126,     0,    17,     0,     0,    78,    56,    55,    57,    54,
      58,     0,     0,     0,    61,    50,     0,   216,     0,   192,
     198,   194,   195,   179,   180,   197,   122,   151,   135,   135,
       0,    49,     0,     0,     0,    63,     0,     0,   224,   234,
     139,   138,   111,   110,     0,    93,    85,     0,     0,     0,
      91,    89,    92,     0,    90,    71,    88,    79,     0,    81,
      94,     0,    98,     0,    96,    52,     0,     0,     0,    46,
      51,     0,     0,   151,   151,    87,     0,     0,     0,    44,
      73,   151,     0,    80,    60,     0,     0,    65,    53,   128,
     127,   129,     0,     0,     0,     0,     0,    96,     0,     0,
     109,    70,     0,    75,     0,     0,     0,     0,   234,   101,
     104,    62,     0,    66,    67,    68,    47,   151,     0,    84,
       0,    95,    86,    99,     0,    77,    82,     0,     0,     0,
      97,     0,    64,   118,     0,    72,   151,     0,   234,   234,
     108,     0,     0,     0,   100,    83,    76,    74,    69,   103,
       0,   106,   105,     0,   107,   234,   102
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     5,     6,    41,    61,   113,   115,    42,    98,    99,
     165,   103,   104,   169,   184,   254,   350,   314,   315,   457,
     353,   382,   384,   405,   439,   486,   449,   450,   473,   495,
     508,   400,   427,   428,   429,   430,   431,   478,   479,   509,
     480,    43,   106,   245,   306,   371,   462,   109,   179,   249,
     310,    44,    93,    94,   215,    14,    17,    18,    19,    10,
      11,    12,   193,    51,    52,   188,   319,   160,    77,   317,
     172
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -396
static const yytype_int16 yypact[] =
{
     155,  -396,    42,    90,  -106,    13,  1169,  -396,  -396,   122,
    -396,    42,  -396,   -85,  -396,    44,   135,  -396,  -106,  -396,
    -396,   -72,   -55,   -46,  -396,  -396,    90,  -396,   -11,    -7,
       4,    19,    29,    46,    52,    69,    35,    78,    54,    82,
    -396,  -396,  -396,  -396,    20,   321,  -396,  -396,    90,   144,
     202,    -6,    97,  -396,   122,  -396,   103,  -396,  -396,    90,
    -396,   110,  -396,   185,  -396,    90,    90,  -396,    90,    90,
      90,  -396,    90,  -396,    90,  -396,  -396,  -396,  -396,  -396,
    -396,  -396,  -396,  -396,  -396,  -396,  -396,   111,   135,   135,
     118,   143,   127,  -396,  1129,    72,   124,   113,   157,    90,
     185,   265,  -396,   -63,  -396,  -396,    84,   186,   108,    40,
     301,   305,   218,  -396,   220,    42,   232,  -396,  -396,  -396,
    -396,  -396,  -396,  -396,  -396,  -396,  -396,   229,   231,  -396,
    -396,  -396,    90,    -3,  1129,  1129,  -396,   237,   258,   259,
     262,   263,   289,   300,   302,   303,   304,   306,   309,   332,
     333,   334,   337,   338,   339,  -396,  1129,  1129,  1129,  1298,
    -396,   335,    90,  -396,  -396,    -5,  -396,   120,  -396,   341,
    -396,  -396,   185,  -396,   131,  -396,  -396,    90,  -396,  -396,
     310,  -396,  -396,  -396,    56,  -396,   340,  -396,   135,   109,
     143,   343,  -396,    25,  -396,  -396,  -396,  1129,    90,  1129,
      90,  1129,  1129,    90,  1129,  1129,  1129,    90,    90,    90,
    1129,  1129,    90,    90,    90,   234,  -396,  -396,  1129,  1129,
    1129,  1129,  1129,  1129,  1129,  1129,  1129,  1129,  1129,  1129,
    1129,  1129,  1129,  1129,  1129,  1129,  1129,  -396,   345,    90,
    -396,  -396,   185,  -396,    90,  -396,   342,   344,  -396,    59,
    -396,   349,   351,  -396,  -396,  -396,   321,  -396,   348,   407,
    -396,  -396,  -396,   718,   353,   260,   356,   411,   744,   358,
     638,   764,   658,   360,   361,   362,   678,   698,   363,   365,
     366,  -396,  1278,   590,   364,   801,   617,   900,   291,   291,
     383,   383,   383,   383,   204,   204,   372,   372,  -396,  -396,
    -396,  -396,  -396,   125,  -396,   -27,   467,    90,   368,    59,
     367,    99,  -396,  -396,  -396,   476,   143,   373,   135,   135,
    -396,  -396,  -396,  1129,  -396,    90,  -396,  -396,  1129,  -396,
    1129,  -396,  -396,  -396,  1129,  1129,  -396,  1129,  -396,  1129,
    -396,    90,   369,    51,   374,  -396,  -396,  -396,   446,  -396,
     376,  -396,  1037,   452,  1029,  -396,   378,   348,   784,   384,
     821,   847,   867,   887,   924,  1298,   385,  -396,  -396,  -396,
    -396,   499,  -396,   390,   391,  -396,  -396,  -396,  -396,  -396,
    -396,   495,   401,   404,   485,  -396,    37,   143,   406,  -396,
    -396,  -396,  -396,  -396,  -396,  -396,  -396,  -396,    59,    59,
     531,  -396,   512,  1129,   416,   427,   520,   418,  -396,   417,
    -396,  -396,  -396,  -396,   420,  -396,  -396,   422,    90,   423,
    -396,  -396,  -396,   425,  -396,   519,  -396,  -396,   429,  -396,
    -396,   430,  -396,    20,    12,  -396,   950,  1129,   433,  -396,
    -396,   529,    39,  -396,  -396,  -396,    53,   175,    90,  -396,
     507,  -396,    15,  -396,  -396,   970,  1129,   -57,  -396,  -396,
    -396,  -396,   547,   435,   436,   440,   441,   443,   447,   450,
    -396,  -396,   534,  -396,   451,   444,   453,   454,   176,  -396,
    -396,  -396,   990,  -396,  -396,  -396,  -396,  -396,    90,  -396,
     187,  -396,  -396,  -396,    90,    75,  -396,   187,    50,    50,
    -396,    21,  -396,  -396,   461,  -396,  -396,    90,   417,   180,
    -396,   463,   464,   455,  -396,  -396,  -396,  -396,  -396,  -396,
     187,  -396,  -396,   187,  -396,   181,  -396
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -396,  -396,  -396,  -396,  -396,  -396,  -396,  -396,  -396,  -396,
    -396,   -98,   409,  -396,  -396,  -396,  -396,  -396,  -396,  -396,
    -396,   196,  -396,  -396,  -396,  -396,  -396,  -396,  -396,  -396,
    -396,  -396,  -396,  -396,  -396,   116,  -396,  -396,  -313,    60,
    -395,   400,  -396,  -396,  -396,  -396,  -396,  -396,  -396,  -396,
    -299,   188,   -44,  -396,   136,  -396,  -396,  -396,   567,   471,
    -396,   576,  -396,   536,    -8,  -396,  -396,    -2,   160,   239,
    -103
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -110
static const yytype_int16 yytable[] =
{
      13,    15,   167,   174,    45,   432,     7,     8,    56,    13,
     346,    16,   349,    20,    96,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    62,   483,   484,   412,     7,     8,
       7,     8,    54,   412,   117,   118,   119,   120,   121,   122,
     123,   124,   125,    58,   413,   485,    87,    55,     7,     8,
     413,   432,   469,   170,     7,     8,   171,    97,     7,     8,
      59,   105,   412,   107,   108,   412,   110,   111,   112,    60,
     114,   475,   116,     7,     8,     7,     8,   513,   506,   413,
     127,   128,   413,     7,     8,   376,     7,     8,   507,     7,
       8,   377,     7,     8,   341,   469,    21,   166,   105,   410,
     411,   308,   510,   378,    63,   130,   131,   180,   459,   460,
      64,    22,    90,    13,   239,   476,   477,   192,   379,    65,
      26,   476,   477,     7,     8,   524,   380,  -109,   510,   132,
     191,   194,     7,     8,    66,   368,   369,   251,   252,    75,
      76,   308,   130,   131,   303,   261,    67,    33,    34,   309,
     476,   477,    71,   406,   465,    46,    47,    88,   178,     9,
     238,    68,   461,   240,     7,     8,   132,    69,    46,    47,
     105,    73,   246,   370,   253,   247,   130,   131,   250,    48,
     258,   259,   256,    49,    70,   511,   512,   412,   514,   309,
     161,   262,    48,    72,    50,   244,   264,    74,   266,   412,
     132,   269,   173,   171,   413,   273,   274,   275,     7,     8,
     278,   279,   280,   101,   348,    89,   413,    91,     7,     8,
       7,     8,   468,    95,   176,   100,   102,   177,   126,   163,
     159,   232,   233,   234,   235,   236,   241,   302,   129,   171,
     105,   340,   304,   162,   171,   133,   218,   311,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,   236,     1,     2,     3,     4,
     195,   196,   218,   164,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,   500,   216,   217,   171,   519,   526,   168,   171,
     171,   373,   175,   342,   181,   344,   442,   311,   182,   311,
     356,   357,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,   236,   359,    78,    79,    80,    81,    82,    83,
      84,    85,    86,   263,   183,   265,   185,   267,   268,   366,
     270,   271,   272,     7,     8,   248,   276,   277,   187,   189,
     281,   190,   197,   409,   282,   283,   284,   285,   286,   287,
     288,   289,   290,   291,   292,   293,   294,   295,   296,   297,
     298,   299,   300,   198,   199,   501,   322,   200,   201,   323,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,   236,   311,   311,   434,   463,
     464,   234,   235,   236,   202,   518,   520,   474,   230,   231,
     232,   233,   234,   235,   236,   203,   445,   204,   205,   206,
     318,   207,   520,   218,   208,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,   236,   503,   467,   470,   471,   209,   210,   211,
     470,   354,   212,   213,   214,   237,   242,   305,   257,   358,
     260,   301,   516,   307,   360,   312,   361,   313,   316,   321,
     362,   363,   324,   364,   327,   365,   331,   332,   333,   336,
     343,   374,   338,   345,   337,   367,   504,   347,   470,   351,
     372,   355,   505,   375,   383,   470,   470,   470,   387,   470,
     390,   396,   397,   134,   135,   517,   398,   399,   401,     7,
       8,   136,   137,   138,   139,   140,   141,   402,   470,   403,
     142,   470,   143,   404,   408,   435,   144,   145,   146,   147,
     325,   437,   438,   440,   441,   443,   171,   444,   446,   436,
     447,   448,   458,   412,   451,   452,   148,   149,   456,   472,
     487,   150,   489,   151,   488,   490,   494,   491,  -109,   497,
     413,   152,   466,   492,     7,     8,   493,   496,   498,   499,
     523,   414,   153,   455,   154,   155,   415,   515,   416,   521,
     522,   243,   407,   525,   255,    57,   186,    53,   433,   417,
      92,   352,   482,   453,     0,   418,   388,   157,     0,   419,
     158,     0,     0,     0,   420,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,    33,    34,   421,     0,     0,     0,   422,     0,
       0,     0,   423,     0,   424,   223,   224,   225,   226,   227,
     228,   229,   230,   231,   232,   233,   234,   235,   236,   425,
     218,   426,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     218,     0,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     218,     0,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     218,     0,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     218,     0,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
       0,     0,     0,     0,     0,     0,   218,   328,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,   236,   218,   330,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,   236,   218,   334,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,   236,     0,   335,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,   236,   218,   320,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,   236,     0,     0,     0,     0,     0,     0,   218,
     326,   219,   220,   221,   222,   223,   224,   225,   226,   227,
     228,   229,   230,   231,   232,   233,   234,   235,   236,   218,
     329,   219,   220,   221,   222,   223,   224,   225,   226,   227,
     228,   229,   230,   231,   232,   233,   234,   235,   236,   218,
     389,   219,   220,   221,   222,   223,   224,   225,   226,   227,
     228,   229,   230,   231,   232,   233,   234,   235,   236,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,     0,     0,     0,     0,   218,   391,   219,   220,
     221,   222,   223,   224,   225,   226,   227,   228,   229,   230,
     231,   232,   233,   234,   235,   236,     0,     0,     0,     0,
       0,     0,   218,   392,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,   218,   393,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,   218,   394,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     395,   218,   385,   219,   220,   221,   222,   223,   224,   225,
     226,   227,   228,   229,   230,   231,   232,   233,   234,   235,
     236,     0,     0,     0,   134,   135,   454,     0,     0,     0,
       7,     8,   136,   137,   138,   139,   140,   141,     0,     0,
       0,   142,     0,   143,     0,   376,   481,   144,   145,   146,
     147,   377,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   378,     0,     0,   502,   148,   149,     0,
       0,     0,   150,     0,   151,     0,     0,     0,   379,     0,
       0,     0,   152,     0,     0,     0,   380,     0,     0,     0,
       0,     0,     0,   153,     0,   154,   155,     0,     0,     0,
       0,     0,     0,     0,   386,     0,     0,     0,     0,     0,
       0,     0,   156,   381,     0,     0,   134,   135,   157,     0,
       0,   158,     7,     8,   136,   137,   138,   139,   140,   141,
       0,     0,     0,   142,     0,   143,     0,     0,     0,   144,
     145,   146,   147,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   148,
     149,     0,     7,     8,   150,     0,   151,     0,     0,    21,
       0,     0,     0,     0,   152,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    22,   153,    23,   154,   155,    24,
       0,    25,     0,    26,    27,     0,    28,     0,     0,     0,
       0,     0,     0,     0,   156,    29,     0,     0,     0,     0,
     157,     0,     0,   158,     0,     0,    30,    31,     0,    32,
      33,    34,     0,    35,    36,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    37,     0,    38,    39,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    40,
     218,   339,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     218,     0,   219,   220,   221,   222,   223,   224,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236
};

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-396)))

#define yytable_value_is_error(Yytable_value) \
  YYID (0)

static const yytype_int16 yycheck[] =
{
       2,     3,   100,   106,     6,   400,    33,    34,    16,    11,
     309,   117,   311,     0,    58,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    26,    82,    83,    12,    33,    34,
      33,    34,   117,    12,    78,    79,    80,    81,    82,    83,
      84,    85,    86,   115,    29,   102,    48,     3,    33,    34,
      29,   446,   447,   116,    33,    34,   119,    59,    33,    34,
     115,    63,    12,    65,    66,    12,    68,    69,    70,   115,
      72,    56,    74,    33,    34,    33,    34,    56,     3,    29,
      88,    89,    29,    33,    34,    48,    33,    34,    13,    33,
      34,    54,    33,    34,   121,   490,    40,    99,   100,   398,
     399,    42,   497,    66,   115,    33,    34,   109,    69,    70,
     117,    55,   118,   115,   119,   100,   101,   120,    81,   115,
      64,   100,   101,    33,    34,   520,    89,   115,   523,    57,
     132,   133,    33,    34,   115,    84,    85,   181,   182,   119,
     120,    42,    33,    34,   242,   120,   117,    91,    92,    90,
     100,   101,   117,   116,   101,    33,    34,    13,   118,   117,
     162,   115,   123,   165,    33,    34,    57,   115,    33,    34,
     172,   117,   174,   122,   118,   177,    33,    34,   180,    57,
     188,    72,   184,    61,   115,   498,   499,    12,   501,    90,
     118,   193,    57,   115,    72,    64,   198,   115,   200,    12,
      57,   203,   118,   119,    29,   207,   208,   209,    33,    34,
     212,   213,   214,    28,   115,    13,    29,   120,    33,    34,
      33,    34,    47,   120,   116,   115,    41,   119,   117,   116,
      94,    27,    28,    29,    30,    31,   116,   239,   120,   119,
     242,   116,   244,   119,   119,   118,    12,   249,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,   111,   112,   113,   114,
     134,   135,    12,   116,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,   116,   157,   158,   119,   116,   116,    33,   119,
     119,   345,   116,   305,     3,   307,   409,   309,     3,   311,
     318,   319,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,   325,     3,     4,     5,     6,     7,     8,
       9,    10,    11,   197,   116,   199,   116,   201,   202,   341,
     204,   205,   206,    33,    34,    35,   210,   211,   116,   120,
     116,   120,   115,   397,   218,   219,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,   236,   115,   115,   478,   116,   115,   115,   119,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,   398,   399,   400,   443,
     444,    29,    30,    31,   115,   508,   509,   451,    25,    26,
      27,    28,    29,    30,    31,   115,   418,   115,   115,   115,
      13,   115,   525,    12,   115,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,   487,   446,   447,   448,   115,   115,   115,
     452,   315,   115,   115,   115,   120,   115,   115,   118,   323,
     117,   116,   506,   119,   328,   116,   330,   116,   120,   116,
     334,   335,   116,   337,   116,   339,   116,   116,   116,   116,
      13,    35,   116,   115,   119,   116,   488,   120,   490,    13,
     116,   118,   494,   117,    42,   497,   498,   499,   120,   501,
     116,   116,     3,    27,    28,   507,   116,   116,    13,    33,
      34,    35,    36,    37,    38,    39,    40,   116,   520,   115,
      44,   523,    46,    38,   118,    13,    50,    51,    52,    53,
     119,   115,   105,    13,   116,   115,   119,   115,   115,   403,
     115,    22,    13,    12,   115,   115,    70,    71,   115,    42,
       3,    75,   116,    77,   119,   115,    22,   116,   115,   115,
      29,    85,   446,   116,    33,    34,   116,   116,   115,   115,
     115,    40,    96,   437,    98,    99,    45,   116,    47,   116,
     116,   172,   386,   523,   184,    18,   115,    11,   400,    58,
      54,   115,   456,   433,    -1,    64,   357,   121,    -1,    68,
     124,    -1,    -1,    -1,    73,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    91,    92,    93,    -1,    -1,    -1,    97,    -1,
      -1,    -1,   101,    -1,   103,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,   118,
      12,   120,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      12,    -1,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      12,    -1,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      12,    -1,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      12,    -1,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      -1,    -1,    -1,    -1,    -1,    -1,    12,   119,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    12,   119,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    12,   119,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    -1,   119,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    12,   116,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    -1,    -1,    -1,    -1,    -1,    -1,    12,
     116,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    12,
     116,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    12,
     116,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    -1,    -1,    -1,    -1,    12,   116,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    -1,    -1,    -1,    -1,
      -1,    -1,    12,   116,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    12,   116,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    12,   116,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     116,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    -1,    -1,    -1,    27,    28,   116,    -1,    -1,    -1,
      33,    34,    35,    36,    37,    38,    39,    40,    -1,    -1,
      -1,    44,    -1,    46,    -1,    48,   116,    50,    51,    52,
      53,    54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    66,    -1,    -1,   116,    70,    71,    -1,
      -1,    -1,    75,    -1,    77,    -1,    -1,    -1,    81,    -1,
      -1,    -1,    85,    -1,    -1,    -1,    89,    -1,    -1,    -1,
      -1,    -1,    -1,    96,    -1,    98,    99,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   115,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   115,   116,    -1,    -1,    27,    28,   121,    -1,
      -1,   124,    33,    34,    35,    36,    37,    38,    39,    40,
      -1,    -1,    -1,    44,    -1,    46,    -1,    -1,    -1,    50,
      51,    52,    53,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    70,
      71,    -1,    33,    34,    75,    -1,    77,    -1,    -1,    40,
      -1,    -1,    -1,    -1,    85,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    55,    96,    57,    98,    99,    60,
      -1,    62,    -1,    64,    65,    -1,    67,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   115,    76,    -1,    -1,    -1,    -1,
     121,    -1,    -1,   124,    -1,    -1,    87,    88,    -1,    90,
      91,    92,    -1,    94,    95,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   107,    -1,   109,   110,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   120,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      12,    -1,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,   111,   112,   113,   114,   126,   127,    33,    34,   117,
     184,   185,   186,   192,   180,   192,   117,   181,   182,   183,
       0,    40,    55,    57,    60,    62,    64,    65,    67,    76,
      87,    88,    90,    91,    92,    94,    95,   107,   109,   110,
     120,   128,   132,   166,   176,   192,    33,    34,    57,    61,
      72,   188,   189,   186,   117,     3,   189,   183,   115,   115,
     115,   129,   192,   115,   117,   115,   115,   117,   115,   115,
     115,   117,   115,   117,   115,   119,   120,   193,     3,     4,
       5,     6,     7,     8,     9,    10,    11,   192,    13,    13,
     118,   120,   188,   177,   178,   120,   177,   192,   133,   134,
     115,    28,    41,   136,   137,   192,   167,   192,   192,   172,
     192,   192,   192,   130,   192,   131,   192,   177,   177,   177,
     177,   177,   177,   177,   177,   177,   117,   189,   189,   120,
      33,    34,    57,   118,    27,    28,    35,    36,    37,    38,
      39,    40,    44,    46,    50,    51,    52,    53,    70,    71,
      75,    77,    85,    96,    98,    99,   115,   121,   124,   179,
     192,   118,   119,   116,   116,   135,   192,   136,    33,   138,
     116,   119,   195,   118,   195,   116,   116,   119,   118,   173,
     192,     3,     3,   116,   139,   116,   184,   116,   190,   120,
     120,   192,   120,   187,   192,   179,   179,   115,   115,   115,
     115,   115,   115,   115,   115,   115,   115,   115,   115,   115,
     115,   115,   115,   115,   115,   179,   179,   179,    12,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,   120,   192,   119,
     192,   116,   115,   137,    64,   168,   192,   192,    35,   174,
     192,   177,   177,   118,   140,   166,   192,   118,   189,    72,
     117,   120,   192,   179,   192,   179,   192,   179,   179,   192,
     179,   179,   179,   192,   192,   192,   179,   179,   192,   192,
     192,   116,   179,   179,   179,   179,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   179,   179,   179,
     179,   116,   192,   136,   192,   115,   169,   119,    42,    90,
     175,   192,   116,   116,   142,   143,   120,   194,    13,   191,
     116,   116,   116,   119,   116,   119,   116,   116,   119,   116,
     119,   116,   116,   116,   119,   119,   116,   119,   116,    13,
     116,   121,   192,    13,   192,   115,   175,   120,   115,   175,
     141,    13,   115,   145,   179,   118,   189,   189,   179,   192,
     179,   179,   179,   179,   179,   179,   192,   116,    84,    85,
     122,   170,   116,   177,    35,   117,    48,    54,    66,    81,
      89,   116,   146,    42,   147,    13,   115,   120,   194,   116,
     116,   116,   116,   116,   116,   116,   116,     3,   116,   116,
     156,    13,   116,   115,    38,   148,   116,   146,   118,   177,
     175,   175,    12,    29,    40,    45,    47,    58,    64,    68,
      73,    93,    97,   101,   103,   118,   120,   157,   158,   159,
     160,   161,   165,   176,   192,    13,   179,   115,   105,   149,
      13,   116,   195,   115,   115,   192,   115,   115,    22,   151,
     152,   115,   115,   193,   116,   179,   115,   144,    13,    69,
      70,   123,   171,   177,   177,   101,   160,   192,    47,   165,
     192,   192,    42,   153,   177,    56,   100,   101,   162,   163,
     165,   116,   179,    82,    83,   102,   150,     3,   119,   116,
     115,   116,   116,   116,    22,   154,   116,   115,   115,   115,
     116,   195,   116,   177,   192,   192,     3,    13,   155,   164,
     165,   163,   163,    56,   163,   116,   177,   192,   195,   116,
     195,   116,   116,   115,   165,   164,   116
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

#define YYBACKUP(Token, Value)                                  \
do                                                              \
  if (yychar == YYEMPTY)                                        \
    {                                                           \
      yychar = (Token);                                         \
      yylval = (Value);                                         \
      YYPOPSTACK (yylen);                                       \
      yystate = *yyssp;                                         \
      goto yybackup;                                            \
    }                                                           \
  else                                                          \
    {                                                           \
      yyerror (closure, YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))

/* Error token number */
#define YYTERROR	1
#define YYERRCODE	256


/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
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
  FILE *yyo = yyoutput;
  YYUSE (yyo);
  if (!yyvaluep)
    return;
  YYUSE (closure);
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  YYUSE (yytype);
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

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (YY_NULL, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULL;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
     - Assume YYFAIL is not used.  It's too flawed to consider.  See
       <http://lists.gnu.org/archive/html/bison-patches/2009-12/msg00024.html>
       for details.  YYERROR is fine as it does not invoke this
       function.
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULL, yytname[yyx]);
                  if (! (yysize <= yysize1
                         && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
                    return 2;
                  yysize = yysize1;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    YYSIZE_T yysize1 = yysize + yystrlen (yyformat);
    if (! (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
      return 2;
    yysize = yysize1;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          yyp++;
          yyformat++;
        }
  }
  return 0;
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

  YYUSE (yytype);
}




/*----------.
| yyparse.  |
`----------*/

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


#if defined __GNUC__ && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN \
    _Pragma ("GCC diagnostic push") \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")\
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END \
    _Pragma ("GCC diagnostic pop")
#else
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
static YYSTYPE yyval_default;
# define YY_INITIAL_VALUE(Value) = Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval YY_INITIAL_VALUE(yyval_default);

    /* Number of syntax errors so far.  */
    int yynerrs;

    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       `yyss': related to states.
       `yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
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
  int yytoken = 0;
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

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
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
  if (yypact_value_is_default (yyn))
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
      if (yytable_value_is_error (yyn))
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
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

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
/* Line 1787 of yacc.c  */
#line 247 "yyscript.y"
    { script_set_common_allocation(closure, 1); }
    break;

  case 10:
/* Line 1787 of yacc.c  */
#line 249 "yyscript.y"
    { script_start_group(closure); }
    break;

  case 11:
/* Line 1787 of yacc.c  */
#line 251 "yyscript.y"
    { script_end_group(closure); }
    break;

  case 12:
/* Line 1787 of yacc.c  */
#line 253 "yyscript.y"
    { script_set_common_allocation(closure, 0); }
    break;

  case 15:
/* Line 1787 of yacc.c  */
#line 257 "yyscript.y"
    { script_parse_option(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 16:
/* Line 1787 of yacc.c  */
#line 259 "yyscript.y"
    {
	      if (!script_check_output_format(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length,
					      NULL, 0, NULL, 0))
		YYABORT;
	    }
    break;

  case 17:
/* Line 1787 of yacc.c  */
#line 265 "yyscript.y"
    {
	      if (!script_check_output_format(closure, (yyvsp[(3) - (8)].string).value, (yyvsp[(3) - (8)].string).length,
					      (yyvsp[(5) - (8)].string).value, (yyvsp[(5) - (8)].string).length,
					      (yyvsp[(7) - (8)].string).value, (yyvsp[(7) - (8)].string).length))
		YYABORT;
	    }
    break;

  case 19:
/* Line 1787 of yacc.c  */
#line 273 "yyscript.y"
    { script_add_search_dir(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 20:
/* Line 1787 of yacc.c  */
#line 275 "yyscript.y"
    { script_start_sections(closure); }
    break;

  case 21:
/* Line 1787 of yacc.c  */
#line 277 "yyscript.y"
    { script_finish_sections(closure); }
    break;

  case 22:
/* Line 1787 of yacc.c  */
#line 279 "yyscript.y"
    { script_set_target(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 23:
/* Line 1787 of yacc.c  */
#line 281 "yyscript.y"
    { script_push_lex_into_version_mode(closure); }
    break;

  case 24:
/* Line 1787 of yacc.c  */
#line 283 "yyscript.y"
    { script_pop_lex_mode(closure); }
    break;

  case 29:
/* Line 1787 of yacc.c  */
#line 302 "yyscript.y"
    { script_push_lex_into_expression_mode(closure); }
    break;

  case 30:
/* Line 1787 of yacc.c  */
#line 304 "yyscript.y"
    { script_pop_lex_mode(closure); }
    break;

  case 31:
/* Line 1787 of yacc.c  */
#line 309 "yyscript.y"
    { script_add_extern(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 32:
/* Line 1787 of yacc.c  */
#line 311 "yyscript.y"
    { script_add_extern(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 33:
/* Line 1787 of yacc.c  */
#line 313 "yyscript.y"
    { script_add_extern(closure, (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length); }
    break;

  case 36:
/* Line 1787 of yacc.c  */
#line 325 "yyscript.y"
    { script_add_file(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 37:
/* Line 1787 of yacc.c  */
#line 327 "yyscript.y"
    { script_add_library(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 38:
/* Line 1787 of yacc.c  */
#line 329 "yyscript.y"
    { script_start_as_needed(closure); }
    break;

  case 39:
/* Line 1787 of yacc.c  */
#line 331 "yyscript.y"
    { script_end_as_needed(closure); }
    break;

  case 43:
/* Line 1787 of yacc.c  */
#line 344 "yyscript.y"
    { script_start_output_section(closure, (yyvsp[(1) - (2)].string).value, (yyvsp[(1) - (2)].string).length, &(yyvsp[(2) - (2)].output_section_header)); }
    break;

  case 44:
/* Line 1787 of yacc.c  */
#line 346 "yyscript.y"
    { script_finish_output_section(closure, &(yyvsp[(7) - (7)].output_section_trailer)); }
    break;

  case 45:
/* Line 1787 of yacc.c  */
#line 352 "yyscript.y"
    { script_push_lex_into_expression_mode(closure); }
    break;

  case 46:
/* Line 1787 of yacc.c  */
#line 354 "yyscript.y"
    { script_pop_lex_mode(closure); }
    break;

  case 47:
/* Line 1787 of yacc.c  */
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
/* Line 1787 of yacc.c  */
#line 372 "yyscript.y"
    {
	      (yyval.output_section_header).address = NULL;
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 49:
/* Line 1787 of yacc.c  */
#line 377 "yyscript.y"
    {
	      (yyval.output_section_header).address = NULL;
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 50:
/* Line 1787 of yacc.c  */
#line 382 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(1) - (2)].expr);
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 51:
/* Line 1787 of yacc.c  */
#line 387 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(1) - (4)].expr);
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 52:
/* Line 1787 of yacc.c  */
#line 392 "yyscript.y"
    {
	      (yyval.output_section_header).address = NULL;
	      (yyval.output_section_header).section_type = (yyvsp[(2) - (4)].section_type);
	    }
    break;

  case 53:
/* Line 1787 of yacc.c  */
#line 397 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(1) - (5)].expr);
	      (yyval.output_section_header).section_type = (yyvsp[(3) - (5)].section_type);
	    }
    break;

  case 54:
/* Line 1787 of yacc.c  */
#line 406 "yyscript.y"
    { (yyval.section_type) = SCRIPT_SECTION_TYPE_NOLOAD; }
    break;

  case 55:
/* Line 1787 of yacc.c  */
#line 408 "yyscript.y"
    {
	      yyerror(closure, "DSECT section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_DSECT;
	    }
    break;

  case 56:
/* Line 1787 of yacc.c  */
#line 413 "yyscript.y"
    {
	      yyerror(closure, "COPY section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_COPY;
	    }
    break;

  case 57:
/* Line 1787 of yacc.c  */
#line 418 "yyscript.y"
    {
	      yyerror(closure, "INFO section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_INFO;
	    }
    break;

  case 58:
/* Line 1787 of yacc.c  */
#line 423 "yyscript.y"
    {
	      yyerror(closure, "OVERLAY section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_OVERLAY;
	    }
    break;

  case 59:
/* Line 1787 of yacc.c  */
#line 432 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 60:
/* Line 1787 of yacc.c  */
#line 434 "yyscript.y"
    { (yyval.expr) = (yyvsp[(3) - (4)].expr); }
    break;

  case 61:
/* Line 1787 of yacc.c  */
#line 440 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 62:
/* Line 1787 of yacc.c  */
#line 442 "yyscript.y"
    { (yyval.expr) = (yyvsp[(3) - (4)].expr); }
    break;

  case 63:
/* Line 1787 of yacc.c  */
#line 448 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 64:
/* Line 1787 of yacc.c  */
#line 450 "yyscript.y"
    { (yyval.expr) = (yyvsp[(3) - (4)].expr); }
    break;

  case 65:
/* Line 1787 of yacc.c  */
#line 456 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_NONE; }
    break;

  case 66:
/* Line 1787 of yacc.c  */
#line 458 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_ONLY_IF_RO; }
    break;

  case 67:
/* Line 1787 of yacc.c  */
#line 460 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_ONLY_IF_RW; }
    break;

  case 68:
/* Line 1787 of yacc.c  */
#line 462 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_SPECIAL; }
    break;

  case 69:
/* Line 1787 of yacc.c  */
#line 468 "yyscript.y"
    {
	      (yyval.output_section_trailer).fill = (yyvsp[(4) - (5)].expr);
	      (yyval.output_section_trailer).phdrs = (yyvsp[(3) - (5)].string_list);
	    }
    break;

  case 70:
/* Line 1787 of yacc.c  */
#line 477 "yyscript.y"
    { script_set_section_region(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length, 1); }
    break;

  case 72:
/* Line 1787 of yacc.c  */
#line 484 "yyscript.y"
    { script_set_section_region(closure, (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length, 0); }
    break;

  case 74:
/* Line 1787 of yacc.c  */
#line 491 "yyscript.y"
    { (yyval.string_list) = script_string_list_push_back((yyvsp[(1) - (3)].string_list), (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length); }
    break;

  case 75:
/* Line 1787 of yacc.c  */
#line 493 "yyscript.y"
    { (yyval.string_list) = NULL; }
    break;

  case 76:
/* Line 1787 of yacc.c  */
#line 500 "yyscript.y"
    { (yyval.expr) = (yyvsp[(2) - (2)].expr); }
    break;

  case 77:
/* Line 1787 of yacc.c  */
#line 502 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 82:
/* Line 1787 of yacc.c  */
#line 518 "yyscript.y"
    { script_add_data(closure, (yyvsp[(1) - (4)].integer), (yyvsp[(3) - (4)].expr)); }
    break;

  case 83:
/* Line 1787 of yacc.c  */
#line 520 "yyscript.y"
    { script_add_assertion(closure, (yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].string).value, (yyvsp[(5) - (6)].string).length); }
    break;

  case 84:
/* Line 1787 of yacc.c  */
#line 522 "yyscript.y"
    { script_add_fill(closure, (yyvsp[(3) - (4)].expr)); }
    break;

  case 85:
/* Line 1787 of yacc.c  */
#line 524 "yyscript.y"
    {
	      /* The GNU linker uses CONSTRUCTORS for the a.out object
		 file format.  It does nothing when using ELF.  Since
		 some ELF linker scripts use it although it does
		 nothing, we accept it and ignore it.  */
	    }
    break;

  case 87:
/* Line 1787 of yacc.c  */
#line 532 "yyscript.y"
    { script_include_directive(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 89:
/* Line 1787 of yacc.c  */
#line 540 "yyscript.y"
    { (yyval.integer) = QUAD; }
    break;

  case 90:
/* Line 1787 of yacc.c  */
#line 542 "yyscript.y"
    { (yyval.integer) = SQUAD; }
    break;

  case 91:
/* Line 1787 of yacc.c  */
#line 544 "yyscript.y"
    { (yyval.integer) = LONG; }
    break;

  case 92:
/* Line 1787 of yacc.c  */
#line 546 "yyscript.y"
    { (yyval.integer) = SHORT; }
    break;

  case 93:
/* Line 1787 of yacc.c  */
#line 548 "yyscript.y"
    { (yyval.integer) = BYTE; }
    break;

  case 94:
/* Line 1787 of yacc.c  */
#line 555 "yyscript.y"
    { script_add_input_section(closure, &(yyvsp[(1) - (1)].input_section_spec), 0); }
    break;

  case 95:
/* Line 1787 of yacc.c  */
#line 557 "yyscript.y"
    { script_add_input_section(closure, &(yyvsp[(3) - (4)].input_section_spec), 1); }
    break;

  case 96:
/* Line 1787 of yacc.c  */
#line 563 "yyscript.y"
    {
	      (yyval.input_section_spec).file.name = (yyvsp[(1) - (1)].string);
	      (yyval.input_section_spec).file.sort = SORT_WILDCARD_NONE;
	      (yyval.input_section_spec).input_sections.sections = NULL;
	      (yyval.input_section_spec).input_sections.exclude = NULL;
	    }
    break;

  case 97:
/* Line 1787 of yacc.c  */
#line 570 "yyscript.y"
    {
	      (yyval.input_section_spec).file = (yyvsp[(1) - (4)].wildcard_section);
	      (yyval.input_section_spec).input_sections = (yyvsp[(3) - (4)].wildcard_sections);
	    }
    break;

  case 98:
/* Line 1787 of yacc.c  */
#line 579 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(1) - (1)].string);
	      (yyval.wildcard_section).sort = SORT_WILDCARD_NONE;
	    }
    break;

  case 99:
/* Line 1787 of yacc.c  */
#line 584 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(3) - (4)].string);
	      (yyval.wildcard_section).sort = SORT_WILDCARD_BY_NAME;
	    }
    break;

  case 100:
/* Line 1787 of yacc.c  */
#line 593 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = script_string_sort_list_add((yyvsp[(1) - (3)].wildcard_sections).sections, &(yyvsp[(3) - (3)].wildcard_section));
	      (yyval.wildcard_sections).exclude = (yyvsp[(1) - (3)].wildcard_sections).exclude;
	    }
    break;

  case 101:
/* Line 1787 of yacc.c  */
#line 598 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = script_new_string_sort_list(&(yyvsp[(1) - (1)].wildcard_section));
	      (yyval.wildcard_sections).exclude = NULL;
	    }
    break;

  case 102:
/* Line 1787 of yacc.c  */
#line 603 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = (yyvsp[(1) - (6)].wildcard_sections).sections;
	      (yyval.wildcard_sections).exclude = script_string_list_append((yyvsp[(1) - (6)].wildcard_sections).exclude, (yyvsp[(5) - (6)].string_list));
	    }
    break;

  case 103:
/* Line 1787 of yacc.c  */
#line 608 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = NULL;
	      (yyval.wildcard_sections).exclude = (yyvsp[(3) - (4)].string_list);
	    }
    break;

  case 104:
/* Line 1787 of yacc.c  */
#line 617 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(1) - (1)].string);
	      (yyval.wildcard_section).sort = SORT_WILDCARD_NONE;
	    }
    break;

  case 105:
/* Line 1787 of yacc.c  */
#line 622 "yyscript.y"
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

  case 106:
/* Line 1787 of yacc.c  */
#line 641 "yyscript.y"
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

  case 107:
/* Line 1787 of yacc.c  */
#line 664 "yyscript.y"
    { (yyval.string_list) = script_string_list_push_back((yyvsp[(1) - (3)].string_list), (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length); }
    break;

  case 108:
/* Line 1787 of yacc.c  */
#line 666 "yyscript.y"
    { (yyval.string_list) = script_new_string_list((yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 109:
/* Line 1787 of yacc.c  */
#line 673 "yyscript.y"
    { (yyval.string) = (yyvsp[(1) - (1)].string); }
    break;

  case 110:
/* Line 1787 of yacc.c  */
#line 675 "yyscript.y"
    {
	      (yyval.string).value = "*";
	      (yyval.string).length = 1;
	    }
    break;

  case 111:
/* Line 1787 of yacc.c  */
#line 680 "yyscript.y"
    {
	      (yyval.string).value = "?";
	      (yyval.string).length = 1;
	    }
    break;

  case 112:
/* Line 1787 of yacc.c  */
#line 690 "yyscript.y"
    { script_set_entry(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 114:
/* Line 1787 of yacc.c  */
#line 693 "yyscript.y"
    { script_add_assertion(closure, (yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].string).value, (yyvsp[(5) - (6)].string).length); }
    break;

  case 115:
/* Line 1787 of yacc.c  */
#line 695 "yyscript.y"
    { script_include_directive(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 118:
/* Line 1787 of yacc.c  */
#line 707 "yyscript.y"
    { script_add_memory(closure, (yyvsp[(1) - (10)].string).value, (yyvsp[(1) - (10)].string).length, (yyvsp[(2) - (10)].integer), (yyvsp[(6) - (10)].expr), (yyvsp[(10) - (10)].expr)); }
    break;

  case 119:
/* Line 1787 of yacc.c  */
#line 711 "yyscript.y"
    { script_include_directive(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 121:
/* Line 1787 of yacc.c  */
#line 718 "yyscript.y"
    { (yyval.integer) = script_parse_memory_attr(closure, (yyvsp[(2) - (3)].string).value, (yyvsp[(2) - (3)].string).length, 0); }
    break;

  case 122:
/* Line 1787 of yacc.c  */
#line 721 "yyscript.y"
    { (yyval.integer) = script_parse_memory_attr(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length, 1); }
    break;

  case 123:
/* Line 1787 of yacc.c  */
#line 723 "yyscript.y"
    { (yyval.integer) = 0; }
    break;

  case 132:
/* Line 1787 of yacc.c  */
#line 751 "yyscript.y"
    { script_add_phdr(closure, (yyvsp[(1) - (4)].string).value, (yyvsp[(1) - (4)].string).length, (yyvsp[(2) - (4)].integer), &(yyvsp[(3) - (4)].phdr_info)); }
    break;

  case 133:
/* Line 1787 of yacc.c  */
#line 760 "yyscript.y"
    { (yyval.integer) = script_phdr_string_to_type(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 134:
/* Line 1787 of yacc.c  */
#line 762 "yyscript.y"
    { (yyval.integer) = (yyvsp[(1) - (1)].integer); }
    break;

  case 135:
/* Line 1787 of yacc.c  */
#line 768 "yyscript.y"
    { memset(&(yyval.phdr_info), 0, sizeof(struct Phdr_info)); }
    break;

  case 136:
/* Line 1787 of yacc.c  */
#line 770 "yyscript.y"
    {
	      (yyval.phdr_info) = (yyvsp[(2) - (2)].phdr_info);
	      if ((yyvsp[(1) - (2)].string).length == 7 && strncmp((yyvsp[(1) - (2)].string).value, "FILEHDR", 7) == 0)
		(yyval.phdr_info).includes_filehdr = 1;
	      else
		yyerror(closure, "PHDRS syntax error");
	    }
    break;

  case 137:
/* Line 1787 of yacc.c  */
#line 778 "yyscript.y"
    {
	      (yyval.phdr_info) = (yyvsp[(2) - (2)].phdr_info);
	      (yyval.phdr_info).includes_phdrs = 1;
	    }
    break;

  case 138:
/* Line 1787 of yacc.c  */
#line 783 "yyscript.y"
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

  case 139:
/* Line 1787 of yacc.c  */
#line 794 "yyscript.y"
    {
	      (yyval.phdr_info) = (yyvsp[(5) - (5)].phdr_info);
	      (yyval.phdr_info).load_address = (yyvsp[(3) - (5)].expr);
	    }
    break;

  case 140:
/* Line 1787 of yacc.c  */
#line 803 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, (yyvsp[(3) - (3)].expr), 0, 0); }
    break;

  case 141:
/* Line 1787 of yacc.c  */
#line 805 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_add(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 142:
/* Line 1787 of yacc.c  */
#line 811 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_sub(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 143:
/* Line 1787 of yacc.c  */
#line 817 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_mult(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 144:
/* Line 1787 of yacc.c  */
#line 823 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_div(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 145:
/* Line 1787 of yacc.c  */
#line 829 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_lshift(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 146:
/* Line 1787 of yacc.c  */
#line 835 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_rshift(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 147:
/* Line 1787 of yacc.c  */
#line 841 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_bitwise_and(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 148:
/* Line 1787 of yacc.c  */
#line 847 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_bitwise_or(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 149:
/* Line 1787 of yacc.c  */
#line 853 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(3) - (6)].string).value, (yyvsp[(3) - (6)].string).length, (yyvsp[(5) - (6)].expr), 1, 0); }
    break;

  case 150:
/* Line 1787 of yacc.c  */
#line 855 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(3) - (6)].string).value, (yyvsp[(3) - (6)].string).length, (yyvsp[(5) - (6)].expr), 1, 1); }
    break;

  case 151:
/* Line 1787 of yacc.c  */
#line 860 "yyscript.y"
    { script_push_lex_into_expression_mode(closure); }
    break;

  case 152:
/* Line 1787 of yacc.c  */
#line 862 "yyscript.y"
    {
	      script_pop_lex_mode(closure);
	      (yyval.expr) = (yyvsp[(2) - (2)].expr);
	    }
    break;

  case 153:
/* Line 1787 of yacc.c  */
#line 871 "yyscript.y"
    { (yyval.expr) = (yyvsp[(2) - (3)].expr); }
    break;

  case 154:
/* Line 1787 of yacc.c  */
#line 873 "yyscript.y"
    { (yyval.expr) = script_exp_unary_minus((yyvsp[(2) - (2)].expr)); }
    break;

  case 155:
/* Line 1787 of yacc.c  */
#line 875 "yyscript.y"
    { (yyval.expr) = script_exp_unary_logical_not((yyvsp[(2) - (2)].expr)); }
    break;

  case 156:
/* Line 1787 of yacc.c  */
#line 877 "yyscript.y"
    { (yyval.expr) = script_exp_unary_bitwise_not((yyvsp[(2) - (2)].expr)); }
    break;

  case 157:
/* Line 1787 of yacc.c  */
#line 879 "yyscript.y"
    { (yyval.expr) = (yyvsp[(2) - (2)].expr); }
    break;

  case 158:
/* Line 1787 of yacc.c  */
#line 881 "yyscript.y"
    { (yyval.expr) = script_exp_binary_mult((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 159:
/* Line 1787 of yacc.c  */
#line 883 "yyscript.y"
    { (yyval.expr) = script_exp_binary_div((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 160:
/* Line 1787 of yacc.c  */
#line 885 "yyscript.y"
    { (yyval.expr) = script_exp_binary_mod((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 161:
/* Line 1787 of yacc.c  */
#line 887 "yyscript.y"
    { (yyval.expr) = script_exp_binary_add((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 162:
/* Line 1787 of yacc.c  */
#line 889 "yyscript.y"
    { (yyval.expr) = script_exp_binary_sub((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 163:
/* Line 1787 of yacc.c  */
#line 891 "yyscript.y"
    { (yyval.expr) = script_exp_binary_lshift((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 164:
/* Line 1787 of yacc.c  */
#line 893 "yyscript.y"
    { (yyval.expr) = script_exp_binary_rshift((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 165:
/* Line 1787 of yacc.c  */
#line 895 "yyscript.y"
    { (yyval.expr) = script_exp_binary_eq((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 166:
/* Line 1787 of yacc.c  */
#line 897 "yyscript.y"
    { (yyval.expr) = script_exp_binary_ne((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 167:
/* Line 1787 of yacc.c  */
#line 899 "yyscript.y"
    { (yyval.expr) = script_exp_binary_le((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 168:
/* Line 1787 of yacc.c  */
#line 901 "yyscript.y"
    { (yyval.expr) = script_exp_binary_ge((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 169:
/* Line 1787 of yacc.c  */
#line 903 "yyscript.y"
    { (yyval.expr) = script_exp_binary_lt((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 170:
/* Line 1787 of yacc.c  */
#line 905 "yyscript.y"
    { (yyval.expr) = script_exp_binary_gt((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 171:
/* Line 1787 of yacc.c  */
#line 907 "yyscript.y"
    { (yyval.expr) = script_exp_binary_bitwise_and((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 172:
/* Line 1787 of yacc.c  */
#line 909 "yyscript.y"
    { (yyval.expr) = script_exp_binary_bitwise_xor((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 173:
/* Line 1787 of yacc.c  */
#line 911 "yyscript.y"
    { (yyval.expr) = script_exp_binary_bitwise_or((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 174:
/* Line 1787 of yacc.c  */
#line 913 "yyscript.y"
    { (yyval.expr) = script_exp_binary_logical_and((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 175:
/* Line 1787 of yacc.c  */
#line 915 "yyscript.y"
    { (yyval.expr) = script_exp_binary_logical_or((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 176:
/* Line 1787 of yacc.c  */
#line 917 "yyscript.y"
    { (yyval.expr) = script_exp_trinary_cond((yyvsp[(1) - (5)].expr), (yyvsp[(3) - (5)].expr), (yyvsp[(5) - (5)].expr)); }
    break;

  case 177:
/* Line 1787 of yacc.c  */
#line 919 "yyscript.y"
    { (yyval.expr) = script_exp_integer((yyvsp[(1) - (1)].integer)); }
    break;

  case 178:
/* Line 1787 of yacc.c  */
#line 921 "yyscript.y"
    { (yyval.expr) = script_symbol(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 179:
/* Line 1787 of yacc.c  */
#line 923 "yyscript.y"
    { (yyval.expr) = script_exp_function_max((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr)); }
    break;

  case 180:
/* Line 1787 of yacc.c  */
#line 925 "yyscript.y"
    { (yyval.expr) = script_exp_function_min((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr)); }
    break;

  case 181:
/* Line 1787 of yacc.c  */
#line 927 "yyscript.y"
    { (yyval.expr) = script_exp_function_defined((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 182:
/* Line 1787 of yacc.c  */
#line 929 "yyscript.y"
    { (yyval.expr) = script_exp_function_sizeof_headers(); }
    break;

  case 183:
/* Line 1787 of yacc.c  */
#line 931 "yyscript.y"
    { (yyval.expr) = script_exp_function_alignof((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 184:
/* Line 1787 of yacc.c  */
#line 933 "yyscript.y"
    { (yyval.expr) = script_exp_function_sizeof((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 185:
/* Line 1787 of yacc.c  */
#line 935 "yyscript.y"
    { (yyval.expr) = script_exp_function_addr((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 186:
/* Line 1787 of yacc.c  */
#line 937 "yyscript.y"
    { (yyval.expr) = script_exp_function_loadaddr((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 187:
/* Line 1787 of yacc.c  */
#line 939 "yyscript.y"
    { (yyval.expr) = script_exp_function_origin(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 188:
/* Line 1787 of yacc.c  */
#line 941 "yyscript.y"
    { (yyval.expr) = script_exp_function_length(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 189:
/* Line 1787 of yacc.c  */
#line 943 "yyscript.y"
    { (yyval.expr) = script_exp_function_constant((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 190:
/* Line 1787 of yacc.c  */
#line 945 "yyscript.y"
    { (yyval.expr) = script_exp_function_absolute((yyvsp[(3) - (4)].expr)); }
    break;

  case 191:
/* Line 1787 of yacc.c  */
#line 947 "yyscript.y"
    { (yyval.expr) = script_exp_function_align(script_exp_string(".", 1), (yyvsp[(3) - (4)].expr)); }
    break;

  case 192:
/* Line 1787 of yacc.c  */
#line 949 "yyscript.y"
    { (yyval.expr) = script_exp_function_align((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr)); }
    break;

  case 193:
/* Line 1787 of yacc.c  */
#line 951 "yyscript.y"
    { (yyval.expr) = script_exp_function_align(script_exp_string(".", 1), (yyvsp[(3) - (4)].expr)); }
    break;

  case 194:
/* Line 1787 of yacc.c  */
#line 953 "yyscript.y"
    {
	      script_data_segment_align(closure);
	      (yyval.expr) = script_exp_function_data_segment_align((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr));
	    }
    break;

  case 195:
/* Line 1787 of yacc.c  */
#line 958 "yyscript.y"
    {
	      script_data_segment_relro_end(closure);
	      (yyval.expr) = script_exp_function_data_segment_relro_end((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr));
	    }
    break;

  case 196:
/* Line 1787 of yacc.c  */
#line 963 "yyscript.y"
    { (yyval.expr) = script_exp_function_data_segment_end((yyvsp[(3) - (4)].expr)); }
    break;

  case 197:
/* Line 1787 of yacc.c  */
#line 965 "yyscript.y"
    {
	      (yyval.expr) = script_exp_function_segment_start((yyvsp[(3) - (6)].string).value, (yyvsp[(3) - (6)].string).length, (yyvsp[(5) - (6)].expr));
	      /* We need to take note of any SEGMENT_START expressions
		 because they change the behaviour of -Ttext, -Tdata and
		 -Tbss options.  */
	      script_saw_segment_start_expression(closure);
	    }
    break;

  case 198:
/* Line 1787 of yacc.c  */
#line 973 "yyscript.y"
    { (yyval.expr) = script_exp_function_assert((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].string).value, (yyvsp[(5) - (6)].string).length); }
    break;

  case 199:
/* Line 1787 of yacc.c  */
#line 979 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, (yyvsp[(3) - (3)].expr), 0, 0); }
    break;

  case 203:
/* Line 1787 of yacc.c  */
#line 997 "yyscript.y"
    { script_new_vers_node (closure, NULL, (yyvsp[(2) - (5)].versyms)); }
    break;

  case 207:
/* Line 1787 of yacc.c  */
#line 1012 "yyscript.y"
    {
	      script_register_vers_node (closure, NULL, 0, (yyvsp[(2) - (4)].versnode), NULL);
	    }
    break;

  case 208:
/* Line 1787 of yacc.c  */
#line 1016 "yyscript.y"
    {
	      script_register_vers_node (closure, (yyvsp[(1) - (5)].string).value, (yyvsp[(1) - (5)].string).length, (yyvsp[(3) - (5)].versnode),
					 NULL);
	    }
    break;

  case 209:
/* Line 1787 of yacc.c  */
#line 1021 "yyscript.y"
    {
	      script_register_vers_node (closure, (yyvsp[(1) - (6)].string).value, (yyvsp[(1) - (6)].string).length, (yyvsp[(3) - (6)].versnode), (yyvsp[(5) - (6)].deplist));
	    }
    break;

  case 210:
/* Line 1787 of yacc.c  */
#line 1028 "yyscript.y"
    {
	      (yyval.deplist) = script_add_vers_depend (closure, NULL, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length);
	    }
    break;

  case 211:
/* Line 1787 of yacc.c  */
#line 1032 "yyscript.y"
    {
	      (yyval.deplist) = script_add_vers_depend (closure, (yyvsp[(1) - (2)].deplist), (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length);
	    }
    break;

  case 212:
/* Line 1787 of yacc.c  */
#line 1039 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, NULL, NULL); }
    break;

  case 213:
/* Line 1787 of yacc.c  */
#line 1041 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, (yyvsp[(1) - (2)].versyms), NULL); }
    break;

  case 214:
/* Line 1787 of yacc.c  */
#line 1043 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, (yyvsp[(3) - (4)].versyms), NULL); }
    break;

  case 215:
/* Line 1787 of yacc.c  */
#line 1045 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, NULL, (yyvsp[(3) - (4)].versyms)); }
    break;

  case 216:
/* Line 1787 of yacc.c  */
#line 1047 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, (yyvsp[(3) - (8)].versyms), (yyvsp[(7) - (8)].versyms)); }
    break;

  case 217:
/* Line 1787 of yacc.c  */
#line 1056 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, NULL, (yyvsp[(1) - (1)].string).value,
					    (yyvsp[(1) - (1)].string).length, 0);
	    }
    break;

  case 218:
/* Line 1787 of yacc.c  */
#line 1061 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, NULL, (yyvsp[(1) - (1)].string).value,
					    (yyvsp[(1) - (1)].string).length, 1);
	    }
    break;

  case 219:
/* Line 1787 of yacc.c  */
#line 1066 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, (yyvsp[(1) - (3)].versyms), (yyvsp[(3) - (3)].string).value,
                                            (yyvsp[(3) - (3)].string).length, 0);
	    }
    break;

  case 220:
/* Line 1787 of yacc.c  */
#line 1071 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, (yyvsp[(1) - (3)].versyms), (yyvsp[(3) - (3)].string).value,
                                            (yyvsp[(3) - (3)].string).length, 1);
	    }
    break;

  case 221:
/* Line 1787 of yacc.c  */
#line 1077 "yyscript.y"
    { version_script_push_lang (closure, (yyvsp[(2) - (3)].string).value, (yyvsp[(2) - (3)].string).length); }
    break;

  case 222:
/* Line 1787 of yacc.c  */
#line 1079 "yyscript.y"
    {
	      (yyval.versyms) = (yyvsp[(5) - (7)].versyms);
	      version_script_pop_lang(closure);
	    }
    break;

  case 223:
/* Line 1787 of yacc.c  */
#line 1087 "yyscript.y"
    { version_script_push_lang (closure, (yyvsp[(4) - (5)].string).value, (yyvsp[(4) - (5)].string).length); }
    break;

  case 224:
/* Line 1787 of yacc.c  */
#line 1089 "yyscript.y"
    {
	      (yyval.versyms) = script_merge_expressions ((yyvsp[(1) - (9)].versyms), (yyvsp[(7) - (9)].versyms));
	      version_script_pop_lang(closure);
	    }
    break;

  case 225:
/* Line 1787 of yacc.c  */
#line 1094 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, NULL, "extern",
					    sizeof("extern") - 1, 1);
	    }
    break;

  case 226:
/* Line 1787 of yacc.c  */
#line 1099 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, (yyvsp[(1) - (3)].versyms), "extern",
					    sizeof("extern") - 1, 1);
	    }
    break;

  case 227:
/* Line 1787 of yacc.c  */
#line 1109 "yyscript.y"
    { (yyval.string) = (yyvsp[(1) - (1)].string); }
    break;

  case 228:
/* Line 1787 of yacc.c  */
#line 1111 "yyscript.y"
    { (yyval.string) = (yyvsp[(1) - (1)].string); }
    break;


/* Line 1787 of yacc.c  */
#line 3562 "yyscript.c"
      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
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
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (closure, YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (closure, yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
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
      if (!yypact_value_is_default (yyn))
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

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


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

#if !defined yyoverflow || YYERROR_VERBOSE
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
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, closure);
    }
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


/* Line 2050 of yacc.c  */
#line 1133 "yyscript.y"

