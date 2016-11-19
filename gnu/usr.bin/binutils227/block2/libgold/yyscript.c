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
#ifndef YY_YY_YYSCRIPT_H_INCLUDED
# define YY_YY_YYSCRIPT_H_INCLUDED
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
     SORT_BY_INIT_PRIORITY = 343,
     SORT_BY_NAME = 344,
     SPECIAL = 345,
     SQUAD = 346,
     STARTUP = 347,
     SUBALIGN = 348,
     SYSLIB = 349,
     TARGET_K = 350,
     TRUNCATE = 351,
     VERSIONK = 352,
     OPTION = 353,
     PARSING_LINKER_SCRIPT = 354,
     PARSING_VERSION_SCRIPT = 355,
     PARSING_DEFSYM = 356,
     PARSING_DYNAMIC_LIST = 357,
     PARSING_SECTIONS_BLOCK = 358,
     PARSING_SECTION_COMMANDS = 359,
     PARSING_MEMORY_DEF = 360
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
#define SORT_BY_INIT_PRIORITY 343
#define SORT_BY_NAME 344
#define SPECIAL 345
#define SQUAD 346
#define STARTUP 347
#define SUBALIGN 348
#define SYSLIB 349
#define TARGET_K 350
#define TRUNCATE 351
#define VERSIONK 352
#define OPTION 353
#define PARSING_LINKER_SCRIPT 354
#define PARSING_VERSION_SCRIPT 355
#define PARSING_DEFSYM 356
#define PARSING_DYNAMIC_LIST 357
#define PARSING_SECTIONS_BLOCK 358
#define PARSING_SECTION_COMMANDS 359
#define PARSING_MEMORY_DEF 360



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
#line 365 "yyscript.c"
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

#endif /* !YY_YY_YYSCRIPT_H_INCLUDED  */

/* Copy the second part of user declarations.  */

/* Line 390 of yacc.c  */
#line 392 "yyscript.c"

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
#define YYFINAL  26
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   1464

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  129
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  70
/* YYNRULES -- Number of rules.  */
#define YYNRULES  240
/* YYNRULES -- Number of states.  */
#define YYNSTATES  549

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   360

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   125,     2,     2,     2,    31,    18,     2,
     119,   120,    29,    27,   123,    28,     2,    30,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    13,   124,
      21,     3,    22,    12,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,    17,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,   127,     2,
       2,   126,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   121,    16,   122,   128,     2,     2,     2,
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
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     6,     9,    12,    15,    18,    21,    24,
      27,    28,    33,    35,    36,    42,    44,    49,    54,    59,
      64,    73,    78,    83,    84,    90,    95,    96,   102,   107,
     110,   117,   120,   122,   124,   129,   130,   133,   135,   138,
     142,   144,   148,   150,   153,   154,   160,   163,   164,   169,
     172,   179,   182,   183,   191,   192,   193,   201,   203,   207,
     210,   215,   220,   226,   228,   230,   232,   234,   236,   237,
     242,   243,   248,   249,   254,   255,   257,   259,   261,   267,
     270,   271,   275,   276,   280,   281,   284,   285,   286,   289,
     292,   294,   299,   306,   311,   313,   318,   321,   323,   325,
     327,   329,   331,   333,   335,   340,   342,   347,   349,   354,
     358,   360,   367,   372,   374,   379,   384,   389,   393,   395,
     397,   399,   401,   405,   406,   417,   420,   421,   425,   430,
     431,   433,   435,   437,   439,   441,   443,   446,   447,   452,
     454,   456,   457,   460,   463,   469,   475,   479,   483,   487,
     491,   495,   499,   503,   507,   511,   518,   525,   526,   529,
     533,   536,   539,   542,   545,   549,   553,   557,   561,   565,
     569,   573,   577,   581,   585,   589,   593,   597,   601,   605,
     609,   613,   617,   623,   625,   627,   634,   641,   646,   648,
     653,   658,   663,   668,   673,   678,   683,   688,   693,   700,
     705,   712,   719,   724,   731,   738,   742,   744,   746,   749,
     755,   757,   759,   762,   767,   773,   780,   782,   785,   786,
     789,   794,   799,   808,   810,   812,   816,   820,   821,   829,
     830,   840,   842,   846,   848,   850,   852,   854,   856,   857,
     859
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
     130,     0,    -1,   112,   131,    -1,   113,   187,    -1,   114,
     183,    -1,   115,   184,    -1,   116,   143,    -1,   117,   160,
      -1,   118,   170,    -1,   131,   132,    -1,    -1,    57,   119,
     137,   120,    -1,    60,    -1,    -1,    62,   133,   119,   140,
     120,    -1,    65,    -1,    67,   119,   140,   120,    -1,    76,
     121,   170,   122,    -1,   111,   119,   195,   120,    -1,    88,
     119,   195,   120,    -1,    88,   119,   195,   123,   195,   123,
     195,   120,    -1,    90,   121,   175,   122,    -1,    94,   119,
     195,   120,    -1,    -1,    95,   121,   134,   143,   122,    -1,
     108,   119,   195,   120,    -1,    -1,   110,   121,   135,   187,
     122,    -1,    55,   119,   195,   120,    -1,   179,   196,    -1,
      40,   119,   180,   123,   195,   120,    -1,    64,   195,    -1,
     136,    -1,   124,    -1,    87,   119,   195,   120,    -1,    -1,
     138,   139,    -1,   195,    -1,   139,   195,    -1,   139,   123,
     195,    -1,   141,    -1,   140,   198,   141,    -1,   195,    -1,
      28,    33,    -1,    -1,    41,   142,   119,   140,   120,    -1,
     143,   144,    -1,    -1,    55,   119,   195,   120,    -1,   179,
     196,    -1,    40,   119,   180,   123,   195,   120,    -1,    64,
     195,    -1,    -1,   195,   146,   145,   121,   160,   122,   155,
      -1,    -1,    -1,   147,   149,   151,   152,   153,   148,   154,
      -1,    13,    -1,   119,   120,    13,    -1,   182,    13,    -1,
     182,   119,   120,    13,    -1,   119,   150,   120,    13,    -1,
     182,   119,   150,   120,    13,    -1,    81,    -1,    54,    -1,
      48,    -1,    66,    -1,    89,    -1,    -1,    42,   119,   182,
     120,    -1,    -1,    38,   119,   182,   120,    -1,    -1,   106,
     119,   182,   120,    -1,    -1,    82,    -1,    83,    -1,   103,
      -1,   156,   157,   158,   159,   198,    -1,    22,   195,    -1,
      -1,    42,    22,   195,    -1,    -1,   158,    13,   195,    -1,
      -1,     3,   180,    -1,    -1,    -1,   160,   161,    -1,   179,
     196,    -1,   163,    -1,   162,   119,   180,   120,    -1,    40,
     119,   180,   123,   195,   120,    -1,    58,   119,   180,   120,
      -1,    47,    -1,   102,   119,    47,   120,    -1,    64,   195,
      -1,   124,    -1,    93,    -1,   104,    -1,    73,    -1,    97,
      -1,    45,    -1,   164,    -1,    68,   119,   164,   120,    -1,
     195,    -1,   165,   119,   166,   120,    -1,   169,    -1,   102,
     119,   169,   120,    -1,   166,   198,   167,    -1,   167,    -1,
     166,   198,    56,   119,   168,   120,    -1,    56,   119,   168,
     120,    -1,   169,    -1,   102,   119,   167,   120,    -1,   100,
     119,   167,   120,    -1,   101,   119,   169,   120,    -1,   168,
     198,   169,    -1,   169,    -1,   195,    -1,    29,    -1,    12,
      -1,   170,   198,   171,    -1,    -1,   195,   172,    13,   173,
       3,   180,   198,   174,     3,   180,    -1,    64,   195,    -1,
      -1,   119,   195,   120,    -1,   119,   125,   195,   120,    -1,
      -1,    85,    -1,    84,    -1,   126,    -1,    70,    -1,    69,
      -1,   127,    -1,   175,   176,    -1,    -1,   195,   177,   178,
     124,    -1,   195,    -1,    35,    -1,    -1,   195,   178,    -1,
      90,   178,    -1,   195,   119,    35,   120,   178,    -1,    42,
     119,   180,   120,   178,    -1,   195,     3,   180,    -1,   195,
      11,   180,    -1,   195,    10,   180,    -1,   195,     9,   180,
      -1,   195,     8,   180,    -1,   195,     7,   180,    -1,   195,
       6,   180,    -1,   195,     5,   180,    -1,   195,     4,   180,
      -1,    91,   119,   195,     3,   180,   120,    -1,    92,   119,
     195,     3,   180,   120,    -1,    -1,   181,   182,    -1,   119,
     182,   120,    -1,    28,   182,    -1,   125,   182,    -1,   128,
     182,    -1,    27,   182,    -1,   182,    29,   182,    -1,   182,
      30,   182,    -1,   182,    31,   182,    -1,   182,    27,   182,
      -1,   182,    28,   182,    -1,   182,    26,   182,    -1,   182,
      25,   182,    -1,   182,    20,   182,    -1,   182,    19,   182,
      -1,   182,    24,   182,    -1,   182,    23,   182,    -1,   182,
      21,   182,    -1,   182,    22,   182,    -1,   182,    18,   182,
      -1,   182,    17,   182,    -1,   182,    16,   182,    -1,   182,
      15,   182,    -1,   182,    14,   182,    -1,   182,    12,   182,
      13,   182,    -1,    35,    -1,   195,    -1,    75,   119,   182,
     123,   182,   120,    -1,    77,   119,   182,   123,   182,   120,
      -1,    53,   119,   195,   120,    -1,    99,    -1,    39,   119,
     195,   120,    -1,    98,   119,   195,   120,    -1,    37,   119,
     195,   120,    -1,    71,   119,   195,   120,    -1,    85,   119,
     195,   120,    -1,    70,   119,   195,   120,    -1,    46,   119,
     195,   120,    -1,    36,   119,   182,   120,    -1,    38,   119,
     182,   120,    -1,    38,   119,   182,   123,   182,   120,    -1,
      44,   119,   182,   120,    -1,    50,   119,   182,   123,   182,
     120,    -1,    52,   119,   182,   123,   182,   120,    -1,    51,
     119,   182,   120,    -1,    96,   119,   195,   123,   182,   120,
      -1,    40,   119,   182,   123,   195,   120,    -1,   195,     3,
     180,    -1,   185,    -1,   186,    -1,   185,   186,    -1,   121,
     192,   124,   122,   124,    -1,   188,    -1,   189,    -1,   188,
     189,    -1,   121,   191,   122,   124,    -1,   195,   121,   191,
     122,   124,    -1,   195,   121,   191,   122,   190,   124,    -1,
     195,    -1,   190,   195,    -1,    -1,   192,   124,    -1,    61,
      13,   192,   124,    -1,    72,    13,   192,   124,    -1,    61,
      13,   192,   124,    72,    13,   192,   124,    -1,    33,    -1,
      34,    -1,   192,   124,    33,    -1,   192,   124,    34,    -1,
      -1,    57,   195,   121,   193,   192,   197,   122,    -1,    -1,
     192,   124,    57,   195,   121,   194,   192,   197,   122,    -1,
      57,    -1,   192,   124,    57,    -1,    33,    -1,    34,    -1,
     124,    -1,   123,    -1,   124,    -1,    -1,   123,    -1,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   235,   235,   236,   237,   238,   239,   240,   241,   246,
     247,   252,   253,   256,   255,   259,   261,   262,   263,   265,
     271,   278,   279,   282,   281,   285,   288,   287,   291,   293,
     294,   296,   299,   300,   308,   316,   316,   322,   324,   326,
     332,   333,   338,   340,   343,   342,   350,   351,   356,   358,
     359,   361,   365,   364,   373,   375,   373,   392,   397,   402,
     407,   412,   417,   426,   428,   433,   438,   443,   453,   454,
     461,   462,   469,   470,   477,   478,   480,   482,   488,   497,
     499,   504,   506,   511,   514,   520,   523,   528,   530,   536,
     537,   538,   540,   542,   544,   551,   552,   555,   561,   563,
     565,   567,   569,   576,   578,   584,   591,   600,   605,   614,
     619,   624,   629,   638,   643,   662,   681,   690,   692,   699,
     701,   706,   715,   716,   721,   724,   727,   732,   735,   738,
     742,   744,   746,   750,   752,   754,   759,   760,   765,   774,
     776,   783,   784,   792,   797,   808,   817,   819,   825,   831,
     837,   843,   849,   855,   861,   867,   869,   875,   875,   885,
     887,   889,   891,   893,   895,   897,   899,   901,   903,   905,
     907,   909,   911,   913,   915,   917,   919,   921,   923,   925,
     927,   929,   931,   933,   935,   937,   939,   941,   943,   945,
     947,   949,   951,   953,   955,   957,   959,   961,   963,   965,
     967,   972,   977,   979,   987,   993,  1003,  1006,  1007,  1011,
    1017,  1021,  1022,  1026,  1030,  1035,  1042,  1046,  1054,  1055,
    1057,  1059,  1061,  1070,  1075,  1080,  1085,  1092,  1091,  1102,
    1101,  1108,  1113,  1123,  1125,  1132,  1133,  1138,  1139,  1144,
    1145
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
  "SORT_BY_ALIGNMENT", "SORT_BY_INIT_PRIORITY", "SORT_BY_NAME", "SPECIAL",
  "SQUAD", "STARTUP", "SUBALIGN", "SYSLIB", "TARGET_K", "TRUNCATE",
  "VERSIONK", "OPTION", "PARSING_LINKER_SCRIPT", "PARSING_VERSION_SCRIPT",
  "PARSING_DEFSYM", "PARSING_DYNAMIC_LIST", "PARSING_SECTIONS_BLOCK",
  "PARSING_SECTION_COMMANDS", "PARSING_MEMORY_DEF", "'('", "')'", "'{'",
  "'}'", "','", "';'", "'!'", "'o'", "'l'", "'~'", "$accept", "top",
  "linker_script", "file_cmd", "$@1", "$@2", "$@3", "ignore_cmd",
  "extern_name_list", "$@4", "extern_name_list_body", "input_list",
  "input_list_element", "$@5", "sections_block", "section_block_cmd",
  "$@6", "section_header", "$@7", "$@8", "opt_address_and_section_type",
  "section_type", "opt_at", "opt_align", "opt_subalign", "opt_constraint",
  "section_trailer", "opt_memspec", "opt_at_memspec", "opt_phdr",
  "opt_fill", "section_cmds", "section_cmd", "data_length",
  "input_section_spec", "input_section_no_keep", "wildcard_file",
  "wildcard_sections", "wildcard_section", "exclude_names",
  "wildcard_name", "memory_defs", "memory_def", "memory_attr",
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
     352,   353,   354,   355,   356,   357,   358,   359,   360,    40,
      41,   123,   125,    44,    59,    33,   111,   108,   126
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   129,   130,   130,   130,   130,   130,   130,   130,   131,
     131,   132,   132,   133,   132,   132,   132,   132,   132,   132,
     132,   132,   132,   134,   132,   132,   135,   132,   132,   132,
     132,   132,   132,   132,   136,   138,   137,   139,   139,   139,
     140,   140,   141,   141,   142,   141,   143,   143,   144,   144,
     144,   144,   145,   144,   147,   148,   146,   149,   149,   149,
     149,   149,   149,   150,   150,   150,   150,   150,   151,   151,
     152,   152,   153,   153,   154,   154,   154,   154,   155,   156,
     156,   157,   157,   158,   158,   159,   159,   160,   160,   161,
     161,   161,   161,   161,   161,   161,   161,   161,   162,   162,
     162,   162,   162,   163,   163,   164,   164,   165,   165,   166,
     166,   166,   166,   167,   167,   167,   167,   168,   168,   169,
     169,   169,   170,   170,   171,   171,   171,   172,   172,   172,
     173,   173,   173,   174,   174,   174,   175,   175,   176,   177,
     177,   178,   178,   178,   178,   178,   179,   179,   179,   179,
     179,   179,   179,   179,   179,   179,   179,   181,   180,   182,
     182,   182,   182,   182,   182,   182,   182,   182,   182,   182,
     182,   182,   182,   182,   182,   182,   182,   182,   182,   182,
     182,   182,   182,   182,   182,   182,   182,   182,   182,   182,
     182,   182,   182,   182,   182,   182,   182,   182,   182,   182,
     182,   182,   182,   182,   182,   183,   184,   185,   185,   186,
     187,   188,   188,   189,   189,   189,   190,   190,   191,   191,
     191,   191,   191,   192,   192,   192,   192,   193,   192,   194,
     192,   192,   192,   195,   195,   196,   196,   197,   197,   198,
     198
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       0,     4,     1,     0,     5,     1,     4,     4,     4,     4,
       8,     4,     4,     0,     5,     4,     0,     5,     4,     2,
       6,     2,     1,     1,     4,     0,     2,     1,     2,     3,
       1,     3,     1,     2,     0,     5,     2,     0,     4,     2,
       6,     2,     0,     7,     0,     0,     7,     1,     3,     2,
       4,     4,     5,     1,     1,     1,     1,     1,     0,     4,
       0,     4,     0,     4,     0,     1,     1,     1,     5,     2,
       0,     3,     0,     3,     0,     2,     0,     0,     2,     2,
       1,     4,     6,     4,     1,     4,     2,     1,     1,     1,
       1,     1,     1,     1,     4,     1,     4,     1,     4,     3,
       1,     6,     4,     1,     4,     4,     4,     3,     1,     1,
       1,     1,     3,     0,    10,     2,     0,     3,     4,     0,
       1,     1,     1,     1,     1,     1,     2,     0,     4,     1,
       1,     0,     2,     2,     5,     5,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     6,     6,     0,     2,     3,
       2,     2,     2,     2,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     5,     1,     1,     6,     6,     4,     1,     4,
       4,     4,     4,     4,     4,     4,     4,     4,     6,     4,
       6,     6,     4,     6,     6,     3,     1,     1,     2,     5,
       1,     1,     2,     4,     5,     6,     1,     2,     0,     2,
       4,     4,     8,     1,     1,     3,     3,     0,     7,     0,
       9,     1,     3,     1,     1,     1,     1,     1,     0,     1,
       0
};

/* YYDEFACT[STATE-NAME] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,    10,     0,     0,     0,    47,    87,   123,     0,     2,
     233,   234,   218,     3,   210,   211,     0,     4,     0,     0,
       5,   206,   207,     6,     7,   240,     1,     0,     0,     0,
      12,    13,     0,    15,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    33,     9,    32,     0,
       0,   223,   224,   231,     0,     0,     0,     0,   212,   218,
     157,     0,   208,     0,     0,     0,    46,     0,    54,   121,
     120,     0,   102,    94,     0,     0,     0,   100,    98,   101,
       0,    99,    97,    88,     0,    90,   103,     0,   107,     0,
     105,   239,   126,   157,     0,    35,     0,    31,     0,   123,
       0,     0,   137,     0,     0,     0,    23,     0,    26,     0,
     236,   235,    29,   157,   157,   157,   157,   157,   157,   157,
     157,   157,     0,     0,     0,     0,   219,     0,   205,     0,
       0,   157,     0,    51,    49,    52,     0,   157,   157,    96,
       0,     0,   157,     0,    89,     0,   122,   129,     0,     0,
       0,     0,     0,     0,    44,   240,    40,    42,   240,     0,
       0,     0,     0,     0,     0,    47,     0,     0,     0,   146,
     154,   153,   152,   151,   150,   149,   148,   147,   227,     0,
       0,   213,   225,   226,   232,     0,     0,     0,   183,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   188,     0,     0,
       0,   158,   184,     0,     0,     0,     0,    57,     0,    68,
       0,     0,     0,     0,     0,   105,     0,     0,   119,     0,
       0,     0,     0,     0,   240,   110,   113,   125,     0,     0,
       0,    28,    11,    36,    37,   240,    43,     0,    16,     0,
      17,    34,    19,     0,    21,   136,     0,   157,   157,    22,
       0,    25,     0,    18,     0,   220,   221,     0,   214,     0,
     216,   163,   160,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   161,   162,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   209,     0,    48,    87,    65,    64,    66,
      63,    67,     0,     0,     0,    70,    59,     0,     0,    93,
       0,   104,    95,   108,    91,     0,     0,     0,     0,   106,
       0,     0,     0,     0,     0,     0,    38,    14,     0,    41,
       0,   140,   141,   139,     0,     0,    24,    27,   238,     0,
     229,   215,   217,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   159,     0,   181,   180,   179,   178,   177,   172,   171,
     175,   176,   174,   173,   170,   169,   167,   168,   164,   165,
     166,     0,     0,    58,     0,     0,     0,    72,     0,     0,
       0,   240,   118,     0,     0,     0,     0,   109,     0,   127,
     131,   130,   132,     0,    30,    39,   240,     0,     0,   141,
       0,   141,   155,   156,   237,     0,     0,     0,   196,   191,
     197,     0,   189,     0,   199,   195,     0,   202,     0,   187,
     194,   192,     0,     0,   193,     0,   190,     0,    50,    80,
      61,     0,     0,     0,    55,    60,     0,    92,   112,     0,
     115,   116,   114,     0,   128,   157,    45,     0,   157,   143,
     138,     0,   142,   228,     0,   238,     0,     0,     0,     0,
       0,     0,     0,   182,     0,    53,    82,    69,     0,     0,
      74,    62,   117,   240,   240,    20,     0,     0,   222,     0,
     198,   204,   200,   201,   185,   186,   203,    79,     0,    84,
      71,     0,    75,    76,    77,    56,   111,     0,   141,   141,
     230,     0,    86,    73,   134,   133,   135,     0,   145,   144,
      81,   157,     0,   240,   157,    85,    83,    78,   124
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     8,     9,    47,    96,   165,   167,    48,   150,   151,
     243,   155,   156,   247,    23,    66,   216,   135,   136,   500,
     219,   323,   325,   407,   464,   525,   495,   496,   519,   532,
     543,    24,    83,    84,    85,    86,    87,   234,   235,   411,
     236,    25,   146,   239,   423,   537,   161,   255,   352,   430,
      67,   128,   129,   291,    17,    20,    21,    22,    13,    14,
      15,   269,    56,    57,   264,   437,   212,   112,   435,   249
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -417
static const yytype_int16 yypact[] =
{
     239,  -417,    24,   103,  -107,  -417,  -417,  -417,    27,   589,
    -417,  -417,   135,  -417,    24,  -417,   -62,  -417,    88,   175,
    -417,  -107,  -417,   179,   570,     8,  -417,   -26,   -22,     3,
    -417,  -417,   103,  -417,    10,    -7,    45,    51,     6,    59,
      71,    95,   106,    97,   112,   121,  -417,  -417,  -417,  -105,
     314,  -417,  -417,   103,    82,    91,    26,   126,  -417,   135,
    -417,   132,  -417,   150,   161,   103,  -417,  -105,   314,  -417,
    -417,   166,  -417,  -417,   191,   103,   192,  -417,  -417,  -417,
     194,  -417,  -417,  -417,   196,  -417,  -417,   208,  -417,  -105,
      37,  -417,    46,  -417,   103,  -417,   213,  -417,   146,  -417,
     103,   103,  -417,   103,   103,   103,  -417,   103,  -417,   103,
    -417,  -417,  -417,  -417,  -417,  -417,  -417,  -417,  -417,  -417,
    -417,  -417,   124,   175,   175,   220,   189,   157,  -417,  1336,
      -5,  -417,   103,  -417,  -417,  -417,   332,  -417,  -417,  -417,
      42,   219,  -417,    99,  -417,   103,  -417,   218,   224,   228,
     229,   103,   146,   325,  -417,   -70,  -417,  -417,    29,   241,
       1,    32,   359,   360,   244,  -417,   253,    24,   254,  -417,
    -417,  -417,  -417,  -417,  -417,  -417,  -417,  -417,  -417,   251,
     255,  -417,  -417,  -417,   103,   -24,  1336,  1336,  -417,   258,
     261,   267,   270,   271,   272,   273,   275,   276,   280,   282,
     286,   287,   289,   299,   300,   301,   304,  -417,  1336,  1336,
    1336,  1205,  -417,   263,   303,   317,   318,  -417,  1240,   382,
    1227,   315,   320,   323,   324,   326,   327,   329,  -417,   330,
     334,   337,   339,   342,    75,  -417,  -417,  -417,   -10,   430,
     103,  -417,  -417,   -17,  -417,   119,  -417,   344,  -417,   146,
    -417,  -417,  -417,   103,  -417,  -417,   296,  -417,  -417,  -417,
     133,  -417,   343,  -417,   175,   187,   189,   345,  -417,    -8,
    -417,  -417,  -417,  1336,   103,  1336,   103,  1336,  1336,   103,
    1336,  1336,  1336,   103,   103,   103,  1336,  1336,   103,   103,
     103,   864,  -417,  -417,  1336,  1336,  1336,  1336,  1336,  1336,
    1336,  1336,  1336,  1336,  1336,  1336,  1336,  1336,  1336,  1336,
    1336,  1336,  1336,  -417,   103,  -417,  -417,  -417,  -417,  -417,
    -417,  -417,   433,   348,   350,   426,  -417,   137,   103,  -417,
      55,  -417,  -417,  -417,  -417,    55,   160,    55,   160,  -417,
     128,   103,   352,    -3,   353,   103,  -417,  -417,   146,  -417,
     347,  -417,    28,  -417,   354,   355,  -417,  -417,   358,   463,
    -417,  -417,  -417,   884,   363,   482,   364,   717,   904,   365,
     754,   937,   774,   366,   367,   368,   794,   827,   369,   356,
     370,  -417,   277,   734,   567,   842,   951,   807,   915,   915,
     385,   385,   385,   385,   405,   405,   311,   311,  -417,  -417,
    -417,   371,   113,  -417,   480,  1336,   376,   408,   502,   396,
     397,   145,  -417,   398,   399,   401,   403,  -417,   404,  -417,
    -417,  -417,  -417,   520,  -417,  -417,   153,   103,   406,    28,
     407,    44,  -417,  -417,   189,   410,   175,   175,  -417,  -417,
    -417,  1336,  -417,   103,  -417,  -417,  1336,  -417,  1336,  -417,
    -417,  -417,  1336,  1336,  -417,  1336,  -417,  1336,  -417,   506,
    -417,   971,  1336,   411,  -417,  -417,   521,  -417,  -417,    55,
    -417,  -417,  -417,    55,  -417,  -417,  -417,   413,  -417,  -417,
    -417,   500,  -417,  -417,   412,   358,   991,   417,  1011,  1044,
    1078,  1098,  1118,  1205,   103,  -417,   496,  -417,  1151,  1336,
     181,  -417,  -417,   155,   416,  -417,   422,   424,   189,   425,
    -417,  -417,  -417,  -417,  -417,  -417,  -417,  -417,   529,  -417,
    -417,  1185,  -417,  -417,  -417,  -417,  -417,    -1,    28,    28,
    -417,   103,    61,  -417,  -417,  -417,  -417,   549,  -417,  -417,
    -417,  -417,   103,   416,  -417,  -417,  -417,  -417,  -417
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -417,  -417,  -417,  -417,  -417,  -417,  -417,  -417,  -417,  -417,
    -417,  -146,   307,  -417,   388,  -417,  -417,  -417,  -417,  -417,
    -417,   230,  -417,  -417,  -417,  -417,  -417,  -417,  -417,  -417,
    -417,   242,  -417,  -417,  -417,   419,  -417,  -417,  -253,    87,
     -21,   462,  -417,  -417,  -417,  -417,  -417,  -417,  -417,  -416,
      -4,   -82,  -417,   268,  -417,  -417,  -417,   560,   434,  -417,
     586,  -417,   547,   -15,  -417,  -417,    -2,     5,   122,   -23
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -120
static const yytype_int16 yytable[] =
{
      16,    18,    92,    88,    61,    49,   245,    50,    -8,    10,
      11,   148,    16,   479,    19,   482,    10,    11,   110,   111,
      89,    68,    90,    10,    11,    10,    11,    26,   182,   183,
      97,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   214,
     248,   122,   184,    91,    69,   221,   222,    10,    11,    59,
     229,    10,    11,   133,   541,    10,    11,    69,   534,   535,
     428,    70,   134,   139,   542,    10,    11,    10,    11,    10,
      11,   420,   421,   413,    70,   415,   428,   417,    10,    11,
     147,    60,   149,    93,   144,   123,   157,    94,   159,   160,
     268,   162,   163,   164,   124,   166,   345,   168,   179,   180,
     145,    69,   538,   539,    99,   341,   361,   213,   429,    88,
     227,   252,    95,   422,   253,    69,   536,   102,    70,    98,
     215,    91,    10,    11,   429,    92,    10,    11,   225,   228,
      69,   228,    70,   237,   223,    12,    10,    11,   125,   244,
     157,   250,    91,    71,   254,   230,  -119,    70,    72,   256,
      73,    10,    11,   481,   100,    16,    10,    11,    51,    52,
     101,    74,    69,    63,   153,   354,   355,    75,   103,    10,
      11,    76,   267,   270,   416,   317,    77,   154,    64,    70,
     104,   318,    53,    10,    11,   339,    54,    65,    91,   231,
     232,   233,   426,   319,    39,    40,    78,    55,    51,    52,
      79,   340,    10,    11,   105,    80,   107,    81,   320,    63,
     182,   183,   182,   183,    39,    40,   321,   106,   231,   232,
     233,    69,    53,   108,    64,   459,   342,    82,   344,   347,
     109,   346,    91,    65,   184,   178,   184,   157,    70,   358,
     126,   350,    10,    11,   353,   356,   130,   408,    68,   359,
     231,   232,   233,   522,   523,   468,   226,   362,    91,   131,
      39,    40,   364,   476,   366,   526,    91,   369,    91,   185,
     132,   373,   374,   375,   524,   137,   378,   379,   380,   294,
     457,   295,   296,   297,   298,   299,   300,   301,   302,   303,
     304,   305,   306,   307,   308,   309,   310,   311,   312,   227,
     138,   140,   401,   141,   412,   142,   414,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   410,   143,   228,    10,
      11,   351,   152,   228,   228,   228,   228,   238,   228,   418,
     310,   311,   312,   425,   181,   217,   157,   240,   241,   242,
     431,     1,     2,     3,     4,     5,     6,     7,   246,   186,
     187,   251,   257,   258,   259,    10,    11,   188,   189,   190,
     191,   192,   193,   261,   263,   265,   194,   273,   195,   266,
     274,    88,   196,   197,   198,   199,   275,   313,   469,   276,
     277,   278,   279,   504,   280,   281,   506,   211,    89,   282,
      90,   283,   200,   201,   220,   284,   285,   202,   286,   203,
     306,   307,   308,   309,   310,   311,   312,   204,   287,   288,
     289,   484,   485,   290,   324,   477,   314,   431,   205,   431,
     206,   207,   308,   309,   310,   311,   312,   315,   328,   316,
     329,   487,   330,   343,   331,  -119,   403,   332,   502,   333,
     334,   218,   412,   335,   271,   272,   336,   209,   337,   545,
     210,   338,   548,   348,   406,   357,   360,   228,   404,   405,
     427,   228,   419,   424,   432,   433,   436,   292,   293,   455,
     469,   527,   434,   439,   442,   445,   449,   450,   451,   454,
     456,   458,   517,   460,   294,   462,   295,   296,   297,   298,
     299,   300,   301,   302,   303,   304,   305,   306,   307,   308,
     309,   310,   311,   312,   463,   465,   466,   467,   470,   471,
     547,   472,   473,   475,   474,   478,   431,   431,   494,   540,
     499,   480,   483,   505,   501,   507,   508,   511,   518,    91,
     546,   363,   528,   365,   529,   367,   368,   530,   370,   371,
     372,   531,   544,   260,   376,   377,   349,   409,   402,   224,
     503,   158,   382,   383,   384,   385,   386,   387,   388,   389,
     390,   391,   392,   393,   394,   395,   396,   397,   398,   399,
     400,    62,    69,   297,   298,   299,   300,   301,   302,   303,
     304,   305,   306,   307,   308,   309,   310,   311,   312,    70,
      58,   262,   440,    10,    11,   441,   127,   509,     0,     0,
      71,     0,     0,     0,     0,    72,     0,    73,     0,     0,
       0,     0,    10,    11,     0,     0,     0,     0,    74,    27,
       0,     0,     0,     0,    75,     0,     0,     0,    76,     0,
       0,     0,     0,    77,    28,     0,    29,     0,     0,    30,
       0,    31,     0,    32,    33,     0,    34,     0,     0,     0,
       0,    39,    40,    78,     0,    35,     0,    79,     0,     0,
       0,     0,    80,   461,    81,     0,    36,    37,     0,    38,
      39,    40,     0,    41,    42,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    82,     0,     0,    43,     0,    44,
      45,     0,     0,     0,     0,     0,     0,     0,     0,   486,
       0,     0,     0,    46,   488,     0,   489,     0,     0,     0,
     490,   491,     0,   492,     0,   493,     0,     0,     0,   294,
     498,   295,   296,   297,   298,   299,   300,   301,   302,   303,
     304,   305,   306,   307,   308,   309,   310,   311,   312,   296,
     297,   298,   299,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,   312,   294,   521,   295,   296,
     297,   298,   299,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,   312,   294,     0,   295,   296,
     297,   298,   299,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,   312,   294,     0,   295,   296,
     297,   298,   299,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,   312,   300,   301,   302,   303,
     304,   305,   306,   307,   308,   309,   310,   311,   312,   294,
     443,   295,   296,   297,   298,   299,   300,   301,   302,   303,
     304,   305,   306,   307,   308,   309,   310,   311,   312,   298,
     299,   300,   301,   302,   303,   304,   305,   306,   307,   308,
     309,   310,   311,   312,     0,     0,   294,   446,   295,   296,
     297,   298,   299,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,   312,   294,   448,   295,   296,
     297,   298,   299,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,   312,   294,   452,   295,   296,
     297,   298,   299,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,   312,   302,   303,   304,   305,
     306,   307,   308,   309,   310,   311,   312,     0,     0,   294,
     453,   295,   296,   297,   298,   299,   300,   301,   302,   303,
     304,   305,   306,   307,   308,   309,   310,   311,   312,   299,
     300,   301,   302,   303,   304,   305,   306,   307,   308,   309,
     310,   311,   312,   294,   381,   295,   296,   297,   298,   299,
     300,   301,   302,   303,   304,   305,   306,   307,   308,   309,
     310,   311,   312,   294,   438,   295,   296,   297,   298,   299,
     300,   301,   302,   303,   304,   305,   306,   307,   308,   309,
     310,   311,   312,   294,   444,   295,   296,   297,   298,   299,
     300,   301,   302,   303,   304,   305,   306,   307,   308,   309,
     310,   311,   312,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   294,   447,   295,   296,
     297,   298,   299,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,   312,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     294,   497,   295,   296,   297,   298,   299,   300,   301,   302,
     303,   304,   305,   306,   307,   308,   309,   310,   311,   312,
     294,   510,   295,   296,   297,   298,   299,   300,   301,   302,
     303,   304,   305,   306,   307,   308,   309,   310,   311,   312,
     294,   512,   295,   296,   297,   298,   299,   300,   301,   302,
     303,   304,   305,   306,   307,   308,   309,   310,   311,   312,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   294,   513,   295,   296,   297,   298,   299,
     300,   301,   302,   303,   304,   305,   306,   307,   308,   309,
     310,   311,   312,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   294,   514,   295,
     296,   297,   298,   299,   300,   301,   302,   303,   304,   305,
     306,   307,   308,   309,   310,   311,   312,   294,   515,   295,
     296,   297,   298,   299,   300,   301,   302,   303,   304,   305,
     306,   307,   308,   309,   310,   311,   312,     0,   516,   294,
     326,   295,   296,   297,   298,   299,   300,   301,   302,   303,
     304,   305,   306,   307,   308,   309,   310,   311,   312,     0,
       0,     0,     0,     0,     0,     0,     0,   186,   187,     0,
       0,   520,     0,    10,    11,   188,   189,   190,   191,   192,
     193,     0,     0,     0,   194,     0,   195,     0,   317,     0,
     196,   197,   198,   199,   318,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   533,   319,     0,     0,     0,
     200,   201,     0,     0,     0,   202,     0,   203,     0,     0,
       0,   320,     0,     0,     0,   204,     0,     0,     0,   321,
       0,     0,     0,     0,     0,     0,   205,     0,   206,   207,
       0,     0,     0,     0,     0,     0,   327,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   208,
     322,     0,     0,   186,   187,   209,     0,     0,   210,    10,
      11,   188,   189,   190,   191,   192,   193,     0,     0,     0,
     194,     0,   195,     0,     0,     0,   196,   197,   198,   199,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   200,   201,     0,     0,
       0,   202,     0,   203,     0,     0,     0,     0,     0,     0,
       0,   204,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   205,     0,   206,   207,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   208,     0,     0,     0,     0,
       0,   209,     0,     0,   210
};

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-417)))

#define yytable_value_is_error(Yytable_value) \
  YYID (0)

static const yytype_int16 yycheck[] =
{
       2,     3,    25,    24,    19,     9,   152,     9,     0,    33,
      34,    93,    14,   429,   121,   431,    33,    34,   123,   124,
      24,    23,    24,    33,    34,    33,    34,     0,    33,    34,
      32,   113,   114,   115,   116,   117,   118,   119,   120,   121,
       3,     4,     5,     6,     7,     8,     9,    10,    11,   131,
     120,    53,    57,   123,    12,   137,   138,    33,    34,   121,
     142,    33,    34,    65,     3,    33,    34,    12,    69,    70,
      42,    29,    67,    75,    13,    33,    34,    33,    34,    33,
      34,    84,    85,   336,    29,   338,    42,   340,    33,    34,
      92,     3,    94,   119,    89,    13,    98,   119,   100,   101,
     124,   103,   104,   105,    13,   107,   123,   109,   123,   124,
      64,    12,   528,   529,   121,   125,   124,   122,    90,   140,
     141,   120,   119,   126,   123,    12,   127,   121,    29,   119,
     132,   123,    33,    34,    90,   158,    33,    34,   140,   141,
      12,   143,    29,   145,   102,   121,    33,    34,   122,   151,
     152,   122,   123,    40,   122,    56,   119,    29,    45,   161,
      47,    33,    34,   119,   119,   167,    33,    34,    33,    34,
     119,    58,    12,    40,    28,   257,   258,    64,   119,    33,
      34,    68,   184,   185,    56,    48,    73,    41,    55,    29,
     119,    54,    57,    33,    34,   120,    61,    64,   123,   100,
     101,   102,   348,    66,    91,    92,    93,    72,    33,    34,
      97,   234,    33,    34,   119,   102,   119,   104,    81,    40,
      33,    34,    33,    34,    91,    92,    89,   121,   100,   101,
     102,    12,    57,   121,    55,   122,   238,   124,   240,   120,
     119,   243,   123,    64,    57,   121,    57,   249,    29,   264,
     124,   253,    33,    34,   256,   122,   124,   120,   260,    72,
     100,   101,   102,    82,    83,   120,    47,   269,   123,   119,
      91,    92,   274,   120,   276,   120,   123,   279,   123,   122,
     119,   283,   284,   285,   103,   119,   288,   289,   290,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,   330,
     119,   119,   314,   119,   335,   119,   337,     3,     4,     5,
       6,     7,     8,     9,    10,    11,   328,   119,   330,    33,
      34,    35,   119,   335,   336,   337,   338,   119,   340,   341,
      29,    30,    31,   345,   124,    13,   348,   123,   120,   120,
     352,   112,   113,   114,   115,   116,   117,   118,    33,    27,
      28,   120,     3,     3,   120,    33,    34,    35,    36,    37,
      38,    39,    40,   120,   120,   124,    44,   119,    46,   124,
     119,   402,    50,    51,    52,    53,   119,   124,   411,   119,
     119,   119,   119,   475,   119,   119,   478,   129,   402,   119,
     402,   119,    70,    71,   136,   119,   119,    75,   119,    77,
      25,    26,    27,    28,    29,    30,    31,    85,   119,   119,
     119,   436,   437,   119,    42,   427,   123,   429,    96,   431,
      98,    99,    27,    28,    29,    30,    31,   120,   123,   121,
     120,   443,   119,    13,   120,   119,    13,   120,   469,   120,
     120,   119,   473,   119,   186,   187,   119,   125,   119,   541,
     128,   119,   544,   119,    38,   122,   121,   469,   120,   119,
     123,   473,   120,   120,   120,   120,    13,   209,   210,   123,
     503,   504,   124,   120,   120,   120,   120,   120,   120,   120,
     120,   120,   494,    13,    12,   119,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,   106,    13,   120,   120,   120,   120,
     543,   120,   119,     3,   120,   119,   528,   529,    22,   531,
     119,   124,   122,   120,    13,    35,   124,   120,    42,   123,
     542,   273,   120,   275,   120,   277,   278,   122,   280,   281,
     282,    22,     3,   165,   286,   287,   249,   327,   316,   140,
     473,    99,   294,   295,   296,   297,   298,   299,   300,   301,
     302,   303,   304,   305,   306,   307,   308,   309,   310,   311,
     312,    21,    12,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    29,
      14,   167,   120,    33,    34,   123,    59,   485,    -1,    -1,
      40,    -1,    -1,    -1,    -1,    45,    -1,    47,    -1,    -1,
      -1,    -1,    33,    34,    -1,    -1,    -1,    -1,    58,    40,
      -1,    -1,    -1,    -1,    64,    -1,    -1,    -1,    68,    -1,
      -1,    -1,    -1,    73,    55,    -1,    57,    -1,    -1,    60,
      -1,    62,    -1,    64,    65,    -1,    67,    -1,    -1,    -1,
      -1,    91,    92,    93,    -1,    76,    -1,    97,    -1,    -1,
      -1,    -1,   102,   405,   104,    -1,    87,    88,    -1,    90,
      91,    92,    -1,    94,    95,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   124,    -1,    -1,   108,    -1,   110,
     111,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   441,
      -1,    -1,    -1,   124,   446,    -1,   448,    -1,    -1,    -1,
     452,   453,    -1,   455,    -1,   457,    -1,    -1,    -1,    12,
     462,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    12,   499,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    12,    -1,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    12,    -1,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    12,
     123,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    -1,    -1,    12,   123,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    12,   123,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    12,   123,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    -1,    -1,    12,
     123,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    12,   120,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    12,   120,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    12,   120,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    12,   120,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      12,   120,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      12,   120,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      12,   120,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    12,   120,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    12,   120,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    12,   120,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    -1,   120,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    27,    28,    -1,
      -1,   120,    -1,    33,    34,    35,    36,    37,    38,    39,
      40,    -1,    -1,    -1,    44,    -1,    46,    -1,    48,    -1,
      50,    51,    52,    53,    54,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   120,    66,    -1,    -1,    -1,
      70,    71,    -1,    -1,    -1,    75,    -1,    77,    -1,    -1,
      -1,    81,    -1,    -1,    -1,    85,    -1,    -1,    -1,    89,
      -1,    -1,    -1,    -1,    -1,    -1,    96,    -1,    98,    99,
      -1,    -1,    -1,    -1,    -1,    -1,   119,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   119,
     120,    -1,    -1,    27,    28,   125,    -1,    -1,   128,    33,
      34,    35,    36,    37,    38,    39,    40,    -1,    -1,    -1,
      44,    -1,    46,    -1,    -1,    -1,    50,    51,    52,    53,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    70,    71,    -1,    -1,
      -1,    75,    -1,    77,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    85,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    96,    -1,    98,    99,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   119,    -1,    -1,    -1,    -1,
      -1,   125,    -1,    -1,   128
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,   112,   113,   114,   115,   116,   117,   118,   130,   131,
      33,    34,   121,   187,   188,   189,   195,   183,   195,   121,
     184,   185,   186,   143,   160,   170,     0,    40,    55,    57,
      60,    62,    64,    65,    67,    76,    87,    88,    90,    91,
      92,    94,    95,   108,   110,   111,   124,   132,   136,   179,
     195,    33,    34,    57,    61,    72,   191,   192,   189,   121,
       3,   192,   186,    40,    55,    64,   144,   179,   195,    12,
      29,    40,    45,    47,    58,    64,    68,    73,    93,    97,
     102,   104,   124,   161,   162,   163,   164,   165,   169,   179,
     195,   123,   198,   119,   119,   119,   133,   195,   119,   121,
     119,   119,   121,   119,   119,   119,   121,   119,   121,   119,
     123,   124,   196,     3,     4,     5,     6,     7,     8,     9,
      10,    11,   195,    13,    13,   122,   124,   191,   180,   181,
     124,   119,   119,   195,   196,   146,   147,   119,   119,   195,
     119,   119,   119,   119,   196,    64,   171,   195,   180,   195,
     137,   138,   119,    28,    41,   140,   141,   195,   170,   195,
     195,   175,   195,   195,   195,   134,   195,   135,   195,   180,
     180,   180,   180,   180,   180,   180,   180,   180,   121,   192,
     192,   124,    33,    34,    57,   122,    27,    28,    35,    36,
      37,    38,    39,    40,    44,    46,    50,    51,    52,    53,
      70,    71,    75,    77,    85,    96,    98,    99,   119,   125,
     128,   182,   195,   122,   180,   195,   145,    13,   119,   149,
     182,   180,   180,   102,   164,   195,    47,   169,   195,   180,
      56,   100,   101,   102,   166,   167,   169,   195,   119,   172,
     123,   120,   120,   139,   195,   140,    33,   142,   120,   198,
     122,   120,   120,   123,   122,   176,   195,     3,     3,   120,
     143,   120,   187,   120,   193,   124,   124,   195,   124,   190,
     195,   182,   182,   119,   119,   119,   119,   119,   119,   119,
     119,   119,   119,   119,   119,   119,   119,   119,   119,   119,
     119,   182,   182,   182,    12,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,   124,   123,   120,   121,    48,    54,    66,
      81,    89,   120,   150,    42,   151,    13,   119,   123,   120,
     119,   120,   120,   120,   120,   119,   119,   119,   119,   120,
     198,   125,   195,    13,   195,   123,   195,   120,   119,   141,
     195,    35,   177,   195,   180,   180,   122,   122,   192,    72,
     121,   124,   195,   182,   195,   182,   195,   182,   182,   195,
     182,   182,   182,   195,   195,   195,   182,   182,   195,   195,
     195,   120,   182,   182,   182,   182,   182,   182,   182,   182,
     182,   182,   182,   182,   182,   182,   182,   182,   182,   182,
     182,   195,   160,    13,   120,   119,    38,   152,   120,   150,
     195,   168,   169,   167,   169,   167,    56,   167,   195,   120,
      84,    85,   126,   173,   120,   195,   140,   123,    42,    90,
     178,   195,   120,   120,   124,   197,    13,   194,   120,   120,
     120,   123,   120,   123,   120,   120,   123,   120,   123,   120,
     120,   120,   123,   123,   120,   123,   120,    13,   120,   122,
      13,   182,   119,   106,   153,    13,   120,   120,   120,   198,
     120,   120,   120,   119,   120,     3,   120,   195,   119,   178,
     124,   119,   178,   122,   192,   192,   182,   195,   182,   182,
     182,   182,   182,   182,    22,   155,   156,   120,   182,   119,
     148,    13,   169,   168,   180,   120,   180,    35,   124,   197,
     120,   120,   120,   120,   120,   120,   120,   195,    42,   157,
     120,   182,    82,    83,   103,   154,   120,   198,   120,   120,
     122,    22,   158,   120,    69,    70,   127,   174,   178,   178,
     195,     3,    13,   159,     3,   180,   195,   198,   180
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
        case 12:
/* Line 1787 of yacc.c  */
#line 254 "yyscript.y"
    { script_set_common_allocation(closure, 1); }
    break;

  case 13:
/* Line 1787 of yacc.c  */
#line 256 "yyscript.y"
    { script_start_group(closure); }
    break;

  case 14:
/* Line 1787 of yacc.c  */
#line 258 "yyscript.y"
    { script_end_group(closure); }
    break;

  case 15:
/* Line 1787 of yacc.c  */
#line 260 "yyscript.y"
    { script_set_common_allocation(closure, 0); }
    break;

  case 18:
/* Line 1787 of yacc.c  */
#line 264 "yyscript.y"
    { script_parse_option(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 19:
/* Line 1787 of yacc.c  */
#line 266 "yyscript.y"
    {
	      if (!script_check_output_format(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length,
					      NULL, 0, NULL, 0))
		YYABORT;
	    }
    break;

  case 20:
/* Line 1787 of yacc.c  */
#line 272 "yyscript.y"
    {
	      if (!script_check_output_format(closure, (yyvsp[(3) - (8)].string).value, (yyvsp[(3) - (8)].string).length,
					      (yyvsp[(5) - (8)].string).value, (yyvsp[(5) - (8)].string).length,
					      (yyvsp[(7) - (8)].string).value, (yyvsp[(7) - (8)].string).length))
		YYABORT;
	    }
    break;

  case 22:
/* Line 1787 of yacc.c  */
#line 280 "yyscript.y"
    { script_add_search_dir(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 23:
/* Line 1787 of yacc.c  */
#line 282 "yyscript.y"
    { script_start_sections(closure); }
    break;

  case 24:
/* Line 1787 of yacc.c  */
#line 284 "yyscript.y"
    { script_finish_sections(closure); }
    break;

  case 25:
/* Line 1787 of yacc.c  */
#line 286 "yyscript.y"
    { script_set_target(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 26:
/* Line 1787 of yacc.c  */
#line 288 "yyscript.y"
    { script_push_lex_into_version_mode(closure); }
    break;

  case 27:
/* Line 1787 of yacc.c  */
#line 290 "yyscript.y"
    { script_pop_lex_mode(closure); }
    break;

  case 28:
/* Line 1787 of yacc.c  */
#line 292 "yyscript.y"
    { script_set_entry(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 30:
/* Line 1787 of yacc.c  */
#line 295 "yyscript.y"
    { script_add_assertion(closure, (yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].string).value, (yyvsp[(5) - (6)].string).length); }
    break;

  case 31:
/* Line 1787 of yacc.c  */
#line 297 "yyscript.y"
    { script_include_directive(PARSING_LINKER_SCRIPT, closure,
				       (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 35:
/* Line 1787 of yacc.c  */
#line 316 "yyscript.y"
    { script_push_lex_into_expression_mode(closure); }
    break;

  case 36:
/* Line 1787 of yacc.c  */
#line 318 "yyscript.y"
    { script_pop_lex_mode(closure); }
    break;

  case 37:
/* Line 1787 of yacc.c  */
#line 323 "yyscript.y"
    { script_add_extern(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 38:
/* Line 1787 of yacc.c  */
#line 325 "yyscript.y"
    { script_add_extern(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 39:
/* Line 1787 of yacc.c  */
#line 327 "yyscript.y"
    { script_add_extern(closure, (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length); }
    break;

  case 42:
/* Line 1787 of yacc.c  */
#line 339 "yyscript.y"
    { script_add_file(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 43:
/* Line 1787 of yacc.c  */
#line 341 "yyscript.y"
    { script_add_library(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 44:
/* Line 1787 of yacc.c  */
#line 343 "yyscript.y"
    { script_start_as_needed(closure); }
    break;

  case 45:
/* Line 1787 of yacc.c  */
#line 345 "yyscript.y"
    { script_end_as_needed(closure); }
    break;

  case 48:
/* Line 1787 of yacc.c  */
#line 357 "yyscript.y"
    { script_set_entry(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 50:
/* Line 1787 of yacc.c  */
#line 360 "yyscript.y"
    { script_add_assertion(closure, (yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].string).value, (yyvsp[(5) - (6)].string).length); }
    break;

  case 51:
/* Line 1787 of yacc.c  */
#line 362 "yyscript.y"
    { script_include_directive(PARSING_SECTIONS_BLOCK, closure,
				       (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 52:
/* Line 1787 of yacc.c  */
#line 365 "yyscript.y"
    { script_start_output_section(closure, (yyvsp[(1) - (2)].string).value, (yyvsp[(1) - (2)].string).length, &(yyvsp[(2) - (2)].output_section_header)); }
    break;

  case 53:
/* Line 1787 of yacc.c  */
#line 367 "yyscript.y"
    { script_finish_output_section(closure, &(yyvsp[(7) - (7)].output_section_trailer)); }
    break;

  case 54:
/* Line 1787 of yacc.c  */
#line 373 "yyscript.y"
    { script_push_lex_into_expression_mode(closure); }
    break;

  case 55:
/* Line 1787 of yacc.c  */
#line 375 "yyscript.y"
    { script_pop_lex_mode(closure); }
    break;

  case 56:
/* Line 1787 of yacc.c  */
#line 377 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(2) - (7)].output_section_header).address;
	      (yyval.output_section_header).section_type = (yyvsp[(2) - (7)].output_section_header).section_type;
	      (yyval.output_section_header).load_address = (yyvsp[(3) - (7)].expr);
	      (yyval.output_section_header).align = (yyvsp[(4) - (7)].expr);
	      (yyval.output_section_header).subalign = (yyvsp[(5) - (7)].expr);
	      (yyval.output_section_header).constraint = (yyvsp[(7) - (7)].constraint);
	    }
    break;

  case 57:
/* Line 1787 of yacc.c  */
#line 393 "yyscript.y"
    {
	      (yyval.output_section_header).address = NULL;
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 58:
/* Line 1787 of yacc.c  */
#line 398 "yyscript.y"
    {
	      (yyval.output_section_header).address = NULL;
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 59:
/* Line 1787 of yacc.c  */
#line 403 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(1) - (2)].expr);
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 60:
/* Line 1787 of yacc.c  */
#line 408 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(1) - (4)].expr);
	      (yyval.output_section_header).section_type = SCRIPT_SECTION_TYPE_NONE;
	    }
    break;

  case 61:
/* Line 1787 of yacc.c  */
#line 413 "yyscript.y"
    {
	      (yyval.output_section_header).address = NULL;
	      (yyval.output_section_header).section_type = (yyvsp[(2) - (4)].section_type);
	    }
    break;

  case 62:
/* Line 1787 of yacc.c  */
#line 418 "yyscript.y"
    {
	      (yyval.output_section_header).address = (yyvsp[(1) - (5)].expr);
	      (yyval.output_section_header).section_type = (yyvsp[(3) - (5)].section_type);
	    }
    break;

  case 63:
/* Line 1787 of yacc.c  */
#line 427 "yyscript.y"
    { (yyval.section_type) = SCRIPT_SECTION_TYPE_NOLOAD; }
    break;

  case 64:
/* Line 1787 of yacc.c  */
#line 429 "yyscript.y"
    {
	      yyerror(closure, "DSECT section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_DSECT;
	    }
    break;

  case 65:
/* Line 1787 of yacc.c  */
#line 434 "yyscript.y"
    {
	      yyerror(closure, "COPY section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_COPY;
	    }
    break;

  case 66:
/* Line 1787 of yacc.c  */
#line 439 "yyscript.y"
    {
	      yyerror(closure, "INFO section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_INFO;
	    }
    break;

  case 67:
/* Line 1787 of yacc.c  */
#line 444 "yyscript.y"
    {
	      yyerror(closure, "OVERLAY section type is unsupported");
	      (yyval.section_type) = SCRIPT_SECTION_TYPE_OVERLAY;
	    }
    break;

  case 68:
/* Line 1787 of yacc.c  */
#line 453 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 69:
/* Line 1787 of yacc.c  */
#line 455 "yyscript.y"
    { (yyval.expr) = (yyvsp[(3) - (4)].expr); }
    break;

  case 70:
/* Line 1787 of yacc.c  */
#line 461 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 71:
/* Line 1787 of yacc.c  */
#line 463 "yyscript.y"
    { (yyval.expr) = (yyvsp[(3) - (4)].expr); }
    break;

  case 72:
/* Line 1787 of yacc.c  */
#line 469 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 73:
/* Line 1787 of yacc.c  */
#line 471 "yyscript.y"
    { (yyval.expr) = (yyvsp[(3) - (4)].expr); }
    break;

  case 74:
/* Line 1787 of yacc.c  */
#line 477 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_NONE; }
    break;

  case 75:
/* Line 1787 of yacc.c  */
#line 479 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_ONLY_IF_RO; }
    break;

  case 76:
/* Line 1787 of yacc.c  */
#line 481 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_ONLY_IF_RW; }
    break;

  case 77:
/* Line 1787 of yacc.c  */
#line 483 "yyscript.y"
    { (yyval.constraint) = CONSTRAINT_SPECIAL; }
    break;

  case 78:
/* Line 1787 of yacc.c  */
#line 489 "yyscript.y"
    {
	      (yyval.output_section_trailer).fill = (yyvsp[(4) - (5)].expr);
	      (yyval.output_section_trailer).phdrs = (yyvsp[(3) - (5)].string_list);
	    }
    break;

  case 79:
/* Line 1787 of yacc.c  */
#line 498 "yyscript.y"
    { script_set_section_region(closure, (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length, 1); }
    break;

  case 81:
/* Line 1787 of yacc.c  */
#line 505 "yyscript.y"
    { script_set_section_region(closure, (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length, 0); }
    break;

  case 83:
/* Line 1787 of yacc.c  */
#line 512 "yyscript.y"
    { (yyval.string_list) = script_string_list_push_back((yyvsp[(1) - (3)].string_list), (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length); }
    break;

  case 84:
/* Line 1787 of yacc.c  */
#line 514 "yyscript.y"
    { (yyval.string_list) = NULL; }
    break;

  case 85:
/* Line 1787 of yacc.c  */
#line 521 "yyscript.y"
    { (yyval.expr) = (yyvsp[(2) - (2)].expr); }
    break;

  case 86:
/* Line 1787 of yacc.c  */
#line 523 "yyscript.y"
    { (yyval.expr) = NULL; }
    break;

  case 91:
/* Line 1787 of yacc.c  */
#line 539 "yyscript.y"
    { script_add_data(closure, (yyvsp[(1) - (4)].integer), (yyvsp[(3) - (4)].expr)); }
    break;

  case 92:
/* Line 1787 of yacc.c  */
#line 541 "yyscript.y"
    { script_add_assertion(closure, (yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].string).value, (yyvsp[(5) - (6)].string).length); }
    break;

  case 93:
/* Line 1787 of yacc.c  */
#line 543 "yyscript.y"
    { script_add_fill(closure, (yyvsp[(3) - (4)].expr)); }
    break;

  case 94:
/* Line 1787 of yacc.c  */
#line 545 "yyscript.y"
    {
	      /* The GNU linker uses CONSTRUCTORS for the a.out object
		 file format.  It does nothing when using ELF.  Since
		 some ELF linker scripts use it although it does
		 nothing, we accept it and ignore it.  */
	    }
    break;

  case 96:
/* Line 1787 of yacc.c  */
#line 553 "yyscript.y"
    { script_include_directive(PARSING_SECTION_COMMANDS, closure,
				       (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 98:
/* Line 1787 of yacc.c  */
#line 562 "yyscript.y"
    { (yyval.integer) = QUAD; }
    break;

  case 99:
/* Line 1787 of yacc.c  */
#line 564 "yyscript.y"
    { (yyval.integer) = SQUAD; }
    break;

  case 100:
/* Line 1787 of yacc.c  */
#line 566 "yyscript.y"
    { (yyval.integer) = LONG; }
    break;

  case 101:
/* Line 1787 of yacc.c  */
#line 568 "yyscript.y"
    { (yyval.integer) = SHORT; }
    break;

  case 102:
/* Line 1787 of yacc.c  */
#line 570 "yyscript.y"
    { (yyval.integer) = BYTE; }
    break;

  case 103:
/* Line 1787 of yacc.c  */
#line 577 "yyscript.y"
    { script_add_input_section(closure, &(yyvsp[(1) - (1)].input_section_spec), 0); }
    break;

  case 104:
/* Line 1787 of yacc.c  */
#line 579 "yyscript.y"
    { script_add_input_section(closure, &(yyvsp[(3) - (4)].input_section_spec), 1); }
    break;

  case 105:
/* Line 1787 of yacc.c  */
#line 585 "yyscript.y"
    {
	      (yyval.input_section_spec).file.name = (yyvsp[(1) - (1)].string);
	      (yyval.input_section_spec).file.sort = SORT_WILDCARD_NONE;
	      (yyval.input_section_spec).input_sections.sections = NULL;
	      (yyval.input_section_spec).input_sections.exclude = NULL;
	    }
    break;

  case 106:
/* Line 1787 of yacc.c  */
#line 592 "yyscript.y"
    {
	      (yyval.input_section_spec).file = (yyvsp[(1) - (4)].wildcard_section);
	      (yyval.input_section_spec).input_sections = (yyvsp[(3) - (4)].wildcard_sections);
	    }
    break;

  case 107:
/* Line 1787 of yacc.c  */
#line 601 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(1) - (1)].string);
	      (yyval.wildcard_section).sort = SORT_WILDCARD_NONE;
	    }
    break;

  case 108:
/* Line 1787 of yacc.c  */
#line 606 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(3) - (4)].string);
	      (yyval.wildcard_section).sort = SORT_WILDCARD_BY_NAME;
	    }
    break;

  case 109:
/* Line 1787 of yacc.c  */
#line 615 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = script_string_sort_list_add((yyvsp[(1) - (3)].wildcard_sections).sections, &(yyvsp[(3) - (3)].wildcard_section));
	      (yyval.wildcard_sections).exclude = (yyvsp[(1) - (3)].wildcard_sections).exclude;
	    }
    break;

  case 110:
/* Line 1787 of yacc.c  */
#line 620 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = script_new_string_sort_list(&(yyvsp[(1) - (1)].wildcard_section));
	      (yyval.wildcard_sections).exclude = NULL;
	    }
    break;

  case 111:
/* Line 1787 of yacc.c  */
#line 625 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = (yyvsp[(1) - (6)].wildcard_sections).sections;
	      (yyval.wildcard_sections).exclude = script_string_list_append((yyvsp[(1) - (6)].wildcard_sections).exclude, (yyvsp[(5) - (6)].string_list));
	    }
    break;

  case 112:
/* Line 1787 of yacc.c  */
#line 630 "yyscript.y"
    {
	      (yyval.wildcard_sections).sections = NULL;
	      (yyval.wildcard_sections).exclude = (yyvsp[(3) - (4)].string_list);
	    }
    break;

  case 113:
/* Line 1787 of yacc.c  */
#line 639 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(1) - (1)].string);
	      (yyval.wildcard_section).sort = SORT_WILDCARD_NONE;
	    }
    break;

  case 114:
/* Line 1787 of yacc.c  */
#line 644 "yyscript.y"
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

  case 115:
/* Line 1787 of yacc.c  */
#line 663 "yyscript.y"
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

  case 116:
/* Line 1787 of yacc.c  */
#line 682 "yyscript.y"
    {
	      (yyval.wildcard_section).name = (yyvsp[(3) - (4)].string);
	      (yyval.wildcard_section).sort = SORT_WILDCARD_BY_INIT_PRIORITY;
	    }
    break;

  case 117:
/* Line 1787 of yacc.c  */
#line 691 "yyscript.y"
    { (yyval.string_list) = script_string_list_push_back((yyvsp[(1) - (3)].string_list), (yyvsp[(3) - (3)].string).value, (yyvsp[(3) - (3)].string).length); }
    break;

  case 118:
/* Line 1787 of yacc.c  */
#line 693 "yyscript.y"
    { (yyval.string_list) = script_new_string_list((yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 119:
/* Line 1787 of yacc.c  */
#line 700 "yyscript.y"
    { (yyval.string) = (yyvsp[(1) - (1)].string); }
    break;

  case 120:
/* Line 1787 of yacc.c  */
#line 702 "yyscript.y"
    {
	      (yyval.string).value = "*";
	      (yyval.string).length = 1;
	    }
    break;

  case 121:
/* Line 1787 of yacc.c  */
#line 707 "yyscript.y"
    {
	      (yyval.string).value = "?";
	      (yyval.string).length = 1;
	    }
    break;

  case 124:
/* Line 1787 of yacc.c  */
#line 722 "yyscript.y"
    { script_add_memory(closure, (yyvsp[(1) - (10)].string).value, (yyvsp[(1) - (10)].string).length, (yyvsp[(2) - (10)].integer), (yyvsp[(6) - (10)].expr), (yyvsp[(10) - (10)].expr)); }
    break;

  case 125:
/* Line 1787 of yacc.c  */
#line 725 "yyscript.y"
    { script_include_directive(PARSING_MEMORY_DEF, closure,
				     (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length); }
    break;

  case 127:
/* Line 1787 of yacc.c  */
#line 733 "yyscript.y"
    { (yyval.integer) = script_parse_memory_attr(closure, (yyvsp[(2) - (3)].string).value, (yyvsp[(2) - (3)].string).length, 0); }
    break;

  case 128:
/* Line 1787 of yacc.c  */
#line 736 "yyscript.y"
    { (yyval.integer) = script_parse_memory_attr(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length, 1); }
    break;

  case 129:
/* Line 1787 of yacc.c  */
#line 738 "yyscript.y"
    { (yyval.integer) = 0; }
    break;

  case 138:
/* Line 1787 of yacc.c  */
#line 766 "yyscript.y"
    { script_add_phdr(closure, (yyvsp[(1) - (4)].string).value, (yyvsp[(1) - (4)].string).length, (yyvsp[(2) - (4)].integer), &(yyvsp[(3) - (4)].phdr_info)); }
    break;

  case 139:
/* Line 1787 of yacc.c  */
#line 775 "yyscript.y"
    { (yyval.integer) = script_phdr_string_to_type(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 140:
/* Line 1787 of yacc.c  */
#line 777 "yyscript.y"
    { (yyval.integer) = (yyvsp[(1) - (1)].integer); }
    break;

  case 141:
/* Line 1787 of yacc.c  */
#line 783 "yyscript.y"
    { memset(&(yyval.phdr_info), 0, sizeof(struct Phdr_info)); }
    break;

  case 142:
/* Line 1787 of yacc.c  */
#line 785 "yyscript.y"
    {
	      (yyval.phdr_info) = (yyvsp[(2) - (2)].phdr_info);
	      if ((yyvsp[(1) - (2)].string).length == 7 && strncmp((yyvsp[(1) - (2)].string).value, "FILEHDR", 7) == 0)
		(yyval.phdr_info).includes_filehdr = 1;
	      else
		yyerror(closure, "PHDRS syntax error");
	    }
    break;

  case 143:
/* Line 1787 of yacc.c  */
#line 793 "yyscript.y"
    {
	      (yyval.phdr_info) = (yyvsp[(2) - (2)].phdr_info);
	      (yyval.phdr_info).includes_phdrs = 1;
	    }
    break;

  case 144:
/* Line 1787 of yacc.c  */
#line 798 "yyscript.y"
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

  case 145:
/* Line 1787 of yacc.c  */
#line 809 "yyscript.y"
    {
	      (yyval.phdr_info) = (yyvsp[(5) - (5)].phdr_info);
	      (yyval.phdr_info).load_address = (yyvsp[(3) - (5)].expr);
	    }
    break;

  case 146:
/* Line 1787 of yacc.c  */
#line 818 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, (yyvsp[(3) - (3)].expr), 0, 0); }
    break;

  case 147:
/* Line 1787 of yacc.c  */
#line 820 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_add(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 148:
/* Line 1787 of yacc.c  */
#line 826 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_sub(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 149:
/* Line 1787 of yacc.c  */
#line 832 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_mult(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 150:
/* Line 1787 of yacc.c  */
#line 838 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_div(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 151:
/* Line 1787 of yacc.c  */
#line 844 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_lshift(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 152:
/* Line 1787 of yacc.c  */
#line 850 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_rshift(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 153:
/* Line 1787 of yacc.c  */
#line 856 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_bitwise_and(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 154:
/* Line 1787 of yacc.c  */
#line 862 "yyscript.y"
    {
	      Expression_ptr s = script_exp_string((yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length);
	      Expression_ptr e = script_exp_binary_bitwise_or(s, (yyvsp[(3) - (3)].expr));
	      script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, e, 0, 0);
	    }
    break;

  case 155:
/* Line 1787 of yacc.c  */
#line 868 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(3) - (6)].string).value, (yyvsp[(3) - (6)].string).length, (yyvsp[(5) - (6)].expr), 1, 0); }
    break;

  case 156:
/* Line 1787 of yacc.c  */
#line 870 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(3) - (6)].string).value, (yyvsp[(3) - (6)].string).length, (yyvsp[(5) - (6)].expr), 1, 1); }
    break;

  case 157:
/* Line 1787 of yacc.c  */
#line 875 "yyscript.y"
    { script_push_lex_into_expression_mode(closure); }
    break;

  case 158:
/* Line 1787 of yacc.c  */
#line 877 "yyscript.y"
    {
	      script_pop_lex_mode(closure);
	      (yyval.expr) = (yyvsp[(2) - (2)].expr);
	    }
    break;

  case 159:
/* Line 1787 of yacc.c  */
#line 886 "yyscript.y"
    { (yyval.expr) = (yyvsp[(2) - (3)].expr); }
    break;

  case 160:
/* Line 1787 of yacc.c  */
#line 888 "yyscript.y"
    { (yyval.expr) = script_exp_unary_minus((yyvsp[(2) - (2)].expr)); }
    break;

  case 161:
/* Line 1787 of yacc.c  */
#line 890 "yyscript.y"
    { (yyval.expr) = script_exp_unary_logical_not((yyvsp[(2) - (2)].expr)); }
    break;

  case 162:
/* Line 1787 of yacc.c  */
#line 892 "yyscript.y"
    { (yyval.expr) = script_exp_unary_bitwise_not((yyvsp[(2) - (2)].expr)); }
    break;

  case 163:
/* Line 1787 of yacc.c  */
#line 894 "yyscript.y"
    { (yyval.expr) = (yyvsp[(2) - (2)].expr); }
    break;

  case 164:
/* Line 1787 of yacc.c  */
#line 896 "yyscript.y"
    { (yyval.expr) = script_exp_binary_mult((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 165:
/* Line 1787 of yacc.c  */
#line 898 "yyscript.y"
    { (yyval.expr) = script_exp_binary_div((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 166:
/* Line 1787 of yacc.c  */
#line 900 "yyscript.y"
    { (yyval.expr) = script_exp_binary_mod((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 167:
/* Line 1787 of yacc.c  */
#line 902 "yyscript.y"
    { (yyval.expr) = script_exp_binary_add((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 168:
/* Line 1787 of yacc.c  */
#line 904 "yyscript.y"
    { (yyval.expr) = script_exp_binary_sub((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 169:
/* Line 1787 of yacc.c  */
#line 906 "yyscript.y"
    { (yyval.expr) = script_exp_binary_lshift((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 170:
/* Line 1787 of yacc.c  */
#line 908 "yyscript.y"
    { (yyval.expr) = script_exp_binary_rshift((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 171:
/* Line 1787 of yacc.c  */
#line 910 "yyscript.y"
    { (yyval.expr) = script_exp_binary_eq((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 172:
/* Line 1787 of yacc.c  */
#line 912 "yyscript.y"
    { (yyval.expr) = script_exp_binary_ne((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 173:
/* Line 1787 of yacc.c  */
#line 914 "yyscript.y"
    { (yyval.expr) = script_exp_binary_le((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 174:
/* Line 1787 of yacc.c  */
#line 916 "yyscript.y"
    { (yyval.expr) = script_exp_binary_ge((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 175:
/* Line 1787 of yacc.c  */
#line 918 "yyscript.y"
    { (yyval.expr) = script_exp_binary_lt((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 176:
/* Line 1787 of yacc.c  */
#line 920 "yyscript.y"
    { (yyval.expr) = script_exp_binary_gt((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 177:
/* Line 1787 of yacc.c  */
#line 922 "yyscript.y"
    { (yyval.expr) = script_exp_binary_bitwise_and((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 178:
/* Line 1787 of yacc.c  */
#line 924 "yyscript.y"
    { (yyval.expr) = script_exp_binary_bitwise_xor((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 179:
/* Line 1787 of yacc.c  */
#line 926 "yyscript.y"
    { (yyval.expr) = script_exp_binary_bitwise_or((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 180:
/* Line 1787 of yacc.c  */
#line 928 "yyscript.y"
    { (yyval.expr) = script_exp_binary_logical_and((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 181:
/* Line 1787 of yacc.c  */
#line 930 "yyscript.y"
    { (yyval.expr) = script_exp_binary_logical_or((yyvsp[(1) - (3)].expr), (yyvsp[(3) - (3)].expr)); }
    break;

  case 182:
/* Line 1787 of yacc.c  */
#line 932 "yyscript.y"
    { (yyval.expr) = script_exp_trinary_cond((yyvsp[(1) - (5)].expr), (yyvsp[(3) - (5)].expr), (yyvsp[(5) - (5)].expr)); }
    break;

  case 183:
/* Line 1787 of yacc.c  */
#line 934 "yyscript.y"
    { (yyval.expr) = script_exp_integer((yyvsp[(1) - (1)].integer)); }
    break;

  case 184:
/* Line 1787 of yacc.c  */
#line 936 "yyscript.y"
    { (yyval.expr) = script_symbol(closure, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length); }
    break;

  case 185:
/* Line 1787 of yacc.c  */
#line 938 "yyscript.y"
    { (yyval.expr) = script_exp_function_max((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr)); }
    break;

  case 186:
/* Line 1787 of yacc.c  */
#line 940 "yyscript.y"
    { (yyval.expr) = script_exp_function_min((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr)); }
    break;

  case 187:
/* Line 1787 of yacc.c  */
#line 942 "yyscript.y"
    { (yyval.expr) = script_exp_function_defined((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 188:
/* Line 1787 of yacc.c  */
#line 944 "yyscript.y"
    { (yyval.expr) = script_exp_function_sizeof_headers(); }
    break;

  case 189:
/* Line 1787 of yacc.c  */
#line 946 "yyscript.y"
    { (yyval.expr) = script_exp_function_alignof((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 190:
/* Line 1787 of yacc.c  */
#line 948 "yyscript.y"
    { (yyval.expr) = script_exp_function_sizeof((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 191:
/* Line 1787 of yacc.c  */
#line 950 "yyscript.y"
    { (yyval.expr) = script_exp_function_addr((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 192:
/* Line 1787 of yacc.c  */
#line 952 "yyscript.y"
    { (yyval.expr) = script_exp_function_loadaddr((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 193:
/* Line 1787 of yacc.c  */
#line 954 "yyscript.y"
    { (yyval.expr) = script_exp_function_origin(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 194:
/* Line 1787 of yacc.c  */
#line 956 "yyscript.y"
    { (yyval.expr) = script_exp_function_length(closure, (yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 195:
/* Line 1787 of yacc.c  */
#line 958 "yyscript.y"
    { (yyval.expr) = script_exp_function_constant((yyvsp[(3) - (4)].string).value, (yyvsp[(3) - (4)].string).length); }
    break;

  case 196:
/* Line 1787 of yacc.c  */
#line 960 "yyscript.y"
    { (yyval.expr) = script_exp_function_absolute((yyvsp[(3) - (4)].expr)); }
    break;

  case 197:
/* Line 1787 of yacc.c  */
#line 962 "yyscript.y"
    { (yyval.expr) = script_exp_function_align(script_exp_string(".", 1), (yyvsp[(3) - (4)].expr)); }
    break;

  case 198:
/* Line 1787 of yacc.c  */
#line 964 "yyscript.y"
    { (yyval.expr) = script_exp_function_align((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr)); }
    break;

  case 199:
/* Line 1787 of yacc.c  */
#line 966 "yyscript.y"
    { (yyval.expr) = script_exp_function_align(script_exp_string(".", 1), (yyvsp[(3) - (4)].expr)); }
    break;

  case 200:
/* Line 1787 of yacc.c  */
#line 968 "yyscript.y"
    {
	      script_data_segment_align(closure);
	      (yyval.expr) = script_exp_function_data_segment_align((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr));
	    }
    break;

  case 201:
/* Line 1787 of yacc.c  */
#line 973 "yyscript.y"
    {
	      script_data_segment_relro_end(closure);
	      (yyval.expr) = script_exp_function_data_segment_relro_end((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].expr));
	    }
    break;

  case 202:
/* Line 1787 of yacc.c  */
#line 978 "yyscript.y"
    { (yyval.expr) = script_exp_function_data_segment_end((yyvsp[(3) - (4)].expr)); }
    break;

  case 203:
/* Line 1787 of yacc.c  */
#line 980 "yyscript.y"
    {
	      (yyval.expr) = script_exp_function_segment_start((yyvsp[(3) - (6)].string).value, (yyvsp[(3) - (6)].string).length, (yyvsp[(5) - (6)].expr));
	      /* We need to take note of any SEGMENT_START expressions
		 because they change the behaviour of -Ttext, -Tdata and
		 -Tbss options.  */
	      script_saw_segment_start_expression(closure);
	    }
    break;

  case 204:
/* Line 1787 of yacc.c  */
#line 988 "yyscript.y"
    { (yyval.expr) = script_exp_function_assert((yyvsp[(3) - (6)].expr), (yyvsp[(5) - (6)].string).value, (yyvsp[(5) - (6)].string).length); }
    break;

  case 205:
/* Line 1787 of yacc.c  */
#line 994 "yyscript.y"
    { script_set_symbol(closure, (yyvsp[(1) - (3)].string).value, (yyvsp[(1) - (3)].string).length, (yyvsp[(3) - (3)].expr), 0, 0); }
    break;

  case 209:
/* Line 1787 of yacc.c  */
#line 1012 "yyscript.y"
    { script_new_vers_node (closure, NULL, (yyvsp[(2) - (5)].versyms)); }
    break;

  case 213:
/* Line 1787 of yacc.c  */
#line 1027 "yyscript.y"
    {
	      script_register_vers_node (closure, NULL, 0, (yyvsp[(2) - (4)].versnode), NULL);
	    }
    break;

  case 214:
/* Line 1787 of yacc.c  */
#line 1031 "yyscript.y"
    {
	      script_register_vers_node (closure, (yyvsp[(1) - (5)].string).value, (yyvsp[(1) - (5)].string).length, (yyvsp[(3) - (5)].versnode),
					 NULL);
	    }
    break;

  case 215:
/* Line 1787 of yacc.c  */
#line 1036 "yyscript.y"
    {
	      script_register_vers_node (closure, (yyvsp[(1) - (6)].string).value, (yyvsp[(1) - (6)].string).length, (yyvsp[(3) - (6)].versnode), (yyvsp[(5) - (6)].deplist));
	    }
    break;

  case 216:
/* Line 1787 of yacc.c  */
#line 1043 "yyscript.y"
    {
	      (yyval.deplist) = script_add_vers_depend (closure, NULL, (yyvsp[(1) - (1)].string).value, (yyvsp[(1) - (1)].string).length);
	    }
    break;

  case 217:
/* Line 1787 of yacc.c  */
#line 1047 "yyscript.y"
    {
	      (yyval.deplist) = script_add_vers_depend (closure, (yyvsp[(1) - (2)].deplist), (yyvsp[(2) - (2)].string).value, (yyvsp[(2) - (2)].string).length);
	    }
    break;

  case 218:
/* Line 1787 of yacc.c  */
#line 1054 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, NULL, NULL); }
    break;

  case 219:
/* Line 1787 of yacc.c  */
#line 1056 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, (yyvsp[(1) - (2)].versyms), NULL); }
    break;

  case 220:
/* Line 1787 of yacc.c  */
#line 1058 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, (yyvsp[(3) - (4)].versyms), NULL); }
    break;

  case 221:
/* Line 1787 of yacc.c  */
#line 1060 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, NULL, (yyvsp[(3) - (4)].versyms)); }
    break;

  case 222:
/* Line 1787 of yacc.c  */
#line 1062 "yyscript.y"
    { (yyval.versnode) = script_new_vers_node (closure, (yyvsp[(3) - (8)].versyms), (yyvsp[(7) - (8)].versyms)); }
    break;

  case 223:
/* Line 1787 of yacc.c  */
#line 1071 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, NULL, (yyvsp[(1) - (1)].string).value,
					    (yyvsp[(1) - (1)].string).length, 0);
	    }
    break;

  case 224:
/* Line 1787 of yacc.c  */
#line 1076 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, NULL, (yyvsp[(1) - (1)].string).value,
					    (yyvsp[(1) - (1)].string).length, 1);
	    }
    break;

  case 225:
/* Line 1787 of yacc.c  */
#line 1081 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, (yyvsp[(1) - (3)].versyms), (yyvsp[(3) - (3)].string).value,
                                            (yyvsp[(3) - (3)].string).length, 0);
	    }
    break;

  case 226:
/* Line 1787 of yacc.c  */
#line 1086 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, (yyvsp[(1) - (3)].versyms), (yyvsp[(3) - (3)].string).value,
                                            (yyvsp[(3) - (3)].string).length, 1);
	    }
    break;

  case 227:
/* Line 1787 of yacc.c  */
#line 1092 "yyscript.y"
    { version_script_push_lang (closure, (yyvsp[(2) - (3)].string).value, (yyvsp[(2) - (3)].string).length); }
    break;

  case 228:
/* Line 1787 of yacc.c  */
#line 1094 "yyscript.y"
    {
	      (yyval.versyms) = (yyvsp[(5) - (7)].versyms);
	      version_script_pop_lang(closure);
	    }
    break;

  case 229:
/* Line 1787 of yacc.c  */
#line 1102 "yyscript.y"
    { version_script_push_lang (closure, (yyvsp[(4) - (5)].string).value, (yyvsp[(4) - (5)].string).length); }
    break;

  case 230:
/* Line 1787 of yacc.c  */
#line 1104 "yyscript.y"
    {
	      (yyval.versyms) = script_merge_expressions ((yyvsp[(1) - (9)].versyms), (yyvsp[(7) - (9)].versyms));
	      version_script_pop_lang(closure);
	    }
    break;

  case 231:
/* Line 1787 of yacc.c  */
#line 1109 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, NULL, "extern",
					    sizeof("extern") - 1, 1);
	    }
    break;

  case 232:
/* Line 1787 of yacc.c  */
#line 1114 "yyscript.y"
    {
	      (yyval.versyms) = script_new_vers_pattern (closure, (yyvsp[(1) - (3)].versyms), "extern",
					    sizeof("extern") - 1, 1);
	    }
    break;

  case 233:
/* Line 1787 of yacc.c  */
#line 1124 "yyscript.y"
    { (yyval.string) = (yyvsp[(1) - (1)].string); }
    break;

  case 234:
/* Line 1787 of yacc.c  */
#line 1126 "yyscript.y"
    { (yyval.string) = (yyvsp[(1) - (1)].string); }
    break;


/* Line 1787 of yacc.c  */
#line 3641 "yyscript.c"
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
#line 1148 "yyscript.y"
