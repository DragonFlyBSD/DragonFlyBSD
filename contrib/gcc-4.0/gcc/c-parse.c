/* A Bison parser, made by GNU Bison 2.0.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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

/* Written by Richard Stallman by simplifying the original so called
   ``semantic'' parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     IDENTIFIER = 258,
     TYPENAME = 259,
     SCSPEC = 260,
     STATIC = 261,
     TYPESPEC = 262,
     TYPE_QUAL = 263,
     OBJC_TYPE_QUAL = 264,
     CONSTANT = 265,
     STRING = 266,
     ELLIPSIS = 267,
     SIZEOF = 268,
     ENUM = 269,
     STRUCT = 270,
     UNION = 271,
     IF = 272,
     ELSE = 273,
     WHILE = 274,
     DO = 275,
     FOR = 276,
     SWITCH = 277,
     CASE = 278,
     DEFAULT = 279,
     BREAK = 280,
     CONTINUE = 281,
     RETURN = 282,
     GOTO = 283,
     ASM_KEYWORD = 284,
     TYPEOF = 285,
     ALIGNOF = 286,
     ATTRIBUTE = 287,
     EXTENSION = 288,
     LABEL = 289,
     REALPART = 290,
     IMAGPART = 291,
     VA_ARG = 292,
     CHOOSE_EXPR = 293,
     TYPES_COMPATIBLE_P = 294,
     FUNC_NAME = 295,
     OFFSETOF = 296,
     ASSIGN = 297,
     OROR = 298,
     ANDAND = 299,
     EQCOMPARE = 300,
     ARITHCOMPARE = 301,
     RSHIFT = 302,
     LSHIFT = 303,
     MINUSMINUS = 304,
     PLUSPLUS = 305,
     UNARY = 306,
     HYPERUNARY = 307,
     POINTSAT = 308,
     AT_INTERFACE = 309,
     AT_IMPLEMENTATION = 310,
     AT_END = 311,
     AT_SELECTOR = 312,
     AT_DEFS = 313,
     AT_ENCODE = 314,
     CLASSNAME = 315,
     AT_PUBLIC = 316,
     AT_PRIVATE = 317,
     AT_PROTECTED = 318,
     AT_PROTOCOL = 319,
     AT_CLASS = 320,
     AT_ALIAS = 321,
     AT_THROW = 322,
     AT_TRY = 323,
     AT_CATCH = 324,
     AT_FINALLY = 325,
     AT_SYNCHRONIZED = 326,
     OBJC_STRING = 327
   };
#endif
#define IDENTIFIER 258
#define TYPENAME 259
#define SCSPEC 260
#define STATIC 261
#define TYPESPEC 262
#define TYPE_QUAL 263
#define OBJC_TYPE_QUAL 264
#define CONSTANT 265
#define STRING 266
#define ELLIPSIS 267
#define SIZEOF 268
#define ENUM 269
#define STRUCT 270
#define UNION 271
#define IF 272
#define ELSE 273
#define WHILE 274
#define DO 275
#define FOR 276
#define SWITCH 277
#define CASE 278
#define DEFAULT 279
#define BREAK 280
#define CONTINUE 281
#define RETURN 282
#define GOTO 283
#define ASM_KEYWORD 284
#define TYPEOF 285
#define ALIGNOF 286
#define ATTRIBUTE 287
#define EXTENSION 288
#define LABEL 289
#define REALPART 290
#define IMAGPART 291
#define VA_ARG 292
#define CHOOSE_EXPR 293
#define TYPES_COMPATIBLE_P 294
#define FUNC_NAME 295
#define OFFSETOF 296
#define ASSIGN 297
#define OROR 298
#define ANDAND 299
#define EQCOMPARE 300
#define ARITHCOMPARE 301
#define RSHIFT 302
#define LSHIFT 303
#define MINUSMINUS 304
#define PLUSPLUS 305
#define UNARY 306
#define HYPERUNARY 307
#define POINTSAT 308
#define AT_INTERFACE 309
#define AT_IMPLEMENTATION 310
#define AT_END 311
#define AT_SELECTOR 312
#define AT_DEFS 313
#define AT_ENCODE 314
#define CLASSNAME 315
#define AT_PUBLIC 316
#define AT_PRIVATE 317
#define AT_PROTECTED 318
#define AT_PROTOCOL 319
#define AT_CLASS 320
#define AT_ALIAS 321
#define AT_THROW 322
#define AT_TRY 323
#define AT_CATCH 324
#define AT_FINALLY 325
#define AT_SYNCHRONIZED 326
#define OBJC_STRING 327




/* Copy the first part of user declarations.  */
#line 34 "c-parse.y"

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "langhooks.h"
#include "input.h"
#include "cpplib.h"
#include "intl.h"
#include "timevar.h"
#include "c-pragma.h"		/* For YYDEBUG definition, and parse_in.  */
#include "c-tree.h"
#include "flags.h"
#include "varray.h"
#include "output.h"
#include "toplev.h"
#include "ggc.h"
#include "c-common.h"

#define YYERROR1 { yyerror ("syntax error"); YYERROR; }

/* Like the default stack expander, except (1) use realloc when possible,
   (2) impose no hard maxiumum on stack size, (3) REALLY do not use alloca.

   Irritatingly, YYSTYPE is defined after this %{ %} block, so we cannot
   give malloced_yyvs its proper type.  This is ok since all we need from
   it is to be able to free it.  */

static short *malloced_yyss;
static void *malloced_yyvs;

#define yyoverflow(MSG, SS, SSSIZE, VS, VSSIZE, YYSSZ)			\
do {									\
  size_t newsize;							\
  short *newss;								\
  YYSTYPE *newvs;							\
  newsize = *(YYSSZ) *= 2;						\
  if (malloced_yyss)							\
    {									\
      newss = really_call_realloc (*(SS), newsize * sizeof (short));	\
      newvs = really_call_realloc (*(VS), newsize * sizeof (YYSTYPE));	\
    }									\
  else									\
    {									\
      newss = really_call_malloc (newsize * sizeof (short));		\
      newvs = really_call_malloc (newsize * sizeof (YYSTYPE));		\
      if (newss)							\
        memcpy (newss, *(SS), (SSSIZE));				\
      if (newvs)							\
        memcpy (newvs, *(VS), (VSSIZE));				\
    }									\
  if (!newss || !newvs)							\
    {									\
      yyerror (MSG);							\
      return 2;								\
    }									\
  *(SS) = newss;							\
  *(VS) = newvs;							\
  malloced_yyss = newss;						\
  malloced_yyvs = (void *) newvs;					\
} while (0)


/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
#line 100 "c-parse.y"
typedef union YYSTYPE {long itype; tree ttype; void *otype; struct c_expr exprtype;
	struct c_arg_info *arginfotype; struct c_declarator *dtrtype;
	struct c_type_name *typenametype; struct c_parm *parmtype;
	struct c_declspecs *dsptype; struct c_typespec tstype;
	enum tree_code code; location_t location; } YYSTYPE;
/* Line 190 of yacc.c.  */
#line 290 "c-parse.c"
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */
#line 251 "c-parse.y"

/* Declaration specifiers of the current declaration.  */
static struct c_declspecs *current_declspecs;
static GTY(()) tree prefix_attributes;

/* List of all the attributes applying to the identifier currently being
   declared; includes prefix_attributes and possibly some more attributes
   just after a comma.  */
static GTY(()) tree all_prefix_attributes;

/* Structure to save declaration specifiers.  */
struct c_declspec_stack {
  /* Saved value of current_declspecs.  */
  struct c_declspecs *current_declspecs;
  /* Saved value of prefix_attributes.  */
  tree prefix_attributes;
  /* Saved value of all_prefix_attributes.  */
  tree all_prefix_attributes;
  /* Next level of stack.  */
  struct c_declspec_stack *next;
};

/* Stack of saved values of current_declspecs, prefix_attributes and
   all_prefix_attributes.  */
static struct c_declspec_stack *declspec_stack;

/* INDIRECT_REF with a TREE_TYPE of the type being queried for offsetof.  */
static tree offsetof_base;

/* PUSH_DECLSPEC_STACK is called from setspecs; POP_DECLSPEC_STACK
   should be called from the productions making use of setspecs.  */
#define PUSH_DECLSPEC_STACK						\
  do {									\
    struct c_declspec_stack *t = XOBNEW (&parser_obstack,		\
					 struct c_declspec_stack);	\
    t->current_declspecs = current_declspecs;				\
    t->prefix_attributes = prefix_attributes;				\
    t->all_prefix_attributes = all_prefix_attributes;			\
    t->next = declspec_stack;						\
    declspec_stack = t;							\
  } while (0)

#define POP_DECLSPEC_STACK						\
  do {									\
    current_declspecs = declspec_stack->current_declspecs;		\
    prefix_attributes = declspec_stack->prefix_attributes;		\
    all_prefix_attributes = declspec_stack->all_prefix_attributes;	\
    declspec_stack = declspec_stack->next;				\
  } while (0)

/* For __extension__, save/restore the warning flags which are
   controlled by __extension__.  */
#define SAVE_EXT_FLAGS()		\
	(pedantic			\
	 | (warn_pointer_arith << 1)	\
	 | (warn_traditional << 2)	\
	 | (flag_iso << 3))

#define RESTORE_EXT_FLAGS(val)			\
  do {						\
    pedantic = val & 1;				\
    warn_pointer_arith = (val >> 1) & 1;	\
    warn_traditional = (val >> 2) & 1;		\
    flag_iso = (val >> 3) & 1;			\
  } while (0)


#define OBJC_NEED_RAW_IDENTIFIER(VAL)	/* nothing */

/* Tell yyparse how to print a token's value, if yydebug is set.  */

#define YYPRINT(FILE,YYCHAR,YYLVAL) yyprint(FILE,YYCHAR,YYLVAL)

static void yyprint (FILE *, int, YYSTYPE);
static void yyerror (const char *);
static int yylexname (void);
static inline int _yylex (void);
static int  yylex (void);
static void init_reswords (void);

  /* Initialization routine for this file.  */
void
c_parse_init (void)
{
  init_reswords ();
}



/* Line 213 of yacc.c.  */
#line 390 "c-parse.c"

#if ! defined (yyoverflow) || YYERROR_VERBOSE

# ifndef YYFREE
#  define YYFREE free
# endif
# ifndef YYMALLOC
#  define YYMALLOC malloc
# endif

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   else
#    define YYSTACK_ALLOC alloca
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
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
# endif
#endif /* ! defined (yyoverflow) || YYERROR_VERBOSE */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (defined (YYSTYPE_IS_TRIVIAL) && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short int yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short int) + sizeof (YYSTYPE))			\
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined (__GNUC__) && 1 < __GNUC__
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
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (0)

#endif

#if defined (__STDC__) || defined (__cplusplus)
   typedef signed char yysigned_char;
#else
   typedef short int yysigned_char;
#endif

/* YYFINAL -- State number of the termination state. */
#define YYFINAL  4
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   3307

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  95
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  209
/* YYNRULES -- Number of rules. */
#define YYNRULES  574
/* YYNRULES -- Number of states. */
#define YYNSTATES  933

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   327

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    90,     2,     2,     2,    59,    50,     2,
      65,    92,    57,    55,    91,    56,    64,    58,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    45,    87,
       2,    42,     2,    44,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    66,     2,    94,    49,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    93,    48,    88,    89,     2,     2,     2,
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
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    43,    46,    47,
      51,    52,    53,    54,    60,    61,    62,    63,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short int yyprhs[] =
{
       0,     0,     3,     4,     6,     7,    11,    12,    17,    19,
      21,    23,    26,    27,    31,    36,    41,    44,    47,    50,
      52,    53,    54,    63,    68,    69,    70,    79,    84,    85,
      86,    94,    98,   100,   102,   104,   106,   108,   110,   112,
     114,   116,   118,   122,   123,   125,   127,   131,   133,   136,
     139,   142,   145,   148,   153,   156,   161,   164,   167,   169,
     171,   173,   175,   180,   182,   186,   190,   194,   198,   202,
     206,   210,   214,   218,   222,   226,   230,   231,   236,   237,
     242,   243,   244,   252,   253,   259,   263,   267,   269,   271,
     273,   275,   276,   284,   288,   292,   296,   300,   305,   312,
     313,   321,   326,   335,   340,   347,   352,   357,   361,   365,
     368,   371,   373,   377,   382,   383,   385,   388,   390,   392,
     395,   398,   403,   408,   411,   414,   417,   418,   420,   425,
     430,   434,   438,   441,   444,   446,   449,   452,   455,   458,
     461,   463,   466,   468,   471,   474,   477,   480,   483,   486,
     488,   491,   494,   497,   500,   503,   506,   509,   512,   515,
     518,   521,   524,   527,   530,   533,   536,   538,   541,   544,
     547,   550,   553,   556,   559,   562,   565,   568,   571,   574,
     577,   580,   583,   586,   589,   592,   595,   598,   601,   604,
     607,   610,   613,   616,   619,   622,   625,   628,   631,   634,
     637,   640,   643,   646,   649,   652,   655,   658,   661,   664,
     667,   670,   672,   674,   676,   678,   680,   682,   684,   686,
     688,   690,   692,   694,   696,   698,   700,   702,   704,   706,
     708,   710,   712,   714,   716,   718,   720,   722,   724,   726,
     728,   730,   732,   734,   736,   738,   740,   742,   744,   746,
     748,   750,   752,   754,   756,   758,   760,   762,   764,   766,
     768,   770,   772,   774,   776,   778,   780,   782,   783,   785,
     787,   789,   791,   793,   795,   797,   799,   804,   809,   811,
     816,   818,   823,   824,   831,   835,   836,   843,   847,   848,
     850,   852,   855,   864,   868,   870,   874,   875,   877,   882,
     889,   894,   896,   898,   900,   902,   904,   906,   908,   909,
     914,   916,   917,   920,   922,   926,   930,   933,   934,   939,
     941,   942,   947,   949,   951,   953,   956,   959,   961,   967,
     971,   972,   973,   980,   981,   982,   989,   991,   993,   998,
    1002,  1005,  1009,  1011,  1013,  1015,  1019,  1022,  1024,  1028,
    1031,  1035,  1039,  1044,  1048,  1053,  1057,  1060,  1062,  1064,
    1067,  1069,  1072,  1074,  1077,  1078,  1086,  1092,  1093,  1101,
    1107,  1108,  1117,  1118,  1126,  1129,  1132,  1135,  1136,  1138,
    1139,  1141,  1143,  1146,  1147,  1151,  1154,  1158,  1161,  1165,
    1167,  1169,  1172,  1174,  1179,  1181,  1186,  1189,  1194,  1198,
    1201,  1206,  1210,  1212,  1216,  1218,  1220,  1224,  1225,  1229,
    1230,  1232,  1233,  1235,  1238,  1240,  1242,  1244,  1248,  1251,
    1255,  1260,  1264,  1267,  1270,  1272,  1277,  1281,  1286,  1292,
    1298,  1300,  1302,  1304,  1306,  1308,  1311,  1314,  1317,  1320,
    1322,  1325,  1328,  1331,  1333,  1336,  1339,  1342,  1345,  1347,
    1350,  1352,  1354,  1356,  1358,  1361,  1362,  1363,  1365,  1367,
    1370,  1374,  1376,  1379,  1381,  1383,  1387,  1389,  1391,  1394,
    1397,  1398,  1399,  1402,  1406,  1409,  1412,  1415,  1419,  1423,
    1425,  1435,  1445,  1453,  1461,  1462,  1463,  1473,  1474,  1475,
    1489,  1490,  1492,  1495,  1497,  1500,  1502,  1515,  1516,  1525,
    1528,  1530,  1532,  1534,  1536,  1538,  1541,  1544,  1547,  1551,
    1553,  1557,  1562,  1564,  1566,  1568,  1572,  1578,  1581,  1586,
    1593,  1594,  1596,  1599,  1604,  1613,  1615,  1619,  1625,  1633,
    1634,  1636,  1637,  1639,  1641,  1645,  1652,  1662,  1664,  1668,
    1670,  1671,  1672,  1673,  1677,  1680,  1681,  1682,  1689,  1692,
    1693,  1695,  1697,  1701,  1703,  1707,  1712,  1717,  1721,  1726,
    1730,  1735,  1740,  1744,  1749,  1753,  1755,  1756,  1760,  1762,
    1765,  1767,  1771,  1773,  1777
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const short int yyrhs[] =
{
      96,     0,    -1,    -1,    97,    -1,    -1,   101,    98,   100,
      -1,    -1,    97,   101,    99,   100,    -1,   103,    -1,   102,
      -1,   277,    -1,   303,   100,    -1,    -1,   135,   169,    87,
      -1,   155,   135,   169,    87,    -1,   154,   135,   168,    87,
      -1,   161,    87,    -1,     1,    87,    -1,     1,    88,    -1,
      87,    -1,    -1,    -1,   154,   135,   198,   104,   130,   250,
     105,   244,    -1,   154,   135,   198,     1,    -1,    -1,    -1,
     155,   135,   203,   106,   130,   250,   107,   244,    -1,   155,
     135,   203,     1,    -1,    -1,    -1,   135,   203,   108,   130,
     250,   109,   244,    -1,   135,   203,     1,    -1,     3,    -1,
       4,    -1,    50,    -1,    56,    -1,    55,    -1,    61,    -1,
      60,    -1,    89,    -1,    90,    -1,   120,    -1,   112,    91,
     120,    -1,    -1,   114,    -1,   120,    -1,   114,    91,   120,
      -1,   126,    -1,    57,   119,    -1,   303,   119,    -1,   111,
     119,    -1,    47,   110,    -1,   116,   115,    -1,   116,    65,
     224,    92,    -1,   117,   115,    -1,   117,    65,   224,    92,
      -1,    35,   119,    -1,    36,   119,    -1,    13,    -1,    31,
      -1,    30,    -1,   115,    -1,    65,   224,    92,   119,    -1,
     119,    -1,   120,    55,   120,    -1,   120,    56,   120,    -1,
     120,    57,   120,    -1,   120,    58,   120,    -1,   120,    59,
     120,    -1,   120,    54,   120,    -1,   120,    53,   120,    -1,
     120,    52,   120,    -1,   120,    51,   120,    -1,   120,    50,
     120,    -1,   120,    48,   120,    -1,   120,    49,   120,    -1,
      -1,   120,    47,   121,   120,    -1,    -1,   120,    46,   122,
     120,    -1,    -1,    -1,   120,    44,   123,   112,    45,   124,
     120,    -1,    -1,   120,    44,   125,    45,   120,    -1,   120,
      42,   120,    -1,   120,    43,   120,    -1,     3,    -1,    10,
      -1,    11,    -1,    40,    -1,    -1,    65,   224,    92,    93,
     127,   183,    88,    -1,    65,   112,    92,    -1,    65,     1,
      92,    -1,   248,   246,    92,    -1,   248,     1,    92,    -1,
     126,    65,   113,    92,    -1,    37,    65,   120,    91,   224,
      92,    -1,    -1,    41,    65,   224,    91,   128,   129,    92,
      -1,    41,    65,     1,    92,    -1,    38,    65,   120,    91,
     120,    91,   120,    92,    -1,    38,    65,     1,    92,    -1,
      39,    65,   224,    91,   224,    92,    -1,    39,    65,     1,
      92,    -1,   126,    66,   112,    94,    -1,   126,    64,   110,
      -1,   126,    67,   110,    -1,   126,    61,    -1,   126,    60,
      -1,   110,    -1,   129,    64,   110,    -1,   129,    66,   112,
      94,    -1,    -1,   132,    -1,   250,   133,    -1,   131,    -1,
     239,    -1,   132,   131,    -1,   131,   239,    -1,   156,   135,
     168,    87,    -1,   157,   135,   169,    87,    -1,   156,    87,
      -1,   157,    87,    -1,   250,   137,    -1,    -1,   174,    -1,
     154,   135,   168,    87,    -1,   155,   135,   169,    87,    -1,
     154,   135,   192,    -1,   155,   135,   195,    -1,   161,    87,
      -1,   303,   137,    -1,     8,    -1,   138,     8,    -1,   139,
       8,    -1,   138,   175,    -1,   140,     8,    -1,   141,     8,
      -1,   175,    -1,   140,   175,    -1,   163,    -1,   142,     8,
      -1,   143,     8,    -1,   142,   165,    -1,   143,   165,    -1,
     138,   163,    -1,   139,   163,    -1,   164,    -1,   142,   175,
      -1,   142,   166,    -1,   143,   166,    -1,   138,   164,    -1,
     139,   164,    -1,   144,     8,    -1,   145,     8,    -1,   144,
     165,    -1,   145,   165,    -1,   140,   163,    -1,   141,   163,
      -1,   144,   175,    -1,   144,   166,    -1,   145,   166,    -1,
     140,   164,    -1,   141,   164,    -1,   180,    -1,   146,     8,
      -1,   147,     8,    -1,   138,   180,    -1,   139,   180,    -1,
     146,   180,    -1,   147,   180,    -1,   146,   175,    -1,   148,
       8,    -1,   149,     8,    -1,   140,   180,    -1,   141,   180,
      -1,   148,   180,    -1,   149,   180,    -1,   148,   175,    -1,
     150,     8,    -1,   151,     8,    -1,   150,   165,    -1,   151,
     165,    -1,   146,   163,    -1,   147,   163,    -1,   142,   180,
      -1,   143,   180,    -1,   150,   180,    -1,   151,   180,    -1,
     150,   175,    -1,   150,   166,    -1,   151,   166,    -1,   146,
     164,    -1,   147,   164,    -1,   152,     8,    -1,   153,     8,
      -1,   152,   165,    -1,   153,   165,    -1,   148,   163,    -1,
     149,   163,    -1,   144,   180,    -1,   145,   180,    -1,   152,
     180,    -1,   153,   180,    -1,   152,   175,    -1,   152,   166,
      -1,   153,   166,    -1,   148,   164,    -1,   149,   164,    -1,
     142,    -1,   143,    -1,   144,    -1,   145,    -1,   150,    -1,
     151,    -1,   152,    -1,   153,    -1,   138,    -1,   139,    -1,
     140,    -1,   141,    -1,   146,    -1,   147,    -1,   148,    -1,
     149,    -1,   142,    -1,   143,    -1,   150,    -1,   151,    -1,
     138,    -1,   139,    -1,   146,    -1,   147,    -1,   142,    -1,
     143,    -1,   144,    -1,   145,    -1,   138,    -1,   139,    -1,
     140,    -1,   141,    -1,   142,    -1,   143,    -1,   144,    -1,
     145,    -1,   138,    -1,   139,    -1,   140,    -1,   141,    -1,
     138,    -1,   139,    -1,   140,    -1,   141,    -1,   142,    -1,
     143,    -1,   144,    -1,   145,    -1,   146,    -1,   147,    -1,
     148,    -1,   149,    -1,   150,    -1,   151,    -1,   152,    -1,
     153,    -1,    -1,   159,    -1,   165,    -1,   167,    -1,   166,
      -1,     7,    -1,   212,    -1,   207,    -1,     4,    -1,   118,
      65,   112,    92,    -1,   118,    65,   224,    92,    -1,   170,
      -1,   168,    91,   136,   170,    -1,   172,    -1,   169,    91,
     136,   172,    -1,    -1,   198,   276,   174,    42,   171,   181,
      -1,   198,   276,   174,    -1,    -1,   203,   276,   174,    42,
     173,   181,    -1,   203,   276,   174,    -1,    -1,   175,    -1,
     176,    -1,   175,   176,    -1,    32,   286,    65,    65,   177,
      92,    92,   287,    -1,    32,     1,   287,    -1,   178,    -1,
     177,    91,   178,    -1,    -1,   179,    -1,   179,    65,     3,
      92,    -1,   179,    65,     3,    91,   114,    92,    -1,   179,
      65,   113,    92,    -1,   110,    -1,   180,    -1,     7,    -1,
       8,    -1,     6,    -1,     5,    -1,   120,    -1,    -1,    93,
     182,   183,    88,    -1,     1,    -1,    -1,   184,   213,    -1,
     185,    -1,   184,    91,   185,    -1,   189,    42,   187,    -1,
     191,   187,    -1,    -1,   110,    45,   186,   187,    -1,   187,
      -1,    -1,    93,   188,   183,    88,    -1,   120,    -1,     1,
      -1,   190,    -1,   189,   190,    -1,    64,   110,    -1,   191,
      -1,    66,   120,    12,   120,    94,    -1,    66,   120,    94,
      -1,    -1,    -1,   198,   193,   130,   250,   194,   249,    -1,
      -1,    -1,   203,   196,   130,   250,   197,   249,    -1,   199,
      -1,   203,    -1,    65,   174,   199,    92,    -1,   199,    65,
     298,    -1,   199,   232,    -1,    57,   162,   199,    -1,     4,
      -1,   201,    -1,   202,    -1,   201,    65,   298,    -1,   201,
     232,    -1,     4,    -1,   202,    65,   298,    -1,   202,   232,
      -1,    57,   162,   201,    -1,    57,   162,   202,    -1,    65,
     174,   202,    92,    -1,   203,    65,   298,    -1,    65,   174,
     203,    92,    -1,    57,   162,   203,    -1,   203,   232,    -1,
       3,    -1,    15,    -1,    15,   175,    -1,    16,    -1,    16,
     175,    -1,    14,    -1,    14,   175,    -1,    -1,   204,   110,
      93,   208,   215,    88,   174,    -1,   204,    93,   215,    88,
     174,    -1,    -1,   205,   110,    93,   209,   215,    88,   174,
      -1,   205,    93,   215,    88,   174,    -1,    -1,   206,   110,
      93,   210,   222,   214,    88,   174,    -1,    -1,   206,    93,
     211,   222,   214,    88,   174,    -1,   204,   110,    -1,   205,
     110,    -1,   206,   110,    -1,    -1,    91,    -1,    -1,    91,
      -1,   216,    -1,   216,   217,    -1,    -1,   216,   217,    87,
      -1,   216,    87,    -1,   158,   135,   218,    -1,   158,   135,
      -1,   159,   135,   219,    -1,   159,    -1,     1,    -1,   303,
     217,    -1,   220,    -1,   218,    91,   136,   220,    -1,   221,
      -1,   219,    91,   136,   221,    -1,   198,   174,    -1,   198,
      45,   120,   174,    -1,    45,   120,   174,    -1,   203,   174,
      -1,   203,    45,   120,   174,    -1,    45,   120,   174,    -1,
     223,    -1,   222,    91,   223,    -1,     1,    -1,   110,    -1,
     110,    42,   120,    -1,    -1,   160,   225,   226,    -1,    -1,
     228,    -1,    -1,   228,    -1,   229,   175,    -1,   230,    -1,
     229,    -1,   231,    -1,    57,   162,   229,    -1,    57,   162,
      -1,    57,   162,   230,    -1,    65,   174,   228,    92,    -1,
     231,    65,   288,    -1,   231,   232,    -1,    65,   288,    -1,
     232,    -1,    66,   162,   120,    94,    -1,    66,   162,    94,
      -1,    66,   162,    57,    94,    -1,    66,     6,   162,   120,
      94,    -1,    66,   159,     6,   120,    94,    -1,   234,    -1,
     235,    -1,   236,    -1,   237,    -1,   253,    -1,   234,   253,
      -1,   235,   253,    -1,   236,   253,    -1,   237,   253,    -1,
     134,    -1,   234,   134,    -1,   235,   134,    -1,   237,   134,
      -1,   254,    -1,   234,   254,    -1,   235,   254,    -1,   236,
     254,    -1,   237,   254,    -1,   239,    -1,   238,   239,    -1,
     234,    -1,   235,    -1,   236,    -1,   237,    -1,     1,    87,
      -1,    -1,    -1,   242,    -1,   243,    -1,   242,   243,    -1,
      34,   302,    87,    -1,   249,    -1,     1,   249,    -1,    93,
      -1,    88,    -1,   241,   247,    88,    -1,   233,    -1,     1,
      -1,    65,    93,    -1,   245,   246,    -1,    -1,    -1,   251,
     254,    -1,   240,   251,   253,    -1,   250,   273,    -1,   250,
     274,    -1,   250,   112,    -1,   240,   251,   258,    -1,   240,
     251,    87,    -1,   252,    -1,    17,   240,   250,    65,   255,
      92,   256,    18,   257,    -1,    17,   240,   250,    65,   255,
      92,   257,    18,   257,    -1,    17,   240,   250,    65,   255,
      92,   256,    -1,    17,   240,   250,    65,   255,    92,   257,
      -1,    -1,    -1,    19,   240,   250,    65,   255,    92,   259,
     260,   252,    -1,    -1,    -1,    20,   240,   250,   259,   260,
     252,    19,   263,   264,    65,   255,    92,    87,    -1,    -1,
     112,    -1,   265,    87,    -1,   137,    -1,   250,   265,    -1,
     265,    -1,    21,   240,    65,   266,   250,   267,    87,   268,
      92,   259,   260,   252,    -1,    -1,    22,   240,    65,   112,
      92,   271,   259,   252,    -1,   112,    87,    -1,   258,    -1,
     261,    -1,   262,    -1,   269,    -1,   270,    -1,    25,    87,
      -1,    26,    87,    -1,    27,    87,    -1,    27,   112,    87,
      -1,   278,    -1,    28,   110,    87,    -1,    28,    57,   112,
      87,    -1,    87,    -1,   249,    -1,   272,    -1,    23,   120,
      45,    -1,    23,   120,    12,   120,    45,    -1,    24,    45,
      -1,   110,   250,    45,   174,    -1,    29,   286,    65,   285,
      92,   287,    -1,    -1,   275,    -1,   275,    87,    -1,    29,
       1,   287,    87,    -1,    29,   280,   286,    65,   279,    92,
     287,    87,    -1,   285,    -1,   285,    45,   281,    -1,   285,
      45,   281,    45,   281,    -1,   285,    45,   281,    45,   281,
      45,   284,    -1,    -1,     8,    -1,    -1,   282,    -1,   283,
      -1,   282,    91,   283,    -1,   285,   287,    65,   112,    92,
     286,    -1,    66,   110,    94,   285,   287,    65,   112,    92,
     286,    -1,   285,    -1,   284,    91,   285,    -1,    11,    -1,
      -1,    -1,    -1,   174,   289,   290,    -1,   293,    92,    -1,
      -1,    -1,   294,    87,   291,   174,   292,   290,    -1,     1,
      92,    -1,    -1,    12,    -1,   294,    -1,   294,    91,    12,
      -1,   296,    -1,   294,    91,   295,    -1,   154,   135,   200,
     174,    -1,   154,   135,   203,   174,    -1,   154,   135,   227,
      -1,   155,   135,   203,   174,    -1,   155,   135,   227,    -1,
     156,   297,   200,   174,    -1,   156,   297,   203,   174,    -1,
     156,   297,   227,    -1,   157,   297,   203,   174,    -1,   157,
     297,   227,    -1,   135,    -1,    -1,   174,   299,   300,    -1,
     290,    -1,   301,    92,    -1,     3,    -1,   301,    91,     3,
      -1,   110,    -1,   302,    91,   110,    -1,    33,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,   342,   342,   345,   353,   353,   356,   355,   361,   362,
     363,   364,   371,   375,   378,   380,   382,   384,   385,   386,
     393,   398,   392,   403,   406,   411,   405,   416,   419,   424,
     418,   429,   434,   435,   438,   440,   442,   447,   449,   451,
     453,   457,   458,   465,   466,   470,   472,   477,   478,   482,
     485,   490,   493,   500,   504,   509,   514,   517,   523,   527,
     531,   535,   536,   542,   543,   545,   547,   549,   551,   553,
     555,   557,   559,   561,   563,   565,   568,   567,   575,   574,
     582,   586,   581,   594,   593,   605,   609,   617,   624,   626,
     628,   632,   631,   652,   657,   659,   665,   670,   673,   678,
     677,   687,   689,   701,   703,   715,   717,   720,   723,   729,
     732,   742,   744,   746,   750,   752,   759,   764,   765,   766,
     767,   775,   777,   779,   782,   791,   800,   820,   825,   827,
     829,   831,   833,   835,   881,   883,   885,   890,   895,   897,
     902,   904,   909,   911,   913,   915,   917,   919,   921,   926,
     928,   930,   932,   934,   936,   941,   943,   945,   947,   949,
     951,   956,   958,   960,   962,   964,   969,   971,   973,   975,
     977,   979,   981,   986,   991,   993,   995,   997,   999,  1001,
    1006,  1011,  1013,  1015,  1017,  1019,  1021,  1023,  1025,  1027,
    1029,  1034,  1036,  1038,  1040,  1042,  1047,  1049,  1051,  1053,
    1055,  1057,  1059,  1061,  1063,  1065,  1070,  1072,  1074,  1076,
    1078,  1084,  1085,  1086,  1087,  1088,  1089,  1090,  1091,  1095,
    1096,  1097,  1098,  1099,  1100,  1101,  1102,  1106,  1107,  1108,
    1109,  1113,  1114,  1115,  1116,  1120,  1121,  1122,  1123,  1127,
    1128,  1129,  1130,  1134,  1135,  1136,  1137,  1138,  1139,  1140,
    1141,  1145,  1146,  1147,  1148,  1149,  1150,  1151,  1152,  1153,
    1154,  1155,  1156,  1157,  1158,  1159,  1160,  1166,  1167,  1193,
    1194,  1198,  1202,  1206,  1210,  1214,  1219,  1229,  1241,  1242,
    1246,  1247,  1252,  1251,  1266,  1276,  1275,  1290,  1300,  1301,
    1306,  1308,  1313,  1316,  1321,  1323,  1329,  1330,  1332,  1334,
    1336,  1344,  1345,  1346,  1347,  1351,  1352,  1358,  1361,  1360,
    1364,  1371,  1373,  1377,  1378,  1384,  1387,  1391,  1390,  1396,
    1401,  1400,  1404,  1406,  1410,  1411,  1415,  1417,  1421,  1425,
    1431,  1443,  1430,  1461,  1473,  1460,  1493,  1494,  1500,  1502,
    1504,  1506,  1508,  1517,  1518,  1522,  1524,  1526,  1531,  1533,
    1535,  1537,  1539,  1547,  1549,  1551,  1553,  1555,  1560,  1562,
    1567,  1569,  1574,  1576,  1588,  1587,  1595,  1602,  1601,  1607,
    1614,  1613,  1620,  1619,  1628,  1630,  1632,  1640,  1642,  1645,
    1647,  1665,  1667,  1673,  1674,  1676,  1682,  1685,  1693,  1696,
    1701,  1703,  1709,  1710,  1715,  1716,  1721,  1725,  1729,  1737,
    1741,  1745,  1756,  1757,  1762,  1768,  1770,  1776,  1775,  1786,
    1787,  1792,  1794,  1797,  1804,  1805,  1809,  1810,  1815,  1818,
    1823,  1825,  1827,  1829,  1832,  1840,  1842,  1844,  1846,  1849,
    1860,  1861,  1862,  1866,  1870,  1871,  1872,  1873,  1874,  1878,
    1879,  1885,  1886,  1890,  1891,  1892,  1893,  1894,  1898,  1899,
    1903,  1904,  1905,  1906,  1909,  1914,  1919,  1921,  1927,  1928,
    1932,  1946,  1948,  1951,  1954,  1955,  1959,  1960,  1964,  1975,
    1984,  1989,  1991,  1996,  2001,  2019,  2023,  2036,  2041,  2045,
    2049,  2053,  2057,  2061,  2068,  2072,  2076,  2087,  2088,  2085,
    2097,  2098,  2103,  2105,  2109,  2121,  2126,  2137,  2136,  2149,
    2151,  2153,  2155,  2157,  2159,  2161,  2163,  2165,  2167,  2169,
    2170,  2172,  2174,  2180,  2182,  2189,  2191,  2193,  2195,  2212,
    2220,  2221,  2226,  2228,  2235,  2242,  2245,  2248,  2251,  2259,
    2260,  2274,  2275,  2279,  2280,  2285,  2289,  2297,  2299,  2305,
    2317,  2321,  2332,  2331,  2340,  2342,  2344,  2341,  2348,  2359,
    2364,  2373,  2375,  2380,  2382,  2389,  2393,  2397,  2400,  2405,
    2413,  2417,  2421,  2424,  2429,  2435,  2445,  2444,  2453,  2454,
    2469,  2471,  2477,  2479,  2484
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "IDENTIFIER", "TYPENAME", "SCSPEC",
  "STATIC", "TYPESPEC", "TYPE_QUAL", "OBJC_TYPE_QUAL", "CONSTANT",
  "STRING", "ELLIPSIS", "SIZEOF", "ENUM", "STRUCT", "UNION", "IF", "ELSE",
  "WHILE", "DO", "FOR", "SWITCH", "CASE", "DEFAULT", "BREAK", "CONTINUE",
  "RETURN", "GOTO", "ASM_KEYWORD", "TYPEOF", "ALIGNOF", "ATTRIBUTE",
  "EXTENSION", "LABEL", "REALPART", "IMAGPART", "VA_ARG", "CHOOSE_EXPR",
  "TYPES_COMPATIBLE_P", "FUNC_NAME", "OFFSETOF", "'='", "ASSIGN", "'?'",
  "':'", "OROR", "ANDAND", "'|'", "'^'", "'&'", "EQCOMPARE",
  "ARITHCOMPARE", "RSHIFT", "LSHIFT", "'+'", "'-'", "'*'", "'/'", "'%'",
  "MINUSMINUS", "PLUSPLUS", "UNARY", "HYPERUNARY", "'.'", "'('", "'['",
  "POINTSAT", "AT_INTERFACE", "AT_IMPLEMENTATION", "AT_END", "AT_SELECTOR",
  "AT_DEFS", "AT_ENCODE", "CLASSNAME", "AT_PUBLIC", "AT_PRIVATE",
  "AT_PROTECTED", "AT_PROTOCOL", "AT_CLASS", "AT_ALIAS", "AT_THROW",
  "AT_TRY", "AT_CATCH", "AT_FINALLY", "AT_SYNCHRONIZED", "OBJC_STRING",
  "';'", "'}'", "'~'", "'!'", "','", "')'", "'{'", "']'", "$accept",
  "program", "extdefs", "@1", "@2", "extdef", "save_obstack_position",
  "datadef", "fndef", "@3", "@4", "@5", "@6", "@7", "@8", "identifier",
  "unop", "expr", "exprlist", "nonnull_exprlist", "unary_expr", "sizeof",
  "alignof", "typeof", "cast_expr", "expr_no_commas", "@9", "@10", "@11",
  "@12", "@13", "primary", "@14", "@15", "offsetof_member_designator",
  "old_style_parm_decls", "lineno_datadecl", "datadecls", "datadecl",
  "lineno_decl", "setspecs", "maybe_resetattrs", "decl",
  "declspecs_nosc_nots_nosa_noea", "declspecs_nosc_nots_nosa_ea",
  "declspecs_nosc_nots_sa_noea", "declspecs_nosc_nots_sa_ea",
  "declspecs_nosc_ts_nosa_noea", "declspecs_nosc_ts_nosa_ea",
  "declspecs_nosc_ts_sa_noea", "declspecs_nosc_ts_sa_ea",
  "declspecs_sc_nots_nosa_noea", "declspecs_sc_nots_nosa_ea",
  "declspecs_sc_nots_sa_noea", "declspecs_sc_nots_sa_ea",
  "declspecs_sc_ts_nosa_noea", "declspecs_sc_ts_nosa_ea",
  "declspecs_sc_ts_sa_noea", "declspecs_sc_ts_sa_ea", "declspecs_ts",
  "declspecs_nots", "declspecs_ts_nosa", "declspecs_nots_nosa",
  "declspecs_nosc_ts", "declspecs_nosc_nots", "declspecs_nosc",
  "declspecs", "maybe_type_quals_attrs", "typespec_nonattr",
  "typespec_attr", "typespec_reserved_nonattr", "typespec_reserved_attr",
  "typespec_nonreserved_nonattr", "initdecls", "notype_initdecls",
  "initdcl", "@16", "notype_initdcl", "@17", "maybe_attribute",
  "attributes", "attribute", "attribute_list", "attrib", "any_word",
  "scspec", "init", "@18", "initlist_maybe_comma", "initlist1", "initelt",
  "@19", "initval", "@20", "designator_list", "designator",
  "array_designator", "nested_function", "@21", "@22",
  "notype_nested_function", "@23", "@24", "declarator",
  "after_type_declarator", "parm_declarator",
  "parm_declarator_starttypename", "parm_declarator_nostarttypename",
  "notype_declarator", "struct_head", "union_head", "enum_head",
  "structsp_attr", "@25", "@26", "@27", "@28", "structsp_nonattr",
  "maybecomma", "maybecomma_warn", "component_decl_list",
  "component_decl_list2", "component_decl", "components",
  "components_notype", "component_declarator",
  "component_notype_declarator", "enumlist", "enumerator", "typename",
  "@29", "absdcl", "absdcl_maybe_attribute", "absdcl1", "absdcl1_noea",
  "absdcl1_ea", "direct_absdcl1", "array_declarator", "stmts_and_decls",
  "lineno_stmt_decl_or_labels_ending_stmt",
  "lineno_stmt_decl_or_labels_ending_decl",
  "lineno_stmt_decl_or_labels_ending_label",
  "lineno_stmt_decl_or_labels_ending_error", "lineno_stmt_decl_or_labels",
  "errstmt", "c99_block_start", "maybe_label_decls", "label_decls",
  "label_decl", "compstmt_or_error", "compstmt_start", "compstmt_nostart",
  "compstmt_contents_nonempty", "compstmt_primary_start", "compstmt",
  "save_location", "lineno_labels", "c99_block_lineno_labeled_stmt",
  "lineno_stmt", "lineno_label", "condition", "if_statement_1",
  "if_statement_2", "if_statement", "start_break", "start_continue",
  "while_statement", "do_statement", "@30", "@31", "xexpr",
  "for_init_stmt", "for_cond_expr", "for_incr_expr", "for_statement",
  "switch_statement", "@32", "stmt_nocomp", "stmt", "label",
  "simple_asm_expr", "maybeasm", "asmdef", "asm_stmt", "asm_argument",
  "maybe_volatile", "asm_operands", "nonnull_asm_operands", "asm_operand",
  "asm_clobbers", "asm_string", "stop_string_translation",
  "start_string_translation", "parmlist", "@33", "parmlist_1", "@34",
  "@35", "parmlist_2", "parms", "parm", "firstparm", "setspecs_fp",
  "parmlist_or_identifiers", "@36", "parmlist_or_identifiers_1",
  "identifiers", "identifiers_or_typenames", "extension", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short int yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,    61,   297,    63,    58,   298,   299,   124,    94,
      38,   300,   301,   302,   303,    43,    45,    42,    47,    37,
     304,   305,   306,   307,    46,    40,    91,   308,   309,   310,
     311,   312,   313,   314,   315,   316,   317,   318,   319,   320,
     321,   322,   323,   324,   325,   326,   327,    59,   125,   126,
      33,    44,    41,   123,    93
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned short int yyr1[] =
{
       0,    95,    96,    96,    98,    97,    99,    97,   100,   100,
     100,   100,   101,   102,   102,   102,   102,   102,   102,   102,
     104,   105,   103,   103,   106,   107,   103,   103,   108,   109,
     103,   103,   110,   110,   111,   111,   111,   111,   111,   111,
     111,   112,   112,   113,   113,   114,   114,   115,   115,   115,
     115,   115,   115,   115,   115,   115,   115,   115,   116,   117,
     118,   119,   119,   120,   120,   120,   120,   120,   120,   120,
     120,   120,   120,   120,   120,   120,   121,   120,   122,   120,
     123,   124,   120,   125,   120,   120,   120,   126,   126,   126,
     126,   127,   126,   126,   126,   126,   126,   126,   126,   128,
     126,   126,   126,   126,   126,   126,   126,   126,   126,   126,
     126,   129,   129,   129,   130,   130,   131,   132,   132,   132,
     132,   133,   133,   133,   133,   134,   135,   136,   137,   137,
     137,   137,   137,   137,   138,   138,   138,   139,   140,   140,
     141,   141,   142,   142,   142,   142,   142,   142,   142,   143,
     143,   143,   143,   143,   143,   144,   144,   144,   144,   144,
     144,   145,   145,   145,   145,   145,   146,   146,   146,   146,
     146,   146,   146,   147,   148,   148,   148,   148,   148,   148,
     149,   150,   150,   150,   150,   150,   150,   150,   150,   150,
     150,   151,   151,   151,   151,   151,   152,   152,   152,   152,
     152,   152,   152,   152,   152,   152,   153,   153,   153,   153,
     153,   154,   154,   154,   154,   154,   154,   154,   154,   155,
     155,   155,   155,   155,   155,   155,   155,   156,   156,   156,
     156,   157,   157,   157,   157,   158,   158,   158,   158,   159,
     159,   159,   159,   160,   160,   160,   160,   160,   160,   160,
     160,   161,   161,   161,   161,   161,   161,   161,   161,   161,
     161,   161,   161,   161,   161,   161,   161,   162,   162,   163,
     163,   164,   165,   165,   166,   167,   167,   167,   168,   168,
     169,   169,   171,   170,   170,   173,   172,   172,   174,   174,
     175,   175,   176,   176,   177,   177,   178,   178,   178,   178,
     178,   179,   179,   179,   179,   180,   180,   181,   182,   181,
     181,   183,   183,   184,   184,   185,   185,   186,   185,   185,
     188,   187,   187,   187,   189,   189,   190,   190,   191,   191,
     193,   194,   192,   196,   197,   195,   198,   198,   199,   199,
     199,   199,   199,   200,   200,   201,   201,   201,   202,   202,
     202,   202,   202,   203,   203,   203,   203,   203,   204,   204,
     205,   205,   206,   206,   208,   207,   207,   209,   207,   207,
     210,   207,   211,   207,   212,   212,   212,   213,   213,   214,
     214,   215,   215,   216,   216,   216,   217,   217,   217,   217,
     217,   217,   218,   218,   219,   219,   220,   220,   220,   221,
     221,   221,   222,   222,   222,   223,   223,   225,   224,   226,
     226,   227,   227,   227,   228,   228,   229,   229,   230,   230,
     231,   231,   231,   231,   231,   232,   232,   232,   232,   232,
     233,   233,   233,   233,   234,   234,   234,   234,   234,   235,
     235,   235,   235,   236,   236,   236,   236,   236,   237,   237,
     238,   238,   238,   238,   239,   240,   241,   241,   242,   242,
     243,   244,   244,   245,   246,   246,   247,   247,   248,   249,
     250,   251,   251,   252,   253,   254,   255,   256,   257,   257,
     258,   258,   258,   258,   259,   260,   261,   263,   264,   262,
     265,   265,   266,   266,   267,   268,   269,   271,   270,   272,
     272,   272,   272,   272,   272,   272,   272,   272,   272,   272,
     272,   272,   272,   273,   273,   274,   274,   274,   274,   275,
     276,   276,   277,   277,   278,   279,   279,   279,   279,   280,
     280,   281,   281,   282,   282,   283,   283,   284,   284,   285,
     286,   287,   289,   288,   290,   291,   292,   290,   290,   293,
     293,   293,   293,   294,   294,   295,   295,   295,   295,   295,
     296,   296,   296,   296,   296,   297,   299,   298,   300,   300,
     301,   301,   302,   302,   303
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     0,     1,     0,     3,     0,     4,     1,     1,
       1,     2,     0,     3,     4,     4,     2,     2,     2,     1,
       0,     0,     8,     4,     0,     0,     8,     4,     0,     0,
       7,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     0,     1,     1,     3,     1,     2,     2,
       2,     2,     2,     4,     2,     4,     2,     2,     1,     1,
       1,     1,     4,     1,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     0,     4,     0,     4,
       0,     0,     7,     0,     5,     3,     3,     1,     1,     1,
       1,     0,     7,     3,     3,     3,     3,     4,     6,     0,
       7,     4,     8,     4,     6,     4,     4,     3,     3,     2,
       2,     1,     3,     4,     0,     1,     2,     1,     1,     2,
       2,     4,     4,     2,     2,     2,     0,     1,     4,     4,
       3,     3,     2,     2,     1,     2,     2,     2,     2,     2,
       1,     2,     1,     2,     2,     2,     2,     2,     2,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     0,     1,     1,
       1,     1,     1,     1,     1,     1,     4,     4,     1,     4,
       1,     4,     0,     6,     3,     0,     6,     3,     0,     1,
       1,     2,     8,     3,     1,     3,     0,     1,     4,     6,
       4,     1,     1,     1,     1,     1,     1,     1,     0,     4,
       1,     0,     2,     1,     3,     3,     2,     0,     4,     1,
       0,     4,     1,     1,     1,     2,     2,     1,     5,     3,
       0,     0,     6,     0,     0,     6,     1,     1,     4,     3,
       2,     3,     1,     1,     1,     3,     2,     1,     3,     2,
       3,     3,     4,     3,     4,     3,     2,     1,     1,     2,
       1,     2,     1,     2,     0,     7,     5,     0,     7,     5,
       0,     8,     0,     7,     2,     2,     2,     0,     1,     0,
       1,     1,     2,     0,     3,     2,     3,     2,     3,     1,
       1,     2,     1,     4,     1,     4,     2,     4,     3,     2,
       4,     3,     1,     3,     1,     1,     3,     0,     3,     0,
       1,     0,     1,     2,     1,     1,     1,     3,     2,     3,
       4,     3,     2,     2,     1,     4,     3,     4,     5,     5,
       1,     1,     1,     1,     1,     2,     2,     2,     2,     1,
       2,     2,     2,     1,     2,     2,     2,     2,     1,     2,
       1,     1,     1,     1,     2,     0,     0,     1,     1,     2,
       3,     1,     2,     1,     1,     3,     1,     1,     2,     2,
       0,     0,     2,     3,     2,     2,     2,     3,     3,     1,
       9,     9,     7,     7,     0,     0,     9,     0,     0,    13,
       0,     1,     2,     1,     2,     1,    12,     0,     8,     2,
       1,     1,     1,     1,     1,     2,     2,     2,     3,     1,
       3,     4,     1,     1,     1,     3,     5,     2,     4,     6,
       0,     1,     2,     4,     8,     1,     3,     5,     7,     0,
       1,     0,     1,     1,     3,     6,     9,     1,     3,     1,
       0,     0,     0,     3,     2,     0,     0,     6,     2,     0,
       1,     1,     3,     1,     3,     4,     4,     3,     4,     3,
       4,     4,     3,     4,     3,     1,     0,     3,     1,     2,
       1,     3,     1,     3,     1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned short int yydefact[] =
{
      12,     0,    12,     4,     1,     6,     0,     0,     0,   275,
     306,   305,   272,   134,   362,   358,   360,     0,    60,     0,
     574,    19,     5,     9,     8,     0,     0,   219,   220,   221,
     222,   211,   212,   213,   214,   223,   224,   225,   226,   215,
     216,   217,   218,   126,   126,     0,   142,   149,   269,   271,
     270,   140,   290,   166,     0,     0,     0,   274,   273,     0,
      10,     0,     7,    17,    18,   363,   359,   361,   541,     0,
     541,     0,     0,   357,   267,   288,     0,   280,     0,   135,
     147,   153,   137,   169,   136,   148,   154,   170,   138,   159,
     164,   141,   176,   139,   160,   165,   177,   143,   145,   151,
     150,   187,   144,   146,   152,   188,   155,   157,   162,   161,
     202,   156,   158,   163,   203,   167,   185,   194,   173,   171,
     168,   186,   195,   172,   174,   200,   209,   180,   178,   175,
     201,   210,   179,   181,   183,   192,   191,   189,   182,   184,
     193,   190,   196,   198,   207,   206,   204,   197,   199,   208,
     205,     0,     0,    16,   291,    32,    33,   383,   374,   383,
     375,   372,   376,   522,    11,     0,     0,   293,     0,    87,
      88,    89,    58,    59,     0,     0,     0,     0,     0,    90,
       0,     0,    34,    36,    35,     0,    38,    37,     0,    39,
      40,     0,     0,    61,     0,     0,    63,    41,    47,   247,
     248,   249,   250,   243,   244,   245,   246,   407,     0,     0,
       0,   239,   240,   241,   242,   268,     0,     0,   289,    13,
     288,    31,   540,   288,   267,     0,   356,   521,   288,   342,
     267,   288,     0,   278,     0,   336,   337,     0,     0,     0,
       0,   364,     0,   367,     0,   370,   523,   539,     0,   296,
      56,    57,     0,     0,     0,     0,    51,    48,     0,   468,
       0,     0,    50,     0,   276,     0,    52,     0,    54,     0,
       0,    80,    78,    76,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   110,   109,     0,    43,
       0,     0,   409,   277,     0,     0,   464,     0,   457,   458,
       0,    49,   355,     0,     0,   127,   566,   353,   267,   268,
       0,     0,   470,     0,   470,   118,     0,   287,     0,     0,
      15,   288,    23,     0,   288,   288,   340,    14,    27,     0,
     288,   390,   385,   239,   240,   241,   242,   235,   236,   237,
     238,   126,   126,   382,     0,   383,   288,   383,   404,   405,
     379,   402,     0,   541,   303,   304,   301,     0,   294,   297,
     302,     0,     0,     0,     0,     0,     0,     0,    94,    93,
       0,    42,     0,     0,    85,    86,     0,     0,     0,     0,
      74,    75,    73,    72,    71,    70,    69,    64,    65,    66,
      67,    68,   107,     0,    44,    45,     0,   108,   267,   288,
     408,   410,   415,   414,   416,   424,    96,   572,     0,   467,
     439,   466,   470,   470,   470,   470,     0,   448,     0,     0,
     434,   443,   459,    95,   354,   281,   520,     0,     0,     0,
       0,   426,     0,   454,    29,   120,   119,   116,   231,   232,
     227,   228,   233,   234,   229,   230,   126,   126,   285,   341,
       0,     0,   470,   284,   339,   470,   366,   387,     0,   384,
     391,     0,   369,     0,     0,   380,     0,   379,   519,   296,
       0,    43,     0,   103,     0,   105,     0,   101,    99,    91,
      62,    53,    55,     0,     0,    79,    77,    97,     0,   106,
     418,   542,   423,   288,   422,   460,     0,   440,   435,   444,
     441,   436,   445,     0,   437,   446,   442,   438,   447,   449,
     465,    87,   275,   455,   455,   455,   455,   455,     0,     0,
       0,     0,     0,     0,   529,   512,   463,   470,     0,   125,
     126,   126,     0,   456,   513,   500,   501,   502,   503,   504,
     514,   474,   475,   509,     0,     0,   570,   550,   126,   126,
     568,     0,   551,   553,   567,     0,     0,     0,   427,   425,
       0,   123,     0,   124,     0,     0,   338,   279,   520,    21,
     282,    25,     0,   288,   386,   392,     0,   288,   388,   394,
     288,   288,   406,   403,   288,     0,   295,   541,    87,     0,
       0,     0,     0,     0,     0,    81,    84,    46,   417,   419,
       0,     0,   542,   421,   573,   470,   470,   470,     0,     0,
       0,   517,   505,   506,   507,     0,     0,     0,   530,   540,
       0,   499,     0,     0,   132,   469,   133,   548,   565,   411,
     411,   544,   545,     0,     0,   569,   428,   429,     0,    30,
     461,     0,     0,   310,   308,   307,   286,     0,     0,     0,
     288,     0,   396,   288,   288,     0,   399,   288,   365,   368,
     373,   288,   292,     0,   298,   300,    98,     0,   104,   111,
       0,   323,     0,     0,   320,     0,   322,     0,   377,   313,
     319,     0,   324,     0,     0,   420,   543,     0,     0,   484,
     490,     0,     0,   515,   508,     0,   510,     0,   288,     0,
     130,   330,     0,   131,   333,   347,   267,   288,   288,   343,
     344,   288,   562,   412,   415,   267,   288,   288,   564,   288,
     552,   219,   220,   221,   222,   211,   212,   213,   214,   223,
     224,   225,   226,   215,   216,   217,   218,   126,   126,   554,
     571,   462,   121,   122,     0,    22,   283,    26,   398,   288,
       0,   401,   288,     0,   371,     0,     0,     0,     0,   100,
     326,     0,     0,   317,    92,     0,   312,     0,   325,   327,
     316,    82,   470,   470,   485,   491,   493,     0,   470,     0,
       0,   511,     0,   518,   128,     0,   129,     0,   418,   542,
     560,   288,   346,   288,   349,   561,   413,   418,   542,   563,
     546,   411,   411,     0,   397,   393,   400,   395,   299,   102,
     112,     0,     0,   329,     0,     0,   314,   315,     0,     0,
       0,   455,   492,   470,   497,   516,     0,   525,   470,   470,
     350,   351,     0,   345,   348,     0,   288,   288,   557,   288,
     559,   309,   113,     0,   321,   318,   476,   455,   484,   471,
       0,   490,     0,   484,   541,   531,   331,   334,   352,   547,
     555,   556,   558,   328,   471,   479,   482,   483,   485,   470,
     487,   494,   490,   455,     0,     0,   526,   532,   533,   541,
       0,     0,   470,   455,   455,   455,   473,   472,   488,   495,
       0,   498,   524,     0,   531,     0,     0,   332,   335,   478,
     477,   471,   480,   481,   486,     0,   484,     0,   527,   534,
       0,   470,   470,   485,   541,     0,     0,     0,   455,     0,
     528,   537,   540,     0,   496,     0,     0,   535,   489,     0,
     538,   540,   536
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short int yydefgoto[] =
{
      -1,     1,     2,     6,     7,    22,     3,    23,    24,   323,
     647,   329,   649,   225,   560,   675,   191,   260,   393,   394,
     193,   194,   195,    25,   196,   197,   379,   378,   376,   684,
     377,   198,   594,   593,   670,   312,   313,   314,   437,   410,
      26,   304,   529,   199,   200,   201,   202,   203,   204,   205,
     206,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,   548,   549,   341,   215,   207,    45,   216,    46,    47,
      48,    49,    50,   232,    76,   233,   648,    77,   565,   305,
     218,    52,   357,   358,   359,    53,   646,   744,   677,   678,
     679,   815,   680,   762,   681,   682,   683,   700,   785,   880,
     703,   787,   881,   568,   235,   708,   709,   710,   236,    54,
      55,    56,    57,   345,   347,   352,   244,    58,   766,   466,
     239,   240,   343,   574,   578,   575,   579,   350,   351,   208,
     292,   400,   712,   713,   402,   403,   404,   226,   411,   412,
     413,   414,   415,   416,   315,   849,   297,   298,   299,   639,
     533,   300,   418,   209,   640,   316,   869,   865,   886,   887,
     819,   866,   867,   535,   774,   821,   536,   537,   888,   905,
     777,   778,   852,   890,   538,   539,   853,   540,   541,   542,
     227,   228,    60,   543,   826,   619,   876,   877,   878,   920,
     879,    69,   165,   492,   601,   550,   719,   835,   551,   552,
     739,   553,   629,   307,   427,   554,   555,   408,   210
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -781
static const short int yypact[] =
{
     109,   117,   136,  -781,  -781,  -781,  2777,  2777,   215,  -781,
    -781,  -781,  -781,  -781,   106,   106,   106,    84,  -781,    94,
    -781,  -781,  -781,  -781,  -781,    61,   131,  1169,   663,  1640,
    1064,   913,   450,   970,  1431,  1809,  1361,  1887,  1541,  1980,
    2238,  2116,  2242,  -781,  -781,   108,  -781,  -781,  -781,  -781,
    -781,   106,  -781,  -781,   104,   110,   116,  -781,  -781,   111,
    -781,  2777,  -781,  -781,  -781,   106,   106,   106,  -781,   139,
    -781,   148,  2509,  -781,    89,   106,   179,  -781,  1293,  -781,
    -781,  -781,   106,  -781,  -781,  -781,  -781,  -781,  -781,  -781,
    -781,   106,  -781,  -781,  -781,  -781,  -781,  -781,  -781,  -781,
     106,  -781,  -781,  -781,  -781,  -781,  -781,  -781,  -781,   106,
    -781,  -781,  -781,  -781,  -781,  -781,  -781,  -781,   106,  -781,
    -781,  -781,  -781,  -781,  -781,  -781,  -781,   106,  -781,  -781,
    -781,  -781,  -781,  -781,  -781,  -781,   106,  -781,  -781,  -781,
    -781,  -781,  -781,  -781,  -781,   106,  -781,  -781,  -781,  -781,
    -781,   197,   131,  -781,  -781,  -781,  -781,  -781,   125,  -781,
     130,  -781,   159,  -781,  -781,   147,   266,  -781,   230,  -781,
    -781,  -781,  -781,  -781,  2591,  2591,   248,   256,   268,  -781,
     278,   495,  -781,  -781,  -781,  2591,  -781,  -781,  1089,  -781,
    -781,  2591,   425,  -781,  2632,  2673,  -781,  3209,   718,   957,
     525,  1839,  1288,   506,   780,   597,  1148,  -781,   272,  1771,
    2591,   347,   382,   352,   395,  -781,   131,   131,   106,  -781,
     106,  -781,  -781,   106,   361,   400,  -781,  -781,   106,  -781,
      89,   106,   213,  -781,  1219,   482,   487,   283,  2111,   330,
     875,  -781,   340,  -781,   432,  -781,  -781,  -781,   353,   473,
    -781,  -781,  2591,  2383,  1323,  3052,  -781,  -781,   360,  -781,
     475,   377,  -781,  2591,  -781,  1089,  -781,  1089,  -781,  2591,
    2591,   441,  -781,  -781,  2591,  2591,  2591,  2591,  2591,  2591,
    2591,  2591,  2591,  2591,  2591,  2591,  -781,  -781,   495,  2591,
    2591,   495,   226,  -781,   402,   495,  -781,  1575,   466,  -781,
     416,  -781,   487,    38,   131,  -781,  -781,  -781,    89,   512,
    2134,   436,  -781,   936,    49,  -781,  2484,   493,   197,   197,
    -781,   106,  -781,   400,   106,   106,  -781,  -781,  -781,   400,
     106,  -781,  -781,   957,   525,  1839,  1288,   506,   780,   597,
    1148,  -781,   474,   463,  1214,  -781,   106,  -781,  -781,   514,
     485,  -781,   432,  -781,  -781,  -781,  -781,   477,  -781,   489,
    -781,  2921,   472,  2942,   510,   488,   524,   539,  -781,  -781,
    1473,  3209,   540,   542,  3209,  3209,  2591,   596,  2591,  2591,
    2105,  3248,  1030,  1626,  1969,   354,   354,   403,   403,  -781,
    -781,  -781,  -781,   567,   581,  3209,   289,  -781,    89,   106,
    -781,  -781,  -781,  -781,   505,  -781,  -781,  -781,   286,   436,
    -781,  -781,    63,    70,    81,    90,   685,  -781,   601,  2267,
    -781,  -781,  -781,  -781,  -781,  -781,   222,  1003,  2591,  2591,
    2175,  -781,  2797,  -781,  -781,  -781,  -781,  -781,  2305,  3167,
    1487,   730,  3086,  3172,  1545,   857,   607,   609,  -781,   482,
     261,   197,  -781,   656,  -781,  -781,  -781,   254,   337,  -781,
    -781,   613,  -781,   615,  2591,   495,   617,   485,  -781,   473,
     618,  2714,  2728,  -781,  2591,  -781,  2728,  -781,  -781,  -781,
    -781,   619,   619,    36,  2591,  3238,  3171,  -781,  2591,  -781,
     226,   226,  -781,   106,  -781,  -781,   495,  -781,  -781,  -781,
    -781,  -781,  -781,  2342,  -781,  -781,  -781,  -781,  -781,  -781,
    -781,   666,   669,  -781,  -781,  -781,  -781,  -781,  2591,   671,
     630,   632,  2550,   275,   715,  -781,  -781,  -781,   298,  -781,
    -781,  -781,   638,   126,  -781,  -781,  -781,  -781,  -781,  -781,
    -781,  -781,  -781,  -781,  2446,   635,  -781,  -781,  -781,  -781,
    -781,   639,   301,  -781,  -781,   515,  2823,  2846,  -781,  -781,
      64,  -781,   197,  -781,   131,  2008,  -781,  -781,   704,  -781,
    -781,  -781,  2591,    80,   650,  -781,  2591,   216,   651,  -781,
     106,   106,  3209,  -781,   106,   667,  -781,  -781,   518,   664,
     672,  2967,   676,   495,  1874,  -781,  3225,  3209,  -781,  -781,
     680,  1389,  -781,  -781,  -781,  -781,  -781,  -781,   696,   708,
    2992,  -781,  -781,  -781,  -781,   308,  2591,   688,  -781,  -781,
     736,  -781,   197,   131,  -781,  -781,  -781,  -781,  -781,    97,
     178,  -781,  -781,  3057,   786,  -781,  -781,  -781,   698,  -781,
    -781,   333,   351,  -781,  -781,  3209,  -781,    64,  2008,    64,
    3111,  2591,  -781,   106,  3111,  2591,  -781,   106,  -781,  -781,
    -781,   106,  -781,  2591,  -781,  -781,  -781,  2591,  -781,  -781,
     220,  -781,   495,  2591,  -781,   747,  3209,   709,   721,  -781,
    -781,   189,  -781,  1656,  2591,  -781,  -781,   734,   735,  -781,
    2446,  2591,  2591,  -781,  -781,   357,  -781,   741,   106,   376,
    -781,   207,   401,  -781,   711,  -781,    89,   106,   106,   532,
     558,   224,  -781,  -781,   106,    89,   106,   224,  -781,   106,
    -781,  2305,  3167,  3091,  3185,  1487,   730,  1694,  1038,  3086,
    3172,  3120,  3202,  1545,   857,  1723,  1257,  -781,  -781,  -781,
    -781,  -781,  -781,  -781,  1874,  -781,  -781,  -781,  -781,  3111,
     254,  -781,  3111,   337,  -781,   536,  2895,   495,  2591,  -781,
    -781,  2774,  1874,  -781,  -781,  1942,  -781,  2049,  -781,  -781,
    -781,  3225,  -781,  -781,  -781,   724,  -781,   729,  -781,   545,
    3191,  -781,   266,  -781,  -781,   400,  -781,   400,    97,   180,
    -781,   106,  -781,   106,  -781,  -781,   106,   178,   178,  -781,
    -781,    97,   178,   737,  -781,  -781,  -781,  -781,  -781,  -781,
    -781,   306,  2591,  -781,   738,  2049,  -781,  -781,  2591,   732,
     744,  -781,  -781,  -781,  -781,  -781,   746,   782,  -781,  -781,
     532,   558,   284,  -781,  -781,  1389,   106,   224,  -781,   224,
    -781,  -781,  -781,  2872,  -781,  -781,   724,  -781,  -781,  -781,
     810,  2591,   743,  -781,  -781,    66,  -781,  -781,  -781,  -781,
    -781,  -781,  -781,  -781,  -781,  -781,   822,   823,  -781,  -781,
    -781,  -781,  2591,  -781,   756,   495,   800,   757,  -781,  -781,
     698,   698,    93,  -781,  -781,  -781,  -781,  -781,  -781,  -781,
     759,  -781,  -781,   761,    66,    66,   791,  -781,  -781,  -781,
    -781,  -781,  -781,  -781,  -781,   792,  -781,   266,   825,  -781,
    2591,   787,  -781,  -781,  -781,   266,   560,   783,  -781,   819,
     794,  -781,  -781,   799,  -781,  2591,   266,  -781,  -781,   562,
    -781,  -781,  -781
};

/* YYPGOTO[NTERM-NUM].  */
static const short int yypgoto[] =
{
    -781,  -781,  -781,  -781,  -781,    76,   885,  -781,  -781,  -781,
    -781,  -781,  -781,  -781,  -781,   -28,  -781,   -71,   417,   232,
     468,  -781,  -781,  -781,  -105,  1189,  -781,  -781,  -781,  -781,
    -781,  -781,  -781,  -781,  -781,  -280,   579,  -781,  -781,   113,
     112,  -301,  -500,    -2,     0,   138,   219,     2,     7,    31,
     141,  -305,  -304,   264,   267,  -292,  -286,   280,   291,  -383,
    -359,   583,   585,  -781,  -162,  -781,  -373,  -209,   620,  1012,
     133,   470,  -781,  -504,  -133,   451,  -781,   612,  -781,    41,
     693,   -34,  -781,   453,  -781,   554,   282,  -781,  -616,  -781,
     161,  -781,  -638,  -781,  -781,   250,   251,  -781,  -781,  -781,
    -781,  -781,  -781,  -135,   362,   135,   150,  -123,    16,  -781,
    -781,  -781,  -781,  -781,  -781,  -781,  -781,  -781,  -781,   479,
    -118,  -781,   595,  -781,  -781,   199,   204,   616,   502,  -115,
    -781,  -781,  -591,  -274,  -443,  -487,  -781,   531,  -781,  -781,
    -781,  -781,  -781,  -781,  -246,  -461,  -781,  -781,   681,  -551,
    -781,   437,  -781,  -781,  -397,   662,  -777,  -742,  -221,  -207,
    -733,  -781,  -200,    87,  -633,  -780,  -781,  -781,  -781,  -781,
    -757,  -781,  -781,  -781,  -781,  -781,  -781,  -781,  -781,  -781,
     269,  -205,  -781,  -781,  -781,  -781,    88,  -781,    95,  -781,
    -156,   -19,   -68,   490,  -781,  -576,  -781,  -781,  -781,  -781,
    -781,  -781,   439,  -302,  -781,  -781,  -781,  -781,    28
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -550
static const short int yytable[] =
{
      71,   192,   167,   599,    27,    27,    28,    28,    31,    31,
     248,   442,   443,    32,    32,   310,   234,   154,   401,   237,
     451,   318,   534,   454,   444,   686,   158,   160,   162,   324,
     445,   154,   154,   154,    61,    61,   530,    33,    33,   718,
     820,   242,    78,   452,   626,   770,   532,   598,   154,   455,
    -115,   417,   605,   606,   607,   608,   609,   154,   641,    27,
     531,    28,   309,    31,  -450,   638,   154,   435,    32,   250,
     251,  -451,   211,   261,   212,   154,   420,   247,   342,   850,
     257,   595,  -452,    62,   154,    68,   262,   882,   885,    61,
     421,  -453,    33,   154,   871,    70,   745,    13,   747,   428,
      73,   705,   154,   223,   224,   301,   534,   155,   156,    -2,
     513,   154,    19,   155,   156,   889,   217,     4,   699,   155,
     156,    19,   442,   443,   911,   651,    72,   263,   803,   817,
     424,   891,   875,   918,    73,   444,    -3,   164,    19,   365,
     367,   445,  -115,   904,    29,    29,   814,    34,    34,  -540,
     372,  -430,   373,   256,   706,   151,   152,   526,  -431,  -540,
     295,   530,   707,   224,    98,   103,   107,   112,   238,  -432,
     509,   532,   134,   139,   143,   148,   924,   845,  -433,   917,
     899,    73,   342,    73,   154,   531,   714,   714,    74,   490,
     776,   498,   501,   504,   507,   153,    75,   157,   163,    29,
      73,   229,    34,   159,   166,   499,   502,   505,   508,   161,
     838,   840,   213,   168,   296,   868,   349,   600,   241,   396,
     873,   356,   211,   243,   212,    30,    30,   461,   211,   463,
     212,   767,   302,   303,   246,   715,   222,   706,   333,  -520,
     334,   741,   337,   716,   224,   707,   224,   338,    19,  -520,
     737,   222,   245,   672,   230,   673,    19,    73,   229,   859,
     392,   655,   231,   397,   306,   480,   219,   407,   344,   317,
     220,   339,   319,   913,   738,    59,    59,   247,   155,   156,
      30,   223,   224,   398,   757,   468,   758,   223,   224,   223,
     224,   399,   224,   214,  -520,   249,   442,   443,  -520,   572,
     320,   599,    63,    64,   321,   483,   211,   530,   212,   444,
     599,   230,   759,   252,   438,   445,   439,   532,   440,   231,
     426,   253,   573,   441,   349,   257,   325,   224,   729,   730,
      59,   531,   616,   254,   302,   303,    98,   103,   107,   112,
      73,   733,   333,   255,   334,   598,   337,   734,   528,   793,
     224,   338,   750,   566,   598,    79,   753,   590,   714,   714,
      88,   592,   213,   324,   293,   453,   306,   308,   213,    13,
     327,   456,   344,   495,   220,   339,   858,   496,   335,    19,
     263,   340,   576,   489,    19,   621,   864,   462,   632,   263,
      84,   527,   633,    19,    74,   694,   211,   263,   212,   263,
     842,   311,    75,    93,  -470,  -470,  -470,  -470,  -470,   281,
     282,   283,   284,   285,  -470,  -470,  -470,    27,   330,    28,
     742,    31,   901,   901,   321,   438,    32,   439,   346,   440,
    -470,   642,   528,   348,   441,   155,   156,   349,   743,   301,
     491,   356,   220,   214,   781,   353,   213,   544,   263,   214,
      33,   615,   368,   457,   458,    10,    11,    12,   102,   336,
     283,   284,   285,   784,    14,    15,    16,   321,   604,   370,
      98,   103,   107,   112,   577,   527,   155,   156,    10,    11,
     354,   355,   335,   897,   898,   340,   -83,   701,   786,   833,
     702,   834,   220,  -114,   406,   617,   324,   788,   155,   156,
     295,    99,   104,   108,   113,   828,   797,   829,   423,   135,
     140,   144,   149,    12,    97,   600,   263,   264,   429,   662,
      14,    15,    16,   433,   600,   497,   500,   214,   506,     9,
     442,   443,    12,    84,   602,   448,   213,  -256,    19,    14,
      15,    16,    27,   444,    28,   695,    31,   325,   224,   445,
     459,    32,   223,   224,   471,    18,   464,    29,   562,   564,
      34,  -389,  -389,   336,   473,   669,   263,   369,   469,   470,
     493,   224,   544,    98,   103,    33,   465,   134,   139,   476,
     426,    83,    87,    92,    96,   101,   105,   110,   114,   119,
     123,   128,   132,   137,   141,   146,   150,   791,   224,   438,
     697,   439,   475,   440,    12,   106,   634,   635,   441,   663,
     664,    14,    15,    16,   652,   573,   477,   214,   656,   775,
     779,   658,   659,   793,   224,   660,   827,   488,   808,    19,
     478,   721,   481,   722,   482,   725,   263,   824,    30,   704,
     726,   484,   622,   623,   760,   711,   717,    80,    85,    89,
      94,   263,   922,   263,   931,   116,   121,   125,   130,   487,
     628,   628,   266,   268,   727,   831,   832,     9,    10,    11,
      12,    84,   488,    99,   104,   108,   113,    14,    15,    16,
     449,   450,    29,   902,   903,    34,   311,   811,    27,   510,
      28,   748,    31,    18,   561,   751,   563,    32,   570,    51,
      51,   580,   754,   581,   211,   584,   212,    65,    66,    67,
     587,   -32,   479,   211,   -33,   212,   611,   612,   544,   613,
      82,    33,    91,   618,   100,   624,   109,   627,   118,   810,
     127,   631,   136,   222,   145,    10,    11,    12,   102,   783,
     222,   653,   657,  -520,    14,    15,    16,   846,   789,   790,
    -252,   914,   795,  -520,    51,   661,   665,   798,   799,   921,
     800,   690,   154,    30,   666,    51,   326,    51,   668,   577,
     930,   723,   685,   691,   728,   696,   223,   224,   286,   287,
     775,   698,   288,   289,   290,   291,   874,    12,   102,   740,
     804,   526,   763,   806,    14,    15,    16,   764,  -520,   772,
     773,   775,  -520,   360,   302,   303,   782,    99,   104,   108,
     113,   896,   765,   302,   303,   263,   822,   837,   839,    80,
      85,    89,    94,   405,   847,   841,   844,   855,    29,   870,
     872,    34,   306,   438,   306,   439,   848,   440,   854,   916,
     883,   884,   441,   892,   213,   894,   919,   893,   895,   801,
     802,   906,   724,   213,   929,   907,   910,   912,    98,   103,
     107,   112,    10,    11,    12,   138,   134,   139,   143,   148,
     915,    14,    15,    16,   899,   923,   331,   860,   861,     9,
     862,    51,    12,    13,   925,   926,   928,     5,   589,    14,
      15,    16,    82,   436,    91,   755,   100,   731,   109,   446,
     732,   447,   567,   927,    82,    18,    91,    19,    20,    30,
      99,   104,   932,   735,   135,   140,   425,    51,    10,    11,
      12,    97,   586,    51,   736,   214,   816,    14,    15,    16,
     746,   768,   769,    51,   214,   494,   836,   311,   830,   460,
    -117,  -117,  -117,  -117,  -117,    19,   585,    51,    51,   805,
    -117,  -117,  -117,    80,    85,    89,    94,   807,    51,   419,
      51,     9,   332,  -381,    12,    79,  -117,   583,   467,   900,
     625,    14,    15,    16,   434,    10,    11,    12,   106,   422,
     326,   326,   908,   603,    14,    15,    16,    18,   630,    19,
     909,     0,    83,    87,   101,   105,   119,   123,   137,   141,
    -255,    51,    19,     0,   545,     0,   546,     9,    10,    11,
      12,    13,     0,     0,     0,   547,     0,    14,    15,    16,
       0,   405,   405,   360,     0,     0,    82,     0,    91,  -117,
     100,     0,   109,    18,     0,     0,     0,    51,     0,    81,
      86,    90,    95,    10,    11,    12,   111,   117,   122,   126,
     131,     0,    14,    15,    16,     0,     0,  -257,    80,    85,
       0,     0,   116,   121,     0,     0,     0,     0,     9,    10,
      11,    12,    93,     0,   419,   419,   503,   419,    14,    15,
      16,   277,   278,   279,   280,   281,   282,   283,   284,   285,
     258,    51,   169,     9,    18,  -549,    12,    13,     0,   170,
     171,     0,   172,    14,    15,    16,     0,     0,     0,     0,
       0,     0,    51,     0,   569,     0,     0,   571,     0,    18,
     173,    19,    20,     0,   174,   175,   176,   177,   178,   179,
     180,    82,     0,   100,     0,   118,   181,   136,     0,   182,
       0,     0,     0,     0,   183,   184,   185,     0,     0,   186,
     187,  -254,     0,     0,   188,    12,   111,     0,     0,     0,
     405,   405,    14,    15,    16,    51,     0,     0,     0,    51,
       0,     0,     0,     9,    10,    11,    12,    79,   189,   190,
       0,     0,   259,    14,    15,    16,     0,     0,     0,   620,
       0,     0,     0,     0,     0,    99,   104,   108,   113,    18,
       0,    19,     0,   135,   140,   144,   149,     0,     0,     0,
       0,    81,    86,    90,    95,   331,     0,     0,     9,     0,
     322,    12,    13,   -20,   -20,   -20,   -20,   -20,    14,    15,
      16,     0,     0,   -20,   -20,   -20,     0,    51,     0,     0,
     792,   794,     0,     0,    18,     0,    19,    20,   222,   -20,
       0,  -520,     0,     0,     0,     0,  -251,     0,     0,     0,
       0,  -520,    10,    11,    12,   147,     0,   687,   688,   689,
       0,    14,    15,    16,     0,    83,    87,    92,    96,   101,
     105,   110,   114,   119,   123,   128,   132,   137,   141,   146,
     150,     0,     9,     0,   221,    12,    93,   -28,   -28,   -28,
     -28,   -28,    14,    15,    16,     0,  -520,   -28,   -28,   -28,
    -520,     0,   -20,     0,     0,     0,     0,     0,    18,   405,
     405,     0,   222,   -28,   364,  -520,    51,     9,   405,   405,
      12,    13,   405,   405,     0,  -520,     0,    14,    15,    16,
       0,    80,    85,    89,    94,    81,    86,    90,    95,   116,
     121,   125,   130,    18,     0,    19,     0,     0,   223,   224,
       0,   792,   794,   794,     0,     9,    10,    11,    12,   120,
       0,     0,     0,     0,     0,    14,    15,    16,     0,     0,
    -520,     0,     0,    51,  -520,     0,   -28,     0,     0,     0,
     545,    18,     0,     9,    10,    11,    12,    13,     0,    51,
       0,   547,     0,    14,    15,    16,     0,   796,    51,     0,
       0,     0,     0,     0,    82,     0,    91,     0,   100,    18,
     109,     0,   118,     0,   127,     0,   136,     0,   145,     0,
       0,     0,     0,     0,   818,   818,    10,    11,    12,   111,
     823,   361,   363,     0,     0,    14,    15,    16,  -260,     0,
      81,    86,   371,     0,   117,   122,     0,     0,   374,   375,
       0,     0,     0,   380,   381,   382,   383,   384,   385,   386,
     387,   388,   389,   390,   391,     0,   169,     0,   395,     0,
       0,  -549,     0,   170,   171,   851,   172,     0,     0,     0,
     856,   857,    10,    11,    12,    97,     0,     0,     0,   432,
       0,    14,    15,    16,   173,     0,    20,     0,   174,   175,
     176,   177,   178,   179,   180,     0,     0,     0,  -258,    19,
     181,     0,     0,   182,     0,     0,     0,     0,   183,   184,
     185,   503,     0,   186,   187,     0,     0,     0,   188,     0,
       0,     0,     0,     0,   503,     9,    10,    11,    12,   129,
      10,    11,    12,   133,     0,    14,    15,    16,     0,    14,
      15,    16,   189,   190,     0,     0,   479,   485,   486,     0,
       0,    18,     0,   503,   818,     0,   409,    19,  -470,  -470,
    -470,  -470,  -470,  -470,     0,  -470,  -470,     0,  -470,  -470,
    -470,  -470,  -470,     0,  -470,  -470,  -470,  -470,  -470,  -470,
    -470,  -470,  -470,  -470,  -470,  -470,  -470,  -470,  -470,     0,
    -470,  -470,  -470,  -470,  -470,  -470,  -470,   556,   557,     0,
       0,     0,  -470,     0,     0,  -470,     0,     0,  -262,     0,
    -470,  -470,  -470,     0,     0,  -470,  -470,     0,     0,     0,
    -470,     0,     0,     0,     9,    10,    11,    12,    88,     0,
       0,     0,     0,   582,    14,    15,    16,   671,     0,   169,
     395,     0,  -470,   591,  -470,  -470,   170,   171,  -470,   172,
      18,     0,    19,   596,     0,     0,     0,   597,   278,   279,
     280,   281,   282,   283,   284,   285,     0,   173,     0,    20,
       0,   174,   175,   176,   177,   178,   179,   180,  -327,    10,
      11,    12,   106,   181,     0,     0,   182,   610,    14,    15,
      16,   183,   184,   185,     0,     0,   186,   187,     0,     0,
    -327,   188,  -327,     0,     0,     0,    19,  -253,    10,    11,
      12,   142,     0,    81,    86,    90,    95,    14,    15,    16,
       0,   117,   122,   126,   131,   189,   190,     0,     0,   674,
       0,     0,     0,     0,   645,    19,     0,     0,     0,     0,
       0,   650,     0,     0,     0,   654,     0,     0,     0,     0,
       0,     0,   294,     0,  -456,  -456,  -456,  -456,  -456,  -456,
       0,  -456,  -456,   676,  -456,  -456,  -456,  -456,  -456,     0,
    -456,  -456,  -456,  -456,  -456,  -456,  -456,  -456,  -456,  -456,
    -456,  -456,  -456,  -456,  -456,   295,  -456,  -456,  -456,  -456,
    -456,  -456,  -456,     9,    10,    11,    12,   115,  -456,     0,
       0,  -456,     0,    14,    15,    16,  -456,  -456,  -456,     0,
       0,  -456,  -456,     0,     0,     0,  -456,   645,     0,    18,
     749,    19,     0,     9,   752,     0,    12,    88,     0,     0,
       0,     0,   395,    14,    15,    16,   756,     0,  -456,   296,
    -456,  -456,   761,     0,  -456,     0,     0,     0,     0,    18,
       0,    19,   676,   771,     0,   671,     0,   511,   156,     0,
       0,   780,     0,     0,   170,   171,     0,   172,     0,     0,
       0,     9,    10,    11,    12,   124,  -259,     0,     0,     0,
       0,    14,    15,    16,     0,   173,     0,    20,     0,   174,
     175,   176,   177,   178,   179,   180,     0,    18,     0,    19,
       0,   181,     0,     0,   182,     0,     0,     0,     0,   183,
     184,   185,     0,   676,   186,   187,     0,     0,   672,   188,
     673,     0,     0,   671,     0,   511,   156,     0,     0,     0,
       0,   676,   170,   171,   676,   172,   676,     0,     0,     0,
       0,     0,  -311,   189,   190,     0,     0,   674,     0,     0,
       0,     0,     0,   173,  -261,    20,     0,   174,   175,   176,
     177,   178,   179,   180,     0,    10,    11,    12,   133,   181,
       0,     0,   182,     0,    14,    15,    16,   183,   184,   185,
       0,   843,   186,   187,   676,     0,   672,   188,   673,   643,
       0,   169,    19,     0,     0,     0,     0,     0,   170,   171,
       0,   172,   279,   280,   281,   282,   283,   284,   285,     0,
    -378,   189,   190,     0,     0,   674,     0,     0,     0,   173,
       0,    20,     0,   174,   175,   176,   177,   178,   179,   180,
     671,     0,   169,     0,     0,   181,     0,     0,   182,   170,
     171,     0,   172,   183,   184,   185,     0,  -263,   186,   187,
       0,     0,     0,   188,     0,     0,     0,     0,     0,     0,
     173,     0,    20,     0,   174,   175,   176,   177,   178,   179,
     180,     0,     0,     0,     0,     0,   181,   189,   190,   182,
       0,   644,     0,     0,   183,   184,   185,     0,     0,   186,
     187,     0,   328,     0,   188,   -24,   -24,   -24,   -24,   -24,
       0,    10,    11,    12,   142,   -24,   -24,   -24,     0,     0,
      14,    15,    16,     0,     0,     0,     0,   169,   189,   190,
     222,   -24,   674,  -520,   170,   171,     0,   172,    19,     0,
       0,     0,     0,  -520,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   173,     0,    20,     0,   174,
     175,   176,   177,   178,   179,   180,   223,   224,   169,     0,
       0,   181,     0,     0,   182,   170,   171,     0,   172,   183,
     184,   430,     0,     0,   186,   187,     0,     0,  -520,   188,
       0,     0,  -520,  -265,   -24,     0,   173,     0,    20,     0,
     174,   175,   176,   177,   178,   179,   180,     0,     0,     0,
       0,     0,   181,   189,   190,   182,     0,     0,   431,     0,
     183,   184,   185,     0,     0,   186,   187,     0,     0,     0,
     188,     0,     0,    10,    11,    12,   138,    10,    11,    12,
     147,     0,    14,    15,    16,     0,    14,    15,    16,     0,
       0,     0,     0,     0,   189,   190,     0,     0,     0,   558,
     511,   512,    10,    11,    12,    13,     0,   170,   171,     0,
     172,    14,    15,    16,   513,     0,   514,   515,   516,   517,
     518,   519,   520,   521,   522,   523,   524,    18,   173,    19,
      20,     0,   174,   175,   176,   177,   178,   179,   180,     9,
      10,    11,    12,    79,   181,     0,     0,   182,     0,    14,
      15,    16,   183,   184,   185,  -264,     0,   186,   187,  -266,
       0,     0,   188,     0,     0,    18,     0,    19,     0,     0,
       0,     0,     0,     0,     0,   511,   156,     0,     0,     0,
       0,     0,   170,   171,   525,   172,   189,   190,     0,   513,
     526,   514,   515,   516,   517,   518,   519,   520,   521,   522,
     523,   524,     0,   173,     0,    20,     0,   174,   175,   176,
     177,   178,   179,   180,   362,     0,   169,     0,     0,   181,
       0,     0,   182,   170,   171,     0,   172,   183,   184,   185,
       0,     0,   186,   187,     0,     0,     0,   188,     0,     0,
       0,     0,     0,     0,   173,     0,    20,     0,   174,   175,
     176,   177,   178,   179,   180,     0,     0,     0,     0,   525,
     181,   189,   190,   182,     0,   526,     0,     0,   183,   184,
     185,     0,     0,   186,   187,     0,     0,     0,   188,   169,
       9,    10,    11,    12,    13,     0,   170,   171,     0,   172,
      14,    15,    16,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   189,   190,     0,     0,    18,   173,    19,    20,
       0,   174,   175,   176,   177,   178,   179,   180,     9,    10,
      11,    12,    13,   181,     0,     0,   182,     0,    14,    15,
      16,   183,   184,   185,     0,     0,   186,   187,     0,     0,
       0,   188,   169,     9,    18,     0,    12,    13,     0,   170,
     171,     0,   172,    14,    15,    16,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   189,   190,     0,     0,    18,
     173,    19,    20,     0,   174,   175,   176,   177,   178,   179,
     180,     0,     0,   169,     0,     0,   181,     0,     0,   182,
     170,   171,     0,   172,   183,   184,   185,     0,     0,   186,
     187,     0,     0,     0,   188,     0,     0,     0,     0,     0,
       0,   173,     0,    20,     0,   174,   175,   176,   177,   178,
     179,   180,     0,     0,   169,     0,     0,   181,   189,   190,
     182,   170,   171,     0,   172,   183,   184,   185,     0,     0,
     186,   187,     0,     0,     0,   188,     0,     0,     0,     0,
       0,     0,   173,     0,    20,     0,   174,   175,   176,   177,
     178,   179,   180,     0,     0,   169,     0,   614,   181,   189,
     190,   182,   170,   171,     0,   172,   183,   184,   185,     0,
       0,   186,   187,     0,     0,     0,   188,     0,     0,     0,
       0,     0,     0,   173,     0,    20,     0,   174,   175,   176,
     177,   178,   179,   180,     0,     0,   169,     0,     0,   181,
     189,   190,   182,   170,   171,     0,   172,   183,   184,   185,
       0,     0,   186,   187,     0,     0,     0,   265,     0,     0,
       0,     0,     0,     0,   173,     0,    20,     0,   174,   175,
     176,   177,   178,   179,   180,     0,     0,   588,     0,     0,
     181,   189,   190,   182,   170,   171,     0,   172,   183,   184,
     185,     0,     9,   186,   187,    12,    13,     0,   267,     0,
       0,     0,    14,    15,    16,   173,     0,    20,     0,   174,
     175,   176,   177,   178,   179,   180,     0,     0,    18,     0,
      19,   181,   189,   190,   182,     0,     0,     0,     0,   183,
     184,   185,     0,     0,   186,   187,     0,     0,     8,   188,
    -126,     9,    10,    11,    12,    13,   812,     0,     0,     0,
       0,    14,    15,    16,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   189,   190,     0,    17,    18,     0,    19,
      20,     0,     0,     0,     0,     0,   269,   270,   271,     0,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   284,   285,  -126,     0,     0,     0,     0,   269,
     270,   271,  -126,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,   284,   285,     0,     0,     0,
       0,     0,     0,     0,    21,   269,   270,   271,   813,   272,
     273,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   284,   285,     0,     0,     0,     0,     0,   269,   270,
     271,   559,   272,   273,   274,   275,   276,   277,   278,   279,
     280,   281,   282,   283,   284,   285,     0,     0,     0,     0,
       0,     0,     0,     0,   269,   270,   271,   636,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,     0,     0,     0,     0,     0,   269,   270,   271,
     637,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,     0,     0,     0,     0,     0,
       0,     0,     0,   269,   270,   271,   863,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,     0,     0,     0,   269,   270,   271,   809,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,     0,     0,   692,     0,     0,     0,     0,   269,
     270,   271,   472,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,   284,   285,     0,     0,     0,
       0,     0,     0,   474,   269,   270,   271,   693,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     284,   285,     0,   366,     0,     0,     9,     0,   667,    12,
      13,     9,    10,    11,    12,    13,    14,    15,    16,   720,
       0,    14,    15,    16,     0,     0,     0,     0,     0,     0,
       0,     0,    18,     0,    19,     0,     0,    18,     0,    19,
       9,    10,    11,    12,   115,     9,    10,    11,    12,    88,
      14,    15,    16,     0,     0,    14,    15,    16,     0,     0,
       0,     0,     0,     0,     0,     0,    18,     0,    19,     0,
       0,    18,     0,    19,     9,    10,    11,    12,   124,     0,
       0,     0,     0,     0,    14,    15,    16,     0,     0,     0,
       0,     0,     0,    19,     0,     0,     0,     0,     0,     0,
      18,     0,    19,   269,   270,   271,     0,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,     9,    10,    11,    12,    84,     9,    10,    11,    12,
     120,    14,    15,    16,     0,     0,    14,    15,    16,     9,
      10,    11,    12,    93,     0,     0,     0,    18,     0,    14,
      15,    16,    18,     0,     0,     0,     9,    10,    11,    12,
     129,     0,     0,     0,     0,    18,    14,    15,    16,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,     0,    18,   269,   270,   271,   825,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   269,   270,   271,     0,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   281,   282,   283,   284,   285,   271,
       0,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,   284,   285,   276,   277,
     278,   279,   280,   281,   282,   283,   284,   285
};

static const short int yycheck[] =
{
      19,    72,    70,   490,     6,     7,     6,     7,     6,     7,
     166,   316,   316,     6,     7,   224,   151,    51,   292,   152,
     321,   230,   419,   325,   316,   601,    54,    55,    56,   234,
     316,    65,    66,    67,     6,     7,   419,     6,     7,   630,
     773,   159,    26,   323,   544,   683,   419,   490,    82,   329,
       1,   297,   513,   514,   515,   516,   517,    91,   562,    61,
     419,    61,   224,    61,     1,     1,   100,   313,    61,   174,
     175,     1,    74,   188,    74,   109,   297,    11,   240,   821,
     185,    45,     1,     7,   118,     1,   191,   864,   868,    61,
     297,     1,    61,   127,   851,     1,   647,     8,   649,   308,
       3,     4,   136,    65,    66,   210,   503,     3,     4,     0,
      17,   145,    32,     3,     4,   872,    75,     0,   622,     3,
       4,    32,   427,   427,   901,    45,    65,    91,   744,   767,
      92,   873,    66,   913,     3,   427,     0,    61,    32,   254,
     255,   427,    93,   885,     6,     7,   762,     6,     7,    65,
     265,    88,   267,   181,    57,    43,    44,    93,    88,    65,
      34,   544,    65,    66,    31,    32,    33,    34,   152,    88,
     416,   544,    39,    40,    41,    42,   918,   815,    88,   912,
      87,     3,   344,     3,   218,   544,   629,   630,    57,   398,
     690,   412,   413,   414,   415,    87,    65,    93,    87,    61,
       3,     4,    61,    93,    65,   412,   413,   414,   415,    93,
     801,   802,    74,    65,    88,   848,   244,   491,    93,   290,
     853,   249,   224,    93,   224,     6,     7,   345,   230,   347,
     230,    42,   216,   217,    87,    57,    29,    57,   240,    32,
     240,   638,   240,    65,    66,    65,    66,   240,    32,    42,
     633,    29,    93,    64,    57,    66,    32,     3,     4,   835,
     288,    45,    65,   291,   223,   370,    87,   295,   240,   228,
      91,   240,   231,   906,   633,     6,     7,    11,     3,     4,
      61,    65,    66,    57,    64,   353,    66,    65,    66,    65,
      66,    65,    66,    74,    87,    65,   601,   601,    91,    45,
      87,   788,    87,    88,    91,   376,   308,   690,   308,   601,
     797,    57,    92,    65,   316,   601,   316,   690,   316,    65,
     304,    65,   457,   316,   352,   430,    65,    66,   633,   633,
      61,   690,    57,    65,   318,   319,   203,   204,   205,   206,
       3,   633,   344,    65,   344,   788,   344,   633,   419,    65,
      66,   344,   653,    92,   797,     8,   657,   472,   801,   802,
       8,   476,   224,   568,    92,   324,   325,     6,   230,     8,
      87,   330,   344,    87,    91,   344,    92,    91,   240,    32,
      91,   240,    45,    94,    32,    87,   847,   346,    87,    91,
       8,   419,    91,    32,    57,    87,   398,    91,   398,    91,
      94,     1,    65,     8,     4,     5,     6,     7,     8,    55,
      56,    57,    58,    59,    14,    15,    16,   419,    88,   419,
      87,   419,   883,   884,    91,   427,   419,   427,    88,   427,
      30,   564,   503,     1,   427,     3,     4,   465,    87,   544,
     399,   469,    91,   224,    87,    92,   308,   419,    91,   230,
     419,   522,    92,   341,   342,     5,     6,     7,     8,   240,
      57,    58,    59,    87,    14,    15,    16,    91,   496,    92,
     337,   338,   339,   340,   458,   503,     3,     4,     5,     6,
       7,     8,   344,   880,   881,   344,    45,   622,    87,   791,
     623,   793,    91,    93,    92,   523,   701,   706,     3,     4,
      34,    31,    32,    33,    34,   785,   715,   787,    92,    39,
      40,    41,    42,     7,     8,   789,    91,    92,     6,   587,
      14,    15,    16,    87,   798,   412,   413,   308,   415,     4,
     835,   835,     7,     8,   493,    42,   398,    87,    32,    14,
      15,    16,   544,   835,   544,   616,   544,    65,    66,   835,
      87,   544,    65,    66,    65,    30,    42,   419,   446,   447,
     419,    87,    88,   344,    92,   593,    91,    92,    91,    92,
      65,    66,   544,   440,   441,   544,    91,   444,   445,    91,
     564,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    65,    66,   601,
     619,   601,    92,   601,     7,     8,    91,    92,   601,    91,
      92,    14,    15,    16,   573,   750,    92,   398,   577,   690,
     691,   580,   581,    65,    66,   584,   782,    91,    92,    32,
      91,   633,    92,   633,    92,   633,    91,    92,   419,   623,
     633,    45,   530,   531,   672,   629,   630,    27,    28,    29,
      30,    91,    92,    91,    92,    35,    36,    37,    38,    92,
     548,   549,   194,   195,   633,   788,   789,     4,     5,     6,
       7,     8,    91,   203,   204,   205,   206,    14,    15,    16,
     318,   319,   544,   883,   884,   544,     1,   758,   690,    88,
     690,   650,   690,    30,    87,   654,    87,   690,    42,     6,
       7,    88,   661,    88,   706,    88,   706,    14,    15,    16,
      92,    45,    93,   715,    45,   715,    45,    87,   690,    87,
      27,   690,    29,     8,    31,    87,    33,    92,    35,   757,
      37,    92,    39,    29,    41,     5,     6,     7,     8,   698,
      29,    91,    91,    32,    14,    15,    16,   818,   707,   708,
      87,   907,   711,    42,    61,    88,    92,   716,   717,   915,
     719,    65,   796,   544,    92,    72,   235,    74,    92,   753,
     926,   633,    92,    65,   633,    87,    65,    66,    60,    61,
     851,    45,    64,    65,    66,    67,   854,     7,     8,     3,
     749,    93,    45,   752,    14,    15,    16,    88,    87,    65,
      65,   872,    91,   249,   788,   789,    65,   337,   338,   339,
     340,   879,    91,   797,   798,    91,    87,   801,   802,   199,
     200,   201,   202,   292,    92,    88,    88,    45,   690,    19,
      87,   690,   791,   835,   793,   835,    92,   835,    92,   910,
      18,    18,   835,    87,   706,    45,   914,   875,    91,   737,
     738,    92,   633,   715,   925,    94,    65,    65,   725,   726,
     727,   728,     5,     6,     7,     8,   733,   734,   735,   736,
      45,    14,    15,    16,    87,    92,     1,   836,   837,     4,
     839,   188,     7,     8,    65,    91,    87,     2,   471,    14,
      15,    16,   199,   314,   201,   663,   203,   633,   205,   316,
     633,   316,   451,   922,   211,    30,   213,    32,    33,   690,
     440,   441,   931,   633,   444,   445,   304,   224,     5,     6,
       7,     8,   469,   230,   633,   706,   765,    14,    15,    16,
     648,   681,   681,   240,   715,   404,   801,     1,   788,   344,
       4,     5,     6,     7,     8,    32,   467,   254,   255,   750,
      14,    15,    16,   333,   334,   335,   336,   753,   265,   297,
     267,     4,    87,    88,     7,     8,    30,   465,   352,   882,
     533,    14,    15,    16,   312,     5,     6,     7,     8,   298,
     449,   450,   894,   493,    14,    15,    16,    30,   549,    32,
     895,    -1,   438,   439,   440,   441,   442,   443,   444,   445,
      87,   308,    32,    -1,     1,    -1,     3,     4,     5,     6,
       7,     8,    -1,    -1,    -1,    12,    -1,    14,    15,    16,
      -1,   490,   491,   469,    -1,    -1,   333,    -1,   335,    93,
     337,    -1,   339,    30,    -1,    -1,    -1,   344,    -1,    27,
      28,    29,    30,     5,     6,     7,     8,    35,    36,    37,
      38,    -1,    14,    15,    16,    -1,    -1,    87,   438,   439,
      -1,    -1,   442,   443,    -1,    -1,    -1,    -1,     4,     5,
       6,     7,     8,    -1,   412,   413,   414,   415,    14,    15,
      16,    51,    52,    53,    54,    55,    56,    57,    58,    59,
       1,   398,     3,     4,    30,    92,     7,     8,    -1,    10,
      11,    -1,    13,    14,    15,    16,    -1,    -1,    -1,    -1,
      -1,    -1,   419,    -1,   452,    -1,    -1,   455,    -1,    30,
      31,    32,    33,    -1,    35,    36,    37,    38,    39,    40,
      41,   438,    -1,   440,    -1,   442,    47,   444,    -1,    50,
      -1,    -1,    -1,    -1,    55,    56,    57,    -1,    -1,    60,
      61,    87,    -1,    -1,    65,     7,     8,    -1,    -1,    -1,
     629,   630,    14,    15,    16,   472,    -1,    -1,    -1,   476,
      -1,    -1,    -1,     4,     5,     6,     7,     8,    89,    90,
      -1,    -1,    93,    14,    15,    16,    -1,    -1,    -1,   527,
      -1,    -1,    -1,    -1,    -1,   725,   726,   727,   728,    30,
      -1,    32,    -1,   733,   734,   735,   736,    -1,    -1,    -1,
      -1,   199,   200,   201,   202,     1,    -1,    -1,     4,    -1,
       1,     7,     8,     4,     5,     6,     7,     8,    14,    15,
      16,    -1,    -1,    14,    15,    16,    -1,   544,    -1,    -1,
     709,   710,    -1,    -1,    30,    -1,    32,    33,    29,    30,
      -1,    32,    -1,    -1,    -1,    -1,    87,    -1,    -1,    -1,
      -1,    42,     5,     6,     7,     8,    -1,   605,   606,   607,
      -1,    14,    15,    16,    -1,   721,   722,   723,   724,   725,
     726,   727,   728,   729,   730,   731,   732,   733,   734,   735,
     736,    -1,     4,    -1,     1,     7,     8,     4,     5,     6,
       7,     8,    14,    15,    16,    -1,    87,    14,    15,    16,
      91,    -1,    93,    -1,    -1,    -1,    -1,    -1,    30,   788,
     789,    -1,    29,    30,     1,    32,   633,     4,   797,   798,
       7,     8,   801,   802,    -1,    42,    -1,    14,    15,    16,
      -1,   721,   722,   723,   724,   333,   334,   335,   336,   729,
     730,   731,   732,    30,    -1,    32,    -1,    -1,    65,    66,
      -1,   830,   831,   832,    -1,     4,     5,     6,     7,     8,
      -1,    -1,    -1,    -1,    -1,    14,    15,    16,    -1,    -1,
      87,    -1,    -1,   690,    91,    -1,    93,    -1,    -1,    -1,
       1,    30,    -1,     4,     5,     6,     7,     8,    -1,   706,
      -1,    12,    -1,    14,    15,    16,    -1,   714,   715,    -1,
      -1,    -1,    -1,    -1,   721,    -1,   723,    -1,   725,    30,
     727,    -1,   729,    -1,   731,    -1,   733,    -1,   735,    -1,
      -1,    -1,    -1,    -1,   772,   773,     5,     6,     7,     8,
     778,   252,   253,    -1,    -1,    14,    15,    16,    87,    -1,
     438,   439,   263,    -1,   442,   443,    -1,    -1,   269,   270,
      -1,    -1,    -1,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,    -1,     3,    -1,   289,    -1,
      -1,    92,    -1,    10,    11,   823,    13,    -1,    -1,    -1,
     828,   829,     5,     6,     7,     8,    -1,    -1,    -1,   310,
      -1,    14,    15,    16,    31,    -1,    33,    -1,    35,    36,
      37,    38,    39,    40,    41,    -1,    -1,    -1,    87,    32,
      47,    -1,    -1,    50,    -1,    -1,    -1,    -1,    55,    56,
      57,   869,    -1,    60,    61,    -1,    -1,    -1,    65,    -1,
      -1,    -1,    -1,    -1,   882,     4,     5,     6,     7,     8,
       5,     6,     7,     8,    -1,    14,    15,    16,    -1,    14,
      15,    16,    89,    90,    -1,    -1,    93,   378,   379,    -1,
      -1,    30,    -1,   911,   912,    -1,     1,    32,     3,     4,
       5,     6,     7,     8,    -1,    10,    11,    -1,    13,    14,
      15,    16,    17,    -1,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    -1,
      35,    36,    37,    38,    39,    40,    41,   428,   429,    -1,
      -1,    -1,    47,    -1,    -1,    50,    -1,    -1,    87,    -1,
      55,    56,    57,    -1,    -1,    60,    61,    -1,    -1,    -1,
      65,    -1,    -1,    -1,     4,     5,     6,     7,     8,    -1,
      -1,    -1,    -1,   464,    14,    15,    16,     1,    -1,     3,
     471,    -1,    87,   474,    89,    90,    10,    11,    93,    13,
      30,    -1,    32,   484,    -1,    -1,    -1,   488,    52,    53,
      54,    55,    56,    57,    58,    59,    -1,    31,    -1,    33,
      -1,    35,    36,    37,    38,    39,    40,    41,    42,     5,
       6,     7,     8,    47,    -1,    -1,    50,   518,    14,    15,
      16,    55,    56,    57,    -1,    -1,    60,    61,    -1,    -1,
      64,    65,    66,    -1,    -1,    -1,    32,    87,     5,     6,
       7,     8,    -1,   721,   722,   723,   724,    14,    15,    16,
      -1,   729,   730,   731,   732,    89,    90,    -1,    -1,    93,
      -1,    -1,    -1,    -1,   565,    32,    -1,    -1,    -1,    -1,
      -1,   572,    -1,    -1,    -1,   576,    -1,    -1,    -1,    -1,
      -1,    -1,     1,    -1,     3,     4,     5,     6,     7,     8,
      -1,    10,    11,   594,    13,    14,    15,    16,    17,    -1,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,     4,     5,     6,     7,     8,    47,    -1,
      -1,    50,    -1,    14,    15,    16,    55,    56,    57,    -1,
      -1,    60,    61,    -1,    -1,    -1,    65,   648,    -1,    30,
     651,    32,    -1,     4,   655,    -1,     7,     8,    -1,    -1,
      -1,    -1,   663,    14,    15,    16,   667,    -1,    87,    88,
      89,    90,   673,    -1,    93,    -1,    -1,    -1,    -1,    30,
      -1,    32,   683,   684,    -1,     1,    -1,     3,     4,    -1,
      -1,   692,    -1,    -1,    10,    11,    -1,    13,    -1,    -1,
      -1,     4,     5,     6,     7,     8,    87,    -1,    -1,    -1,
      -1,    14,    15,    16,    -1,    31,    -1,    33,    -1,    35,
      36,    37,    38,    39,    40,    41,    -1,    30,    -1,    32,
      -1,    47,    -1,    -1,    50,    -1,    -1,    -1,    -1,    55,
      56,    57,    -1,   744,    60,    61,    -1,    -1,    64,    65,
      66,    -1,    -1,     1,    -1,     3,     4,    -1,    -1,    -1,
      -1,   762,    10,    11,   765,    13,   767,    -1,    -1,    -1,
      -1,    -1,    88,    89,    90,    -1,    -1,    93,    -1,    -1,
      -1,    -1,    -1,    31,    87,    33,    -1,    35,    36,    37,
      38,    39,    40,    41,    -1,     5,     6,     7,     8,    47,
      -1,    -1,    50,    -1,    14,    15,    16,    55,    56,    57,
      -1,   812,    60,    61,   815,    -1,    64,    65,    66,     1,
      -1,     3,    32,    -1,    -1,    -1,    -1,    -1,    10,    11,
      -1,    13,    53,    54,    55,    56,    57,    58,    59,    -1,
      88,    89,    90,    -1,    -1,    93,    -1,    -1,    -1,    31,
      -1,    33,    -1,    35,    36,    37,    38,    39,    40,    41,
       1,    -1,     3,    -1,    -1,    47,    -1,    -1,    50,    10,
      11,    -1,    13,    55,    56,    57,    -1,    87,    60,    61,
      -1,    -1,    -1,    65,    -1,    -1,    -1,    -1,    -1,    -1,
      31,    -1,    33,    -1,    35,    36,    37,    38,    39,    40,
      41,    -1,    -1,    -1,    -1,    -1,    47,    89,    90,    50,
      -1,    93,    -1,    -1,    55,    56,    57,    -1,    -1,    60,
      61,    -1,     1,    -1,    65,     4,     5,     6,     7,     8,
      -1,     5,     6,     7,     8,    14,    15,    16,    -1,    -1,
      14,    15,    16,    -1,    -1,    -1,    -1,     3,    89,    90,
      29,    30,    93,    32,    10,    11,    -1,    13,    32,    -1,
      -1,    -1,    -1,    42,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    31,    -1,    33,    -1,    35,
      36,    37,    38,    39,    40,    41,    65,    66,     3,    -1,
      -1,    47,    -1,    -1,    50,    10,    11,    -1,    13,    55,
      56,    57,    -1,    -1,    60,    61,    -1,    -1,    87,    65,
      -1,    -1,    91,    87,    93,    -1,    31,    -1,    33,    -1,
      35,    36,    37,    38,    39,    40,    41,    -1,    -1,    -1,
      -1,    -1,    47,    89,    90,    50,    -1,    -1,    94,    -1,
      55,    56,    57,    -1,    -1,    60,    61,    -1,    -1,    -1,
      65,    -1,    -1,     5,     6,     7,     8,     5,     6,     7,
       8,    -1,    14,    15,    16,    -1,    14,    15,    16,    -1,
      -1,    -1,    -1,    -1,    89,    90,    -1,    -1,    -1,    94,
       3,     4,     5,     6,     7,     8,    -1,    10,    11,    -1,
      13,    14,    15,    16,    17,    -1,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    -1,    35,    36,    37,    38,    39,    40,    41,     4,
       5,     6,     7,     8,    47,    -1,    -1,    50,    -1,    14,
      15,    16,    55,    56,    57,    87,    -1,    60,    61,    87,
      -1,    -1,    65,    -1,    -1,    30,    -1,    32,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,     3,     4,    -1,    -1,    -1,
      -1,    -1,    10,    11,    87,    13,    89,    90,    -1,    17,
      93,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    -1,    31,    -1,    33,    -1,    35,    36,    37,
      38,    39,    40,    41,     1,    -1,     3,    -1,    -1,    47,
      -1,    -1,    50,    10,    11,    -1,    13,    55,    56,    57,
      -1,    -1,    60,    61,    -1,    -1,    -1,    65,    -1,    -1,
      -1,    -1,    -1,    -1,    31,    -1,    33,    -1,    35,    36,
      37,    38,    39,    40,    41,    -1,    -1,    -1,    -1,    87,
      47,    89,    90,    50,    -1,    93,    -1,    -1,    55,    56,
      57,    -1,    -1,    60,    61,    -1,    -1,    -1,    65,     3,
       4,     5,     6,     7,     8,    -1,    10,    11,    -1,    13,
      14,    15,    16,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    89,    90,    -1,    -1,    30,    31,    32,    33,
      -1,    35,    36,    37,    38,    39,    40,    41,     4,     5,
       6,     7,     8,    47,    -1,    -1,    50,    -1,    14,    15,
      16,    55,    56,    57,    -1,    -1,    60,    61,    -1,    -1,
      -1,    65,     3,     4,    30,    -1,     7,     8,    -1,    10,
      11,    -1,    13,    14,    15,    16,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    89,    90,    -1,    -1,    30,
      31,    32,    33,    -1,    35,    36,    37,    38,    39,    40,
      41,    -1,    -1,     3,    -1,    -1,    47,    -1,    -1,    50,
      10,    11,    -1,    13,    55,    56,    57,    -1,    -1,    60,
      61,    -1,    -1,    -1,    65,    -1,    -1,    -1,    -1,    -1,
      -1,    31,    -1,    33,    -1,    35,    36,    37,    38,    39,
      40,    41,    -1,    -1,     3,    -1,    -1,    47,    89,    90,
      50,    10,    11,    -1,    13,    55,    56,    57,    -1,    -1,
      60,    61,    -1,    -1,    -1,    65,    -1,    -1,    -1,    -1,
      -1,    -1,    31,    -1,    33,    -1,    35,    36,    37,    38,
      39,    40,    41,    -1,    -1,     3,    -1,    87,    47,    89,
      90,    50,    10,    11,    -1,    13,    55,    56,    57,    -1,
      -1,    60,    61,    -1,    -1,    -1,    65,    -1,    -1,    -1,
      -1,    -1,    -1,    31,    -1,    33,    -1,    35,    36,    37,
      38,    39,    40,    41,    -1,    -1,     3,    -1,    -1,    47,
      89,    90,    50,    10,    11,    -1,    13,    55,    56,    57,
      -1,    -1,    60,    61,    -1,    -1,    -1,    65,    -1,    -1,
      -1,    -1,    -1,    -1,    31,    -1,    33,    -1,    35,    36,
      37,    38,    39,    40,    41,    -1,    -1,     3,    -1,    -1,
      47,    89,    90,    50,    10,    11,    -1,    13,    55,    56,
      57,    -1,     4,    60,    61,     7,     8,    -1,    65,    -1,
      -1,    -1,    14,    15,    16,    31,    -1,    33,    -1,    35,
      36,    37,    38,    39,    40,    41,    -1,    -1,    30,    -1,
      32,    47,    89,    90,    50,    -1,    -1,    -1,    -1,    55,
      56,    57,    -1,    -1,    60,    61,    -1,    -1,     1,    65,
       3,     4,     5,     6,     7,     8,    12,    -1,    -1,    -1,
      -1,    14,    15,    16,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    89,    90,    -1,    29,    30,    -1,    32,
      33,    -1,    -1,    -1,    -1,    -1,    42,    43,    44,    -1,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    57,    -1,    -1,    -1,    -1,    42,
      43,    44,    65,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    87,    42,    43,    44,    94,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    -1,    -1,    -1,    -1,    -1,    42,    43,
      44,    94,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    42,    43,    44,    94,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    -1,    -1,    -1,    -1,    -1,    42,    43,    44,
      94,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    42,    43,    44,    94,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    -1,    -1,    -1,    42,    43,    44,    92,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    -1,    -1,    12,    -1,    -1,    -1,    -1,    42,
      43,    44,    91,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    -1,    -1,    -1,
      -1,    -1,    -1,    91,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    -1,     1,    -1,    -1,     4,    -1,    91,     7,
       8,     4,     5,     6,     7,     8,    14,    15,    16,    12,
      -1,    14,    15,    16,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    30,    -1,    32,    -1,    -1,    30,    -1,    32,
       4,     5,     6,     7,     8,     4,     5,     6,     7,     8,
      14,    15,    16,    -1,    -1,    14,    15,    16,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    30,    -1,    32,    -1,
      -1,    30,    -1,    32,     4,     5,     6,     7,     8,    -1,
      -1,    -1,    -1,    -1,    14,    15,    16,    -1,    -1,    -1,
      -1,    -1,    -1,    32,    -1,    -1,    -1,    -1,    -1,    -1,
      30,    -1,    32,    42,    43,    44,    -1,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,     4,     5,     6,     7,     8,     4,     5,     6,     7,
       8,    14,    15,    16,    -1,    -1,    14,    15,    16,     4,
       5,     6,     7,     8,    -1,    -1,    -1,    30,    -1,    14,
      15,    16,    30,    -1,    -1,    -1,     4,     5,     6,     7,
       8,    -1,    -1,    -1,    -1,    30,    14,    15,    16,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    -1,    30,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    42,    43,    44,    -1,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned short int yystos[] =
{
       0,    96,    97,   101,     0,   101,    98,    99,     1,     4,
       5,     6,     7,     8,    14,    15,    16,    29,    30,    32,
      33,    87,   100,   102,   103,   118,   135,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   161,   163,   164,   165,   166,
     167,   175,   176,   180,   204,   205,   206,   207,   212,   275,
     277,   303,   100,    87,    88,   175,   175,   175,     1,   286,
       1,   286,    65,     3,    57,    65,   169,   172,   203,     8,
     163,   164,   175,   180,     8,   163,   164,   180,     8,   163,
     164,   175,   180,     8,   163,   164,   180,     8,   165,   166,
     175,   180,     8,   165,   166,   180,     8,   165,   166,   175,
     180,     8,   165,   166,   180,     8,   163,   164,   175,   180,
       8,   163,   164,   180,     8,   163,   164,   175,   180,     8,
     163,   164,   180,     8,   165,   166,   175,   180,     8,   165,
     166,   180,     8,   165,   166,   175,   180,     8,   165,   166,
     180,   135,   135,    87,   176,     3,     4,    93,   110,    93,
     110,    93,   110,    87,   100,   287,    65,   287,    65,     3,
      10,    11,    13,    31,    35,    36,    37,    38,    39,    40,
      41,    47,    50,    55,    56,    57,    60,    61,    65,    89,
      90,   111,   112,   115,   116,   117,   119,   120,   126,   138,
     139,   140,   141,   142,   143,   144,   145,   160,   224,   248,
     303,   138,   139,   140,   141,   159,   162,   174,   175,    87,
      91,     1,    29,    65,    66,   108,   232,   275,   276,     4,
      57,    65,   168,   170,   198,   199,   203,   169,   203,   215,
     216,    93,   215,    93,   211,    93,    87,    11,   285,    65,
     119,   119,    65,    65,    65,    65,   110,   119,     1,    93,
     112,   224,   119,    91,    92,    65,   115,    65,   115,    42,
      43,    44,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    64,    65,
      66,    67,   225,    92,     1,    34,    88,   241,   242,   243,
     246,   119,   203,   203,   136,   174,   174,   298,     6,   159,
     162,     1,   130,   131,   132,   239,   250,   174,   162,   174,
      87,    91,     1,   104,   276,    65,   232,    87,     1,   106,
      88,     1,    87,   138,   139,   140,   141,   142,   143,   144,
     145,   158,   159,   217,   303,   208,    88,   209,     1,   110,
     222,   223,   210,    92,     7,     8,   110,   177,   178,   179,
     180,   120,     1,   120,     1,   224,     1,   224,    92,    92,
      92,   120,   224,   224,   120,   120,   123,   125,   122,   121,
     120,   120,   120,   120,   120,   120,   120,   120,   120,   120,
     120,   120,   110,   113,   114,   120,   112,   110,    57,    65,
     226,   228,   229,   230,   231,   232,    92,   110,   302,     1,
     134,   233,   234,   235,   236,   237,   238,   239,   247,   250,
     253,   254,   243,    92,    92,   172,   203,   299,   162,     6,
      57,    94,   120,    87,   250,   239,   131,   133,   138,   139,
     142,   143,   146,   147,   150,   151,   156,   157,    42,   199,
     199,   136,   130,   174,   298,   130,   174,   135,   135,    87,
     217,   215,   174,   215,    42,    91,   214,   222,   287,    91,
      92,    65,    91,    92,    91,    92,    91,    92,    91,    93,
     119,    92,    92,   112,    45,   120,   120,    92,    91,    94,
     162,   174,   288,    65,   232,    87,    91,   134,   253,   254,
     134,   253,   254,   250,   253,   254,   134,   253,   254,   239,
      88,     3,     4,    17,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    87,    93,   110,   112,   137,
     154,   155,   161,   245,   249,   258,   261,   262,   269,   270,
     272,   273,   274,   278,   303,     1,     3,    12,   156,   157,
     290,   293,   294,   296,   300,   301,   120,   120,    94,    94,
     109,    87,   135,    87,   135,   173,    92,   170,   198,   250,
      42,   250,    45,   198,   218,   220,    45,   203,   219,   221,
      88,    88,   120,   223,    88,   214,   178,    92,     3,   113,
     224,   120,   224,   128,   127,    45,   120,   120,   229,   230,
     228,   289,   174,   288,   110,   240,   240,   240,   240,   240,
     120,    45,    87,    87,    87,   112,    57,   110,     8,   280,
     250,    87,   135,   135,    87,   246,   137,    92,   135,   297,
     297,    92,    87,    91,    91,    92,    94,    94,     1,   244,
     249,   168,   169,     1,    93,   120,   181,   105,   171,   107,
     120,    45,   174,    91,   120,    45,   174,    91,   174,   174,
     174,    88,   287,    91,    92,    92,    92,    91,    92,   110,
     129,     1,    64,    66,    93,   110,   120,   183,   184,   185,
     187,   189,   190,   191,   124,    92,   290,   250,   250,   250,
      65,    65,    12,    45,    87,   112,    87,   286,    45,   168,
     192,   198,   169,   195,   203,     4,    57,    65,   200,   201,
     202,   203,   227,   228,   229,    57,    65,   203,   227,   291,
      12,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   295,
       3,   249,    87,    87,   182,   244,   181,   244,   174,   120,
     136,   174,   120,   136,   174,   114,   120,    64,    66,    92,
     110,   120,   188,    45,    88,    91,   213,    42,   190,   191,
     187,   120,    65,    65,   259,   112,   137,   265,   266,   112,
     120,    87,    65,   174,    87,   193,    87,   196,   162,   174,
     174,    65,   232,    65,   232,   174,   175,   162,   174,   174,
     174,   135,   135,   183,   174,   220,   174,   221,    92,    92,
     110,   112,    12,    94,   183,   186,   185,   187,   250,   255,
     255,   260,    87,   250,    92,    45,   279,   285,   130,   130,
     201,   202,   202,   298,   298,   292,   200,   203,   227,   203,
     227,    88,    94,   120,    88,   187,   112,    92,    92,   240,
     252,   250,   267,   271,    92,    45,   250,   250,    92,   290,
     174,   174,   174,    94,   240,   252,   256,   257,   259,   251,
      19,   265,    87,   259,   287,    66,   281,   282,   283,   285,
     194,   197,   251,    18,    18,   260,   253,   254,   263,   265,
     268,   252,    87,   110,    45,    91,   287,   249,   249,    87,
     258,   240,   257,   257,   252,   264,    92,    94,   281,   283,
      65,   251,    65,   259,   285,    45,   112,   255,   260,   287,
     284,   285,    92,    92,   252,    65,    91,   286,    87,   112,
     285,    92,   286
};

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
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


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
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { 								\
      yyerror ("syntax error: cannot back up");\
      YYERROR;							\
    }								\
while (0)


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (N)								\
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
    while (0)
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if YYLTYPE_IS_TRIVIAL
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
# define YYLEX yylex (YYLEX_PARAM)
#else
# define YYLEX yylex ()
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
} while (0)

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)		\
do {								\
  if (yydebug)							\
    {								\
      YYFPRINTF (stderr, "%s ", Title);				\
      yysymprint (stderr, 					\
                  Type, Value);	\
      YYFPRINTF (stderr, "\n");					\
    }								\
} while (0)

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_stack_print (short int *bottom, short int *top)
#else
static void
yy_stack_print (bottom, top)
    short int *bottom;
    short int *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (/* Nothing. */; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_reduce_print (int yyrule)
#else
static void
yy_reduce_print (yyrule)
    int yyrule;
#endif
{
  int yyi;
  unsigned int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %u), ",
             yyrule - 1, yylno);
  /* Print the symbols being reduced, and their result.  */
  for (yyi = yyprhs[yyrule]; 0 <= yyrhs[yyi]; yyi++)
    YYFPRINTF (stderr, "%s ", yytname [yyrhs[yyi]]);
  YYFPRINTF (stderr, "-> %s\n", yytname [yyr1[yyrule]]);
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (Rule);		\
} while (0)

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
   SIZE_MAX < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

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

#endif /* !YYERROR_VERBOSE */



#if YYDEBUG
/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yysymprint (FILE *yyoutput, int yytype, YYSTYPE *yyvaluep)
#else
static void
yysymprint (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);


# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  switch (yytype)
    {
      default:
        break;
    }
  YYFPRINTF (yyoutput, ")");
}

#endif /* ! YYDEBUG */
/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yymsg, yytype, yyvaluep)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

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
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM);
# else
int yyparse ();
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */



/* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;



/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM)
# else
int yyparse (YYPARSE_PARAM)
  void *YYPARSE_PARAM;
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  
  register int yystate;
  register int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  short int yyssa[YYINITDEPTH];
  short int *yyss = yyssa;
  register short int *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  register YYSTYPE *yyvsp;



#define YYPOPSTACK   (yyvsp--, yyssp--)

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* When reducing, the number of symbols on the RHS of the reduced
     rule.  */
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


  yyvsp[0] = yylval;

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

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short int *yyss1 = yyss;


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyoverflowlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyoverflowlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	short int *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyoverflowlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);

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

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

/* Do appropriate processing given the current state.  */
/* Read a look-ahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to look-ahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
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

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;


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

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 342 "c-parse.y"
    { if (pedantic)
		    pedwarn ("ISO C forbids an empty source file");
		;}
    break;

  case 4:
#line 353 "c-parse.y"
    { (yyval.dsptype) = NULL; ;}
    break;

  case 5:
#line 354 "c-parse.y"
    { obstack_free (&parser_obstack, (yyvsp[-2].otype)); ;}
    break;

  case 6:
#line 356 "c-parse.y"
    { (yyval.dsptype) = NULL; ggc_collect (); ;}
    break;

  case 7:
#line 357 "c-parse.y"
    { obstack_free (&parser_obstack, (yyvsp[-2].otype)); ;}
    break;

  case 11:
#line 365 "c-parse.y"
    { RESTORE_EXT_FLAGS ((yyvsp[-1].itype)); ;}
    break;

  case 12:
#line 371 "c-parse.y"
    { (yyval.otype) = obstack_alloc (&parser_obstack, 0); ;}
    break;

  case 13:
#line 376 "c-parse.y"
    { pedwarn ("data definition has no type or storage class");
		  POP_DECLSPEC_STACK; ;}
    break;

  case 14:
#line 379 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 15:
#line 381 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 16:
#line 383 "c-parse.y"
    { shadow_tag (finish_declspecs ((yyvsp[-1].dsptype))); ;}
    break;

  case 19:
#line 387 "c-parse.y"
    { if (pedantic)
		    pedwarn ("ISO C does not allow extra %<;%> outside of a function"); ;}
    break;

  case 20:
#line 393 "c-parse.y"
    { if (!start_function (current_declspecs, (yyvsp[0].dtrtype),
				       all_prefix_attributes))
		    YYERROR1;
		;}
    break;

  case 21:
#line 398 "c-parse.y"
    { DECL_SOURCE_LOCATION (current_function_decl) = (yyvsp[0].location);
		  store_parm_decls (); ;}
    break;

  case 22:
#line 401 "c-parse.y"
    { finish_function ();
		  POP_DECLSPEC_STACK; ;}
    break;

  case 23:
#line 404 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 24:
#line 406 "c-parse.y"
    { if (!start_function (current_declspecs, (yyvsp[0].dtrtype),
				       all_prefix_attributes))
		    YYERROR1;
		;}
    break;

  case 25:
#line 411 "c-parse.y"
    { DECL_SOURCE_LOCATION (current_function_decl) = (yyvsp[0].location);
		  store_parm_decls (); ;}
    break;

  case 26:
#line 414 "c-parse.y"
    { finish_function ();
		  POP_DECLSPEC_STACK; ;}
    break;

  case 27:
#line 417 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 28:
#line 419 "c-parse.y"
    { if (!start_function (current_declspecs, (yyvsp[0].dtrtype),
				       all_prefix_attributes))
		    YYERROR1;
		;}
    break;

  case 29:
#line 424 "c-parse.y"
    { DECL_SOURCE_LOCATION (current_function_decl) = (yyvsp[0].location);
		  store_parm_decls (); ;}
    break;

  case 30:
#line 427 "c-parse.y"
    { finish_function ();
		  POP_DECLSPEC_STACK; ;}
    break;

  case 31:
#line 430 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 34:
#line 439 "c-parse.y"
    { (yyval.code) = ADDR_EXPR; ;}
    break;

  case 35:
#line 441 "c-parse.y"
    { (yyval.code) = NEGATE_EXPR; ;}
    break;

  case 36:
#line 443 "c-parse.y"
    { (yyval.code) = CONVERT_EXPR;
  if (warn_traditional && !in_system_header)
    warning ("traditional C rejects the unary plus operator");
		;}
    break;

  case 37:
#line 448 "c-parse.y"
    { (yyval.code) = PREINCREMENT_EXPR; ;}
    break;

  case 38:
#line 450 "c-parse.y"
    { (yyval.code) = PREDECREMENT_EXPR; ;}
    break;

  case 39:
#line 452 "c-parse.y"
    { (yyval.code) = BIT_NOT_EXPR; ;}
    break;

  case 40:
#line 454 "c-parse.y"
    { (yyval.code) = TRUTH_NOT_EXPR; ;}
    break;

  case 42:
#line 459 "c-parse.y"
    { (yyval.exprtype).value = build_compound_expr ((yyvsp[-2].exprtype).value, (yyvsp[0].exprtype).value);
		  (yyval.exprtype).original_code = COMPOUND_EXPR; ;}
    break;

  case 43:
#line 465 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 45:
#line 471 "c-parse.y"
    { (yyval.ttype) = build_tree_list (NULL_TREE, (yyvsp[0].exprtype).value); ;}
    break;

  case 46:
#line 473 "c-parse.y"
    { chainon ((yyvsp[-2].ttype), build_tree_list (NULL_TREE, (yyvsp[0].exprtype).value)); ;}
    break;

  case 48:
#line 479 "c-parse.y"
    { (yyval.exprtype).value = build_indirect_ref ((yyvsp[0].exprtype).value, "unary *");
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 49:
#line 483 "c-parse.y"
    { (yyval.exprtype) = (yyvsp[0].exprtype);
		  RESTORE_EXT_FLAGS ((yyvsp[-1].itype)); ;}
    break;

  case 50:
#line 486 "c-parse.y"
    { (yyval.exprtype).value = build_unary_op ((yyvsp[-1].code), (yyvsp[0].exprtype).value, 0);
		  overflow_warning ((yyval.exprtype).value);
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 51:
#line 491 "c-parse.y"
    { (yyval.exprtype).value = finish_label_address_expr ((yyvsp[0].ttype));
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 52:
#line 494 "c-parse.y"
    { skip_evaluation--;
		  in_sizeof--;
		  if (TREE_CODE ((yyvsp[0].exprtype).value) == COMPONENT_REF
		      && DECL_C_BIT_FIELD (TREE_OPERAND ((yyvsp[0].exprtype).value, 1)))
		    error ("%<sizeof%> applied to a bit-field");
		  (yyval.exprtype) = c_expr_sizeof_expr ((yyvsp[0].exprtype)); ;}
    break;

  case 53:
#line 501 "c-parse.y"
    { skip_evaluation--;
		  in_sizeof--;
		  (yyval.exprtype) = c_expr_sizeof_type ((yyvsp[-1].typenametype)); ;}
    break;

  case 54:
#line 505 "c-parse.y"
    { skip_evaluation--;
		  in_alignof--;
		  (yyval.exprtype).value = c_alignof_expr ((yyvsp[0].exprtype).value);
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 55:
#line 510 "c-parse.y"
    { skip_evaluation--;
		  in_alignof--;
		  (yyval.exprtype).value = c_alignof (groktypename ((yyvsp[-1].typenametype)));
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 56:
#line 515 "c-parse.y"
    { (yyval.exprtype).value = build_unary_op (REALPART_EXPR, (yyvsp[0].exprtype).value, 0);
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 57:
#line 518 "c-parse.y"
    { (yyval.exprtype).value = build_unary_op (IMAGPART_EXPR, (yyvsp[0].exprtype).value, 0);
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 58:
#line 523 "c-parse.y"
    { skip_evaluation++; in_sizeof++; ;}
    break;

  case 59:
#line 527 "c-parse.y"
    { skip_evaluation++; in_alignof++; ;}
    break;

  case 60:
#line 531 "c-parse.y"
    { skip_evaluation++; in_typeof++; ;}
    break;

  case 62:
#line 537 "c-parse.y"
    { (yyval.exprtype).value = c_cast_expr ((yyvsp[-2].typenametype), (yyvsp[0].exprtype).value);
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 64:
#line 544 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 65:
#line 546 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 66:
#line 548 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 67:
#line 550 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 68:
#line 552 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 69:
#line 554 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 70:
#line 556 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 71:
#line 558 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 72:
#line 560 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 73:
#line 562 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 74:
#line 564 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 75:
#line 566 "c-parse.y"
    { (yyval.exprtype) = parser_build_binary_op ((yyvsp[-1].code), (yyvsp[-2].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 76:
#line 568 "c-parse.y"
    { (yyvsp[-1].exprtype).value = lang_hooks.truthvalue_conversion
		    (default_conversion ((yyvsp[-1].exprtype).value));
		  skip_evaluation += (yyvsp[-1].exprtype).value == truthvalue_false_node; ;}
    break;

  case 77:
#line 572 "c-parse.y"
    { skip_evaluation -= (yyvsp[-3].exprtype).value == truthvalue_false_node;
		  (yyval.exprtype) = parser_build_binary_op (TRUTH_ANDIF_EXPR, (yyvsp[-3].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 78:
#line 575 "c-parse.y"
    { (yyvsp[-1].exprtype).value = lang_hooks.truthvalue_conversion
		    (default_conversion ((yyvsp[-1].exprtype).value));
		  skip_evaluation += (yyvsp[-1].exprtype).value == truthvalue_true_node; ;}
    break;

  case 79:
#line 579 "c-parse.y"
    { skip_evaluation -= (yyvsp[-3].exprtype).value == truthvalue_true_node;
		  (yyval.exprtype) = parser_build_binary_op (TRUTH_ORIF_EXPR, (yyvsp[-3].exprtype), (yyvsp[0].exprtype)); ;}
    break;

  case 80:
#line 582 "c-parse.y"
    { (yyvsp[-1].exprtype).value = lang_hooks.truthvalue_conversion
		    (default_conversion ((yyvsp[-1].exprtype).value));
		  skip_evaluation += (yyvsp[-1].exprtype).value == truthvalue_false_node; ;}
    break;

  case 81:
#line 586 "c-parse.y"
    { skip_evaluation += (((yyvsp[-4].exprtype).value == truthvalue_true_node)
				      - ((yyvsp[-4].exprtype).value == truthvalue_false_node)); ;}
    break;

  case 82:
#line 589 "c-parse.y"
    { skip_evaluation -= (yyvsp[-6].exprtype).value == truthvalue_true_node;
		  (yyval.exprtype).value = build_conditional_expr ((yyvsp[-6].exprtype).value, (yyvsp[-3].exprtype).value,
						     (yyvsp[0].exprtype).value);
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 83:
#line 594 "c-parse.y"
    { if (pedantic)
		    pedwarn ("ISO C forbids omitting the middle term of a ?: expression");
		  /* Make sure first operand is calculated only once.  */
		  (yyvsp[0].ttype) = save_expr (default_conversion ((yyvsp[-1].exprtype).value));
		  (yyvsp[-1].exprtype).value = lang_hooks.truthvalue_conversion ((yyvsp[0].ttype));
		  skip_evaluation += (yyvsp[-1].exprtype).value == truthvalue_true_node; ;}
    break;

  case 84:
#line 601 "c-parse.y"
    { skip_evaluation -= (yyvsp[-4].exprtype).value == truthvalue_true_node;
		  (yyval.exprtype).value = build_conditional_expr ((yyvsp[-4].exprtype).value, (yyvsp[-3].ttype),
						     (yyvsp[0].exprtype).value);
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 85:
#line 606 "c-parse.y"
    { (yyval.exprtype).value = build_modify_expr ((yyvsp[-2].exprtype).value, NOP_EXPR, (yyvsp[0].exprtype).value);
		  (yyval.exprtype).original_code = MODIFY_EXPR;
		;}
    break;

  case 86:
#line 610 "c-parse.y"
    { (yyval.exprtype).value = build_modify_expr ((yyvsp[-2].exprtype).value, (yyvsp[-1].code), (yyvsp[0].exprtype).value);
		  TREE_NO_WARNING ((yyval.exprtype).value) = 1;
		  (yyval.exprtype).original_code = ERROR_MARK;
		;}
    break;

  case 87:
#line 618 "c-parse.y"
    {
		  if (yychar == YYEMPTY)
		    yychar = YYLEX;
		  (yyval.exprtype).value = build_external_ref ((yyvsp[0].ttype), yychar == '(');
		  (yyval.exprtype).original_code = ERROR_MARK;
		;}
    break;

  case 88:
#line 625 "c-parse.y"
    { (yyval.exprtype).value = (yyvsp[0].ttype); (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 89:
#line 627 "c-parse.y"
    { (yyval.exprtype).value = (yyvsp[0].ttype); (yyval.exprtype).original_code = STRING_CST; ;}
    break;

  case 90:
#line 629 "c-parse.y"
    { (yyval.exprtype).value = fname_decl (C_RID_CODE ((yyvsp[0].ttype)), (yyvsp[0].ttype));
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 91:
#line 632 "c-parse.y"
    { start_init (NULL_TREE, NULL, 0);
		  (yyval.ttype) = groktypename ((yyvsp[-2].typenametype));
		  if (C_TYPE_VARIABLE_SIZE ((yyval.ttype)))
		    {
		      error ("compound literal has variable size");
		      (yyval.ttype) = error_mark_node;
		    }
		  really_start_incremental_init ((yyval.ttype)); ;}
    break;

  case 92:
#line 641 "c-parse.y"
    { struct c_expr init = pop_init_level (0);
		  tree constructor = init.value;
		  tree type = (yyvsp[-2].ttype);
		  finish_init ();
		  maybe_warn_string_init (type, init);

		  if (pedantic && !flag_isoc99)
		    pedwarn ("ISO C90 forbids compound literals");
		  (yyval.exprtype).value = build_compound_literal (type, constructor);
		  (yyval.exprtype).original_code = ERROR_MARK;
		;}
    break;

  case 93:
#line 653 "c-parse.y"
    { (yyval.exprtype).value = (yyvsp[-1].exprtype).value;
		  if (TREE_CODE ((yyval.exprtype).value) == MODIFY_EXPR)
		    TREE_NO_WARNING ((yyval.exprtype).value) = 1;
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 94:
#line 658 "c-parse.y"
    { (yyval.exprtype).value = error_mark_node; (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 95:
#line 660 "c-parse.y"
    { if (pedantic)
		    pedwarn ("ISO C forbids braced-groups within expressions");
		  (yyval.exprtype).value = c_finish_stmt_expr ((yyvsp[-2].ttype));
		  (yyval.exprtype).original_code = ERROR_MARK;
		;}
    break;

  case 96:
#line 666 "c-parse.y"
    { c_finish_stmt_expr ((yyvsp[-2].ttype));
		  (yyval.exprtype).value = error_mark_node;
		  (yyval.exprtype).original_code = ERROR_MARK;
		;}
    break;

  case 97:
#line 671 "c-parse.y"
    { (yyval.exprtype).value = build_function_call ((yyvsp[-3].exprtype).value, (yyvsp[-1].ttype));
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 98:
#line 674 "c-parse.y"
    { (yyval.exprtype).value = build_va_arg ((yyvsp[-3].exprtype).value, groktypename ((yyvsp[-1].typenametype)));
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 99:
#line 678 "c-parse.y"
    { tree type = groktypename ((yyvsp[-1].typenametype));
		  if (type == error_mark_node)
		    offsetof_base = error_mark_node;
		  else
		    offsetof_base = build1 (INDIRECT_REF, type, NULL);
		;}
    break;

  case 100:
#line 685 "c-parse.y"
    { (yyval.exprtype).value = fold_offsetof ((yyvsp[-1].ttype));
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 101:
#line 688 "c-parse.y"
    { (yyval.exprtype).value = error_mark_node; (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 102:
#line 691 "c-parse.y"
    {
                  tree c;

                  c = fold ((yyvsp[-5].exprtype).value);
                  STRIP_NOPS (c);
                  if (TREE_CODE (c) != INTEGER_CST)
                    error ("first argument to %<__builtin_choose_expr%> not"
			   " a constant");
                  (yyval.exprtype) = integer_zerop (c) ? (yyvsp[-1].exprtype) : (yyvsp[-3].exprtype);
		;}
    break;

  case 103:
#line 702 "c-parse.y"
    { (yyval.exprtype).value = error_mark_node; (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 104:
#line 704 "c-parse.y"
    {
		  tree e1, e2;

		  e1 = TYPE_MAIN_VARIANT (groktypename ((yyvsp[-3].typenametype)));
		  e2 = TYPE_MAIN_VARIANT (groktypename ((yyvsp[-1].typenametype)));

		  (yyval.exprtype).value = comptypes (e1, e2)
		    ? build_int_cst (NULL_TREE, 1)
		    : build_int_cst (NULL_TREE, 0);
		  (yyval.exprtype).original_code = ERROR_MARK;
		;}
    break;

  case 105:
#line 716 "c-parse.y"
    { (yyval.exprtype).value = error_mark_node; (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 106:
#line 718 "c-parse.y"
    { (yyval.exprtype).value = build_array_ref ((yyvsp[-3].exprtype).value, (yyvsp[-1].exprtype).value);
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 107:
#line 721 "c-parse.y"
    { (yyval.exprtype).value = build_component_ref ((yyvsp[-2].exprtype).value, (yyvsp[0].ttype));
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 108:
#line 724 "c-parse.y"
    {
                  tree expr = build_indirect_ref ((yyvsp[-2].exprtype).value, "->");
		  (yyval.exprtype).value = build_component_ref (expr, (yyvsp[0].ttype));
		  (yyval.exprtype).original_code = ERROR_MARK;
		;}
    break;

  case 109:
#line 730 "c-parse.y"
    { (yyval.exprtype).value = build_unary_op (POSTINCREMENT_EXPR, (yyvsp[-1].exprtype).value, 0);
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 110:
#line 733 "c-parse.y"
    { (yyval.exprtype).value = build_unary_op (POSTDECREMENT_EXPR, (yyvsp[-1].exprtype).value, 0);
		  (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 111:
#line 743 "c-parse.y"
    { (yyval.ttype) = build_component_ref (offsetof_base, (yyvsp[0].ttype)); ;}
    break;

  case 112:
#line 745 "c-parse.y"
    { (yyval.ttype) = build_component_ref ((yyvsp[-2].ttype), (yyvsp[0].ttype)); ;}
    break;

  case 113:
#line 747 "c-parse.y"
    { (yyval.ttype) = build_array_ref ((yyvsp[-3].ttype), (yyvsp[-1].exprtype).value); ;}
    break;

  case 116:
#line 760 "c-parse.y"
    { ;}
    break;

  case 121:
#line 776 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 122:
#line 778 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 123:
#line 780 "c-parse.y"
    { shadow_tag_warned (finish_declspecs ((yyvsp[-1].dsptype)), 1);
		  pedwarn ("empty declaration"); ;}
    break;

  case 124:
#line 783 "c-parse.y"
    { pedwarn ("empty declaration"); ;}
    break;

  case 125:
#line 792 "c-parse.y"
    { ;}
    break;

  case 126:
#line 800 "c-parse.y"
    { pending_xref_error ();
		  PUSH_DECLSPEC_STACK;
		  if ((yyvsp[0].dsptype))
		    {
		      prefix_attributes = (yyvsp[0].dsptype)->attrs;
		      (yyvsp[0].dsptype)->attrs = NULL_TREE;
		      current_declspecs = (yyvsp[0].dsptype);
		    }
		  else
		    {
		      prefix_attributes = NULL_TREE;
		      current_declspecs = build_null_declspecs ();
		    }
		  current_declspecs = finish_declspecs (current_declspecs);
		  all_prefix_attributes = prefix_attributes; ;}
    break;

  case 127:
#line 821 "c-parse.y"
    { all_prefix_attributes = chainon ((yyvsp[0].ttype), prefix_attributes); ;}
    break;

  case 128:
#line 826 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 129:
#line 828 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 130:
#line 830 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 131:
#line 832 "c-parse.y"
    { POP_DECLSPEC_STACK; ;}
    break;

  case 132:
#line 834 "c-parse.y"
    { shadow_tag (finish_declspecs ((yyvsp[-1].dsptype))); ;}
    break;

  case 133:
#line 836 "c-parse.y"
    { RESTORE_EXT_FLAGS ((yyvsp[-1].itype)); ;}
    break;

  case 134:
#line 882 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual (build_null_declspecs (), (yyvsp[0].ttype)); ;}
    break;

  case 135:
#line 884 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 136:
#line 886 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 137:
#line 891 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_attrs ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 138:
#line 896 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 139:
#line 898 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 140:
#line 903 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_attrs (build_null_declspecs (), (yyvsp[0].ttype)); ;}
    break;

  case 141:
#line 905 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_attrs ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 142:
#line 910 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type (build_null_declspecs (), (yyvsp[0].tstype)); ;}
    break;

  case 143:
#line 912 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 144:
#line 914 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 145:
#line 916 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 146:
#line 918 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 147:
#line 920 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 148:
#line 922 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 149:
#line 927 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type (build_null_declspecs (), (yyvsp[0].tstype)); ;}
    break;

  case 150:
#line 929 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_attrs ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 151:
#line 931 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 152:
#line 933 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 153:
#line 935 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 154:
#line 937 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 155:
#line 942 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 156:
#line 944 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 157:
#line 946 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 158:
#line 948 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 159:
#line 950 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 160:
#line 952 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 161:
#line 957 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_attrs ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 162:
#line 959 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 163:
#line 961 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 164:
#line 963 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 165:
#line 965 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 166:
#line 970 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec (build_null_declspecs (), (yyvsp[0].ttype)); ;}
    break;

  case 167:
#line 972 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 168:
#line 974 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 169:
#line 976 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 170:
#line 978 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 171:
#line 980 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 172:
#line 982 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 173:
#line 987 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_attrs ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 174:
#line 992 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 175:
#line 994 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 176:
#line 996 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 177:
#line 998 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 178:
#line 1000 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 179:
#line 1002 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 180:
#line 1007 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_attrs ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 181:
#line 1012 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 182:
#line 1014 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 183:
#line 1016 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 184:
#line 1018 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 185:
#line 1020 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 186:
#line 1022 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 187:
#line 1024 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 188:
#line 1026 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 189:
#line 1028 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 190:
#line 1030 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 191:
#line 1035 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_attrs ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 192:
#line 1037 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 193:
#line 1039 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 194:
#line 1041 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 195:
#line 1043 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 196:
#line 1048 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 197:
#line 1050 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_qual ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 198:
#line 1052 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 199:
#line 1054 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 200:
#line 1056 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 201:
#line 1058 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 202:
#line 1060 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 203:
#line 1062 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 204:
#line 1064 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 205:
#line 1066 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_scspec ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 206:
#line 1071 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_attrs ((yyvsp[-1].dsptype), (yyvsp[0].ttype)); ;}
    break;

  case 207:
#line 1073 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 208:
#line 1075 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 209:
#line 1077 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 210:
#line 1079 "c-parse.y"
    { (yyval.dsptype) = declspecs_add_type ((yyvsp[-1].dsptype), (yyvsp[0].tstype)); ;}
    break;

  case 267:
#line 1166 "c-parse.y"
    { (yyval.dsptype) = NULL; ;}
    break;

  case 268:
#line 1168 "c-parse.y"
    { (yyval.dsptype) = (yyvsp[0].dsptype); ;}
    break;

  case 272:
#line 1203 "c-parse.y"
    { OBJC_NEED_RAW_IDENTIFIER (1);
		  (yyval.tstype).kind = ctsk_resword;
		  (yyval.tstype).spec = (yyvsp[0].ttype); ;}
    break;

  case 275:
#line 1215 "c-parse.y"
    { /* For a typedef name, record the meaning, not the name.
		     In case of `foo foo, bar;'.  */
		  (yyval.tstype).kind = ctsk_typedef;
		  (yyval.tstype).spec = lookup_name ((yyvsp[0].ttype)); ;}
    break;

  case 276:
#line 1220 "c-parse.y"
    { skip_evaluation--;
		  in_typeof--;
		  if (TREE_CODE ((yyvsp[-1].exprtype).value) == COMPONENT_REF
		      && DECL_C_BIT_FIELD (TREE_OPERAND ((yyvsp[-1].exprtype).value, 1)))
		    error ("%<typeof%> applied to a bit-field");
		  (yyval.tstype).kind = ctsk_typeof;
		  (yyval.tstype).spec = TREE_TYPE ((yyvsp[-1].exprtype).value);
		  pop_maybe_used (variably_modified_type_p ((yyval.tstype).spec,
							    NULL_TREE)); ;}
    break;

  case 277:
#line 1230 "c-parse.y"
    { skip_evaluation--;
		  in_typeof--;
		  (yyval.tstype).kind = ctsk_typeof;
		  (yyval.tstype).spec = groktypename ((yyvsp[-1].typenametype));
		  pop_maybe_used (variably_modified_type_p ((yyval.tstype).spec,
							    NULL_TREE)); ;}
    break;

  case 282:
#line 1252 "c-parse.y"
    { (yyval.ttype) = start_decl ((yyvsp[-3].dtrtype), current_declspecs, true,
					  chainon ((yyvsp[-1].ttype), all_prefix_attributes));
		  if (!(yyval.ttype))
		    (yyval.ttype) = error_mark_node;
		  start_init ((yyval.ttype), (yyvsp[-2].ttype), global_bindings_p ()); ;}
    break;

  case 283:
#line 1259 "c-parse.y"
    { finish_init ();
		  if ((yyvsp[-1].ttype) != error_mark_node)
		    {
		      maybe_warn_string_init (TREE_TYPE ((yyvsp[-1].ttype)), (yyvsp[0].exprtype));
		      finish_decl ((yyvsp[-1].ttype), (yyvsp[0].exprtype).value, (yyvsp[-4].ttype));
		    }
		;}
    break;

  case 284:
#line 1267 "c-parse.y"
    { tree d = start_decl ((yyvsp[-2].dtrtype), current_declspecs, false,
				       chainon ((yyvsp[0].ttype), all_prefix_attributes));
		  if (d)
		    finish_decl (d, NULL_TREE, (yyvsp[-1].ttype));
                ;}
    break;

  case 285:
#line 1276 "c-parse.y"
    { (yyval.ttype) = start_decl ((yyvsp[-3].dtrtype), current_declspecs, true,
					  chainon ((yyvsp[-1].ttype), all_prefix_attributes));
		  if (!(yyval.ttype))
		    (yyval.ttype) = error_mark_node;
		  start_init ((yyval.ttype), (yyvsp[-2].ttype), global_bindings_p ()); ;}
    break;

  case 286:
#line 1283 "c-parse.y"
    { finish_init ();
		  if ((yyvsp[-1].ttype) != error_mark_node)
		    {
		      maybe_warn_string_init (TREE_TYPE ((yyvsp[-1].ttype)), (yyvsp[0].exprtype));
		      finish_decl ((yyvsp[-1].ttype), (yyvsp[0].exprtype).value, (yyvsp[-4].ttype));
		    }
		;}
    break;

  case 287:
#line 1291 "c-parse.y"
    { tree d = start_decl ((yyvsp[-2].dtrtype), current_declspecs, false,
				       chainon ((yyvsp[0].ttype), all_prefix_attributes));
		  if (d)
                    finish_decl (d, NULL_TREE, (yyvsp[-1].ttype)); ;}
    break;

  case 288:
#line 1300 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 289:
#line 1302 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 290:
#line 1307 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 291:
#line 1309 "c-parse.y"
    { (yyval.ttype) = chainon ((yyvsp[-1].ttype), (yyvsp[0].ttype)); ;}
    break;

  case 292:
#line 1315 "c-parse.y"
    { (yyval.ttype) = (yyvsp[-3].ttype); ;}
    break;

  case 293:
#line 1317 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 294:
#line 1322 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 295:
#line 1324 "c-parse.y"
    { (yyval.ttype) = chainon ((yyvsp[-2].ttype), (yyvsp[0].ttype)); ;}
    break;

  case 296:
#line 1329 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 297:
#line 1331 "c-parse.y"
    { (yyval.ttype) = build_tree_list ((yyvsp[0].ttype), NULL_TREE); ;}
    break;

  case 298:
#line 1333 "c-parse.y"
    { (yyval.ttype) = build_tree_list ((yyvsp[-3].ttype), build_tree_list (NULL_TREE, (yyvsp[-1].ttype))); ;}
    break;

  case 299:
#line 1335 "c-parse.y"
    { (yyval.ttype) = build_tree_list ((yyvsp[-5].ttype), tree_cons (NULL_TREE, (yyvsp[-3].ttype), (yyvsp[-1].ttype))); ;}
    break;

  case 300:
#line 1337 "c-parse.y"
    { (yyval.ttype) = build_tree_list ((yyvsp[-3].ttype), (yyvsp[-1].ttype)); ;}
    break;

  case 307:
#line 1359 "c-parse.y"
    { (yyval.exprtype) = (yyvsp[0].exprtype); ;}
    break;

  case 308:
#line 1361 "c-parse.y"
    { really_start_incremental_init (NULL_TREE); ;}
    break;

  case 309:
#line 1363 "c-parse.y"
    { (yyval.exprtype) = pop_init_level (0); ;}
    break;

  case 310:
#line 1365 "c-parse.y"
    { (yyval.exprtype).value = error_mark_node; (yyval.exprtype).original_code = ERROR_MARK; ;}
    break;

  case 311:
#line 1371 "c-parse.y"
    { if (pedantic)
		    pedwarn ("ISO C forbids empty initializer braces"); ;}
    break;

  case 315:
#line 1385 "c-parse.y"
    { if (pedantic && !flag_isoc99)
		    pedwarn ("ISO C90 forbids specifying subobject to initialize"); ;}
    break;

  case 316:
#line 1388 "c-parse.y"
    { if (pedantic)
		    pedwarn ("obsolete use of designated initializer without %<=%>"); ;}
    break;

  case 317:
#line 1391 "c-parse.y"
    { set_init_label ((yyvsp[-1].ttype));
		  if (pedantic)
		    pedwarn ("obsolete use of designated initializer with %<:%>"); ;}
    break;

  case 318:
#line 1395 "c-parse.y"
    {;}
    break;

  case 320:
#line 1401 "c-parse.y"
    { push_init_level (0); ;}
    break;

  case 321:
#line 1403 "c-parse.y"
    { process_init_element (pop_init_level (0)); ;}
    break;

  case 322:
#line 1405 "c-parse.y"
    { process_init_element ((yyvsp[0].exprtype)); ;}
    break;

  case 326:
#line 1416 "c-parse.y"
    { set_init_label ((yyvsp[0].ttype)); ;}
    break;

  case 328:
#line 1422 "c-parse.y"
    { set_init_index ((yyvsp[-3].exprtype).value, (yyvsp[-1].exprtype).value);
		  if (pedantic)
		    pedwarn ("ISO C forbids specifying range of elements to initialize"); ;}
    break;

  case 329:
#line 1426 "c-parse.y"
    { set_init_index ((yyvsp[-1].exprtype).value, NULL_TREE); ;}
    break;

  case 330:
#line 1431 "c-parse.y"
    { if (pedantic)
		    pedwarn ("ISO C forbids nested functions");

		  push_function_context ();
		  if (!start_function (current_declspecs, (yyvsp[0].dtrtype),
				       all_prefix_attributes))
		    {
		      pop_function_context ();
		      YYERROR1;
		    }
		;}
    break;

  case 331:
#line 1443 "c-parse.y"
    { tree decl = current_function_decl;
		  DECL_SOURCE_LOCATION (decl) = (yyvsp[0].location);
		  store_parm_decls (); ;}
    break;

  case 332:
#line 1452 "c-parse.y"
    { tree decl = current_function_decl;
		  add_stmt ((yyvsp[0].ttype));
		  finish_function ();
		  pop_function_context ();
		  add_stmt (build_stmt (DECL_EXPR, decl)); ;}
    break;

  case 333:
#line 1461 "c-parse.y"
    { if (pedantic)
		    pedwarn ("ISO C forbids nested functions");

		  push_function_context ();
		  if (!start_function (current_declspecs, (yyvsp[0].dtrtype),
				       all_prefix_attributes))
		    {
		      pop_function_context ();
		      YYERROR1;
		    }
		;}
    break;

  case 334:
#line 1473 "c-parse.y"
    { tree decl = current_function_decl;
		  DECL_SOURCE_LOCATION (decl) = (yyvsp[0].location);
		  store_parm_decls (); ;}
    break;

  case 335:
#line 1482 "c-parse.y"
    { tree decl = current_function_decl;
		  add_stmt ((yyvsp[0].ttype));
		  finish_function ();
		  pop_function_context ();
		  add_stmt (build_stmt (DECL_EXPR, decl)); ;}
    break;

  case 338:
#line 1501 "c-parse.y"
    { (yyval.dtrtype) = (yyvsp[-2].ttype) ? build_attrs_declarator ((yyvsp[-2].ttype), (yyvsp[-1].dtrtype)) : (yyvsp[-1].dtrtype); ;}
    break;

  case 339:
#line 1503 "c-parse.y"
    { (yyval.dtrtype) = build_function_declarator ((yyvsp[0].arginfotype), (yyvsp[-2].dtrtype)); ;}
    break;

  case 340:
#line 1505 "c-parse.y"
    { (yyval.dtrtype) = set_array_declarator_inner ((yyvsp[0].dtrtype), (yyvsp[-1].dtrtype), false); ;}
    break;

  case 341:
#line 1507 "c-parse.y"
    { (yyval.dtrtype) = make_pointer_declarator ((yyvsp[-1].dsptype), (yyvsp[0].dtrtype)); ;}
    break;

  case 342:
#line 1509 "c-parse.y"
    { (yyval.dtrtype) = build_id_declarator ((yyvsp[0].ttype)); ;}
    break;

  case 345:
#line 1523 "c-parse.y"
    { (yyval.dtrtype) = build_function_declarator ((yyvsp[0].arginfotype), (yyvsp[-2].dtrtype)); ;}
    break;

  case 346:
#line 1525 "c-parse.y"
    { (yyval.dtrtype) = set_array_declarator_inner ((yyvsp[0].dtrtype), (yyvsp[-1].dtrtype), false); ;}
    break;

  case 347:
#line 1527 "c-parse.y"
    { (yyval.dtrtype) = build_id_declarator ((yyvsp[0].ttype)); ;}
    break;

  case 348:
#line 1532 "c-parse.y"
    { (yyval.dtrtype) = build_function_declarator ((yyvsp[0].arginfotype), (yyvsp[-2].dtrtype)); ;}
    break;

  case 349:
#line 1534 "c-parse.y"
    { (yyval.dtrtype) = set_array_declarator_inner ((yyvsp[0].dtrtype), (yyvsp[-1].dtrtype), false); ;}
    break;

  case 350:
#line 1536 "c-parse.y"
    { (yyval.dtrtype) = make_pointer_declarator ((yyvsp[-1].dsptype), (yyvsp[0].dtrtype)); ;}
    break;

  case 351:
#line 1538 "c-parse.y"
    { (yyval.dtrtype) = make_pointer_declarator ((yyvsp[-1].dsptype), (yyvsp[0].dtrtype)); ;}
    break;

  case 352:
#line 1540 "c-parse.y"
    { (yyval.dtrtype) = (yyvsp[-2].ttype) ? build_attrs_declarator ((yyvsp[-2].ttype), (yyvsp[-1].dtrtype)) : (yyvsp[-1].dtrtype); ;}
    break;

  case 353:
#line 1548 "c-parse.y"
    { (yyval.dtrtype) = build_function_declarator ((yyvsp[0].arginfotype), (yyvsp[-2].dtrtype)); ;}
    break;

  case 354:
#line 1550 "c-parse.y"
    { (yyval.dtrtype) = (yyvsp[-2].ttype) ? build_attrs_declarator ((yyvsp[-2].ttype), (yyvsp[-1].dtrtype)) : (yyvsp[-1].dtrtype); ;}
    break;

  case 355:
#line 1552 "c-parse.y"
    { (yyval.dtrtype) = make_pointer_declarator ((yyvsp[-1].dsptype), (yyvsp[0].dtrtype)); ;}
    break;

  case 356:
#line 1554 "c-parse.y"
    { (yyval.dtrtype) = set_array_declarator_inner ((yyvsp[0].dtrtype), (yyvsp[-1].dtrtype), false); ;}
    break;

  case 357:
#line 1556 "c-parse.y"
    { (yyval.dtrtype) = build_id_declarator ((yyvsp[0].ttype)); ;}
    break;

  case 358:
#line 1561 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 359:
#line 1563 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 360:
#line 1568 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 361:
#line 1570 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 362:
#line 1575 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 363:
#line 1577 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 364:
#line 1588 "c-parse.y"
    { (yyval.ttype) = start_struct (RECORD_TYPE, (yyvsp[-1].ttype));
		  /* Start scope of tag before parsing components.  */
		;}
    break;

  case 365:
#line 1592 "c-parse.y"
    { (yyval.tstype).spec = finish_struct ((yyvsp[-3].ttype), nreverse ((yyvsp[-2].ttype)),
					   chainon ((yyvsp[-6].ttype), (yyvsp[0].ttype)));
		  (yyval.tstype).kind = ctsk_tagdef; ;}
    break;

  case 366:
#line 1596 "c-parse.y"
    { (yyval.tstype).spec = finish_struct (start_struct (RECORD_TYPE,
							 NULL_TREE),
					   nreverse ((yyvsp[-2].ttype)), chainon ((yyvsp[-4].ttype), (yyvsp[0].ttype)));
		  (yyval.tstype).kind = ctsk_tagdef;
		;}
    break;

  case 367:
#line 1602 "c-parse.y"
    { (yyval.ttype) = start_struct (UNION_TYPE, (yyvsp[-1].ttype)); ;}
    break;

  case 368:
#line 1604 "c-parse.y"
    { (yyval.tstype).spec = finish_struct ((yyvsp[-3].ttype), nreverse ((yyvsp[-2].ttype)),
					   chainon ((yyvsp[-6].ttype), (yyvsp[0].ttype)));
		  (yyval.tstype).kind = ctsk_tagdef; ;}
    break;

  case 369:
#line 1608 "c-parse.y"
    { (yyval.tstype).spec = finish_struct (start_struct (UNION_TYPE,
							 NULL_TREE),
					   nreverse ((yyvsp[-2].ttype)), chainon ((yyvsp[-4].ttype), (yyvsp[0].ttype)));
		  (yyval.tstype).kind = ctsk_tagdef;
		;}
    break;

  case 370:
#line 1614 "c-parse.y"
    { (yyval.ttype) = start_enum ((yyvsp[-1].ttype)); ;}
    break;

  case 371:
#line 1616 "c-parse.y"
    { (yyval.tstype).spec = finish_enum ((yyvsp[-4].ttype), nreverse ((yyvsp[-3].ttype)),
					 chainon ((yyvsp[-7].ttype), (yyvsp[0].ttype)));
		  (yyval.tstype).kind = ctsk_tagdef; ;}
    break;

  case 372:
#line 1620 "c-parse.y"
    { (yyval.ttype) = start_enum (NULL_TREE); ;}
    break;

  case 373:
#line 1622 "c-parse.y"
    { (yyval.tstype).spec = finish_enum ((yyvsp[-4].ttype), nreverse ((yyvsp[-3].ttype)),
					 chainon ((yyvsp[-6].ttype), (yyvsp[0].ttype)));
		  (yyval.tstype).kind = ctsk_tagdef; ;}
    break;

  case 374:
#line 1629 "c-parse.y"
    { (yyval.tstype) = parser_xref_tag (RECORD_TYPE, (yyvsp[0].ttype)); ;}
    break;

  case 375:
#line 1631 "c-parse.y"
    { (yyval.tstype) = parser_xref_tag (UNION_TYPE, (yyvsp[0].ttype)); ;}
    break;

  case 376:
#line 1633 "c-parse.y"
    { (yyval.tstype) = parser_xref_tag (ENUMERAL_TYPE, (yyvsp[0].ttype));
		  /* In ISO C, enumerated types can be referred to
		     only if already defined.  */
		  if (pedantic && !COMPLETE_TYPE_P ((yyval.tstype).spec))
		    pedwarn ("ISO C forbids forward references to %<enum%> types"); ;}
    break;

  case 380:
#line 1648 "c-parse.y"
    { if (pedantic && !flag_isoc99)
		    pedwarn ("comma at end of enumerator list"); ;}
    break;

  case 381:
#line 1666 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 382:
#line 1668 "c-parse.y"
    { (yyval.ttype) = chainon ((yyvsp[0].ttype), (yyvsp[-1].ttype));
		  pedwarn ("no semicolon at end of struct or union"); ;}
    break;

  case 383:
#line 1673 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 384:
#line 1675 "c-parse.y"
    { (yyval.ttype) = chainon ((yyvsp[-1].ttype), (yyvsp[-2].ttype)); ;}
    break;

  case 385:
#line 1677 "c-parse.y"
    { if (pedantic)
		    pedwarn ("extra semicolon in struct or union specified"); ;}
    break;

  case 386:
#line 1683 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].ttype);
		  POP_DECLSPEC_STACK; ;}
    break;

  case 387:
#line 1686 "c-parse.y"
    {
		  /* Support for unnamed structs or unions as members of
		     structs or unions (which is [a] useful and [b] supports
		     MS P-SDK).  */
		  (yyval.ttype) = grokfield (build_id_declarator (NULL_TREE),
				  current_declspecs, NULL_TREE);
		  POP_DECLSPEC_STACK; ;}
    break;

  case 388:
#line 1694 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].ttype);
		  POP_DECLSPEC_STACK; ;}
    break;

  case 389:
#line 1697 "c-parse.y"
    { if (pedantic)
		    pedwarn ("ISO C forbids member declarations with no members");
		  shadow_tag_warned (finish_declspecs ((yyvsp[0].dsptype)), pedantic);
		  (yyval.ttype) = NULL_TREE; ;}
    break;

  case 390:
#line 1702 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 391:
#line 1704 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].ttype);
		  RESTORE_EXT_FLAGS ((yyvsp[-1].itype)); ;}
    break;

  case 393:
#line 1711 "c-parse.y"
    { TREE_CHAIN ((yyvsp[0].ttype)) = (yyvsp[-3].ttype); (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 395:
#line 1717 "c-parse.y"
    { TREE_CHAIN ((yyvsp[0].ttype)) = (yyvsp[-3].ttype); (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 396:
#line 1722 "c-parse.y"
    { (yyval.ttype) = grokfield ((yyvsp[-1].dtrtype), current_declspecs, NULL_TREE);
		  decl_attributes (&(yyval.ttype),
				   chainon ((yyvsp[0].ttype), all_prefix_attributes), 0); ;}
    break;

  case 397:
#line 1726 "c-parse.y"
    { (yyval.ttype) = grokfield ((yyvsp[-3].dtrtype), current_declspecs, (yyvsp[-1].exprtype).value);
		  decl_attributes (&(yyval.ttype),
				   chainon ((yyvsp[0].ttype), all_prefix_attributes), 0); ;}
    break;

  case 398:
#line 1730 "c-parse.y"
    { (yyval.ttype) = grokfield (build_id_declarator (NULL_TREE),
				  current_declspecs, (yyvsp[-1].exprtype).value);
		  decl_attributes (&(yyval.ttype),
				   chainon ((yyvsp[0].ttype), all_prefix_attributes), 0); ;}
    break;

  case 399:
#line 1738 "c-parse.y"
    { (yyval.ttype) = grokfield ((yyvsp[-1].dtrtype), current_declspecs, NULL_TREE);
		  decl_attributes (&(yyval.ttype),
				   chainon ((yyvsp[0].ttype), all_prefix_attributes), 0); ;}
    break;

  case 400:
#line 1742 "c-parse.y"
    { (yyval.ttype) = grokfield ((yyvsp[-3].dtrtype), current_declspecs, (yyvsp[-1].exprtype).value);
		  decl_attributes (&(yyval.ttype),
				   chainon ((yyvsp[0].ttype), all_prefix_attributes), 0); ;}
    break;

  case 401:
#line 1746 "c-parse.y"
    { (yyval.ttype) = grokfield (build_id_declarator (NULL_TREE),
				  current_declspecs, (yyvsp[-1].exprtype).value);
		  decl_attributes (&(yyval.ttype),
				   chainon ((yyvsp[0].ttype), all_prefix_attributes), 0); ;}
    break;

  case 403:
#line 1758 "c-parse.y"
    { if ((yyvsp[-2].ttype) == error_mark_node)
		    (yyval.ttype) = (yyvsp[-2].ttype);
		  else
		    TREE_CHAIN ((yyvsp[0].ttype)) = (yyvsp[-2].ttype), (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 404:
#line 1763 "c-parse.y"
    { (yyval.ttype) = error_mark_node; ;}
    break;

  case 405:
#line 1769 "c-parse.y"
    { (yyval.ttype) = build_enumerator ((yyvsp[0].ttype), NULL_TREE); ;}
    break;

  case 406:
#line 1771 "c-parse.y"
    { (yyval.ttype) = build_enumerator ((yyvsp[-2].ttype), (yyvsp[0].exprtype).value); ;}
    break;

  case 407:
#line 1776 "c-parse.y"
    { pending_xref_error ();
		  (yyval.dsptype) = finish_declspecs ((yyvsp[0].dsptype)); ;}
    break;

  case 408:
#line 1779 "c-parse.y"
    { (yyval.typenametype) = XOBNEW (&parser_obstack, struct c_type_name);
		  (yyval.typenametype)->specs = (yyvsp[-1].dsptype);
		  (yyval.typenametype)->declarator = (yyvsp[0].dtrtype); ;}
    break;

  case 409:
#line 1786 "c-parse.y"
    { (yyval.dtrtype) = build_id_declarator (NULL_TREE); ;}
    break;

  case 411:
#line 1792 "c-parse.y"
    { (yyval.parmtype) = build_c_parm (current_declspecs, all_prefix_attributes,
				     build_id_declarator (NULL_TREE)); ;}
    break;

  case 412:
#line 1795 "c-parse.y"
    { (yyval.parmtype) = build_c_parm (current_declspecs, all_prefix_attributes,
				     (yyvsp[0].dtrtype)); ;}
    break;

  case 413:
#line 1798 "c-parse.y"
    { (yyval.parmtype) = build_c_parm (current_declspecs,
				     chainon ((yyvsp[0].ttype), all_prefix_attributes),
				     (yyvsp[-1].dtrtype)); ;}
    break;

  case 417:
#line 1811 "c-parse.y"
    { (yyval.dtrtype) = make_pointer_declarator ((yyvsp[-1].dsptype), (yyvsp[0].dtrtype)); ;}
    break;

  case 418:
#line 1816 "c-parse.y"
    { (yyval.dtrtype) = make_pointer_declarator
		    ((yyvsp[0].dsptype), build_id_declarator (NULL_TREE)); ;}
    break;

  case 419:
#line 1819 "c-parse.y"
    { (yyval.dtrtype) = make_pointer_declarator ((yyvsp[-1].dsptype), (yyvsp[0].dtrtype)); ;}
    break;

  case 420:
#line 1824 "c-parse.y"
    { (yyval.dtrtype) = (yyvsp[-2].ttype) ? build_attrs_declarator ((yyvsp[-2].ttype), (yyvsp[-1].dtrtype)) : (yyvsp[-1].dtrtype); ;}
    break;

  case 421:
#line 1826 "c-parse.y"
    { (yyval.dtrtype) = build_function_declarator ((yyvsp[0].arginfotype), (yyvsp[-2].dtrtype)); ;}
    break;

  case 422:
#line 1828 "c-parse.y"
    { (yyval.dtrtype) = set_array_declarator_inner ((yyvsp[0].dtrtype), (yyvsp[-1].dtrtype), true); ;}
    break;

  case 423:
#line 1830 "c-parse.y"
    { (yyval.dtrtype) = build_function_declarator
		    ((yyvsp[0].arginfotype), build_id_declarator (NULL_TREE)); ;}
    break;

  case 424:
#line 1833 "c-parse.y"
    { (yyval.dtrtype) = set_array_declarator_inner
		    ((yyvsp[0].dtrtype), build_id_declarator (NULL_TREE), true); ;}
    break;

  case 425:
#line 1841 "c-parse.y"
    { (yyval.dtrtype) = build_array_declarator ((yyvsp[-1].exprtype).value, (yyvsp[-2].dsptype), false, false); ;}
    break;

  case 426:
#line 1843 "c-parse.y"
    { (yyval.dtrtype) = build_array_declarator (NULL_TREE, (yyvsp[-1].dsptype), false, false); ;}
    break;

  case 427:
#line 1845 "c-parse.y"
    { (yyval.dtrtype) = build_array_declarator (NULL_TREE, (yyvsp[-2].dsptype), false, true); ;}
    break;

  case 428:
#line 1847 "c-parse.y"
    { (yyval.dtrtype) = build_array_declarator ((yyvsp[-1].exprtype).value, (yyvsp[-2].dsptype), true, false); ;}
    break;

  case 429:
#line 1850 "c-parse.y"
    { (yyval.dtrtype) = build_array_declarator ((yyvsp[-1].exprtype).value, (yyvsp[-3].dsptype), true, false); ;}
    break;

  case 432:
#line 1863 "c-parse.y"
    {
		  error ("label at end of compound statement");
		;}
    break;

  case 440:
#line 1880 "c-parse.y"
    {
		  if ((pedantic && !flag_isoc99)
		      || warn_declaration_after_statement)
		    pedwarn_c90 ("ISO C90 forbids mixed declarations and code");
		;}
    break;

  case 455:
#line 1914 "c-parse.y"
    { (yyval.ttype) = c_begin_compound_stmt (flag_isoc99); ;}
    break;

  case 457:
#line 1922 "c-parse.y"
    { if (pedantic)
		    pedwarn ("ISO C forbids label declarations"); ;}
    break;

  case 460:
#line 1933 "c-parse.y"
    { tree link;
		  for (link = (yyvsp[-1].ttype); link; link = TREE_CHAIN (link))
		    {
		      tree label = declare_label (TREE_VALUE (link));
		      C_DECLARED_LABEL_FLAG (label) = 1;
		      add_stmt (build_stmt (DECL_EXPR, label));
		    }
		;}
    break;

  case 461:
#line 1947 "c-parse.y"
    { add_stmt ((yyvsp[0].ttype)); ;}
    break;

  case 463:
#line 1951 "c-parse.y"
    { (yyval.ttype) = c_begin_compound_stmt (true); ;}
    break;

  case 468:
#line 1965 "c-parse.y"
    { if (cur_stmt_list == NULL)
		    {
		      error ("braced-group within expression allowed "
			     "only inside a function");
		      YYERROR;
		    }
		  (yyval.ttype) = c_begin_stmt_expr ();
		;}
    break;

  case 469:
#line 1976 "c-parse.y"
    { (yyval.ttype) = c_end_compound_stmt ((yyvsp[-1].ttype), true); ;}
    break;

  case 470:
#line 1984 "c-parse.y"
    { if (yychar == YYEMPTY)
		    yychar = YYLEX;
		  (yyval.location) = input_location; ;}
    break;

  case 473:
#line 1997 "c-parse.y"
    { (yyval.ttype) = c_end_compound_stmt ((yyvsp[-2].ttype), flag_isoc99); ;}
    break;

  case 474:
#line 2002 "c-parse.y"
    {
		  /* Two cases cannot and do not have line numbers associated:
		     If stmt is degenerate, such as "2;", then stmt is an
		     INTEGER_CST, which cannot hold line numbers.  But that's
		     ok because the statement will either be changed to a
		     MODIFY_EXPR during gimplification of the statement expr,
		     or discarded.  If stmt was compound, but without new
		     variables, we will have skipped the creation of a BIND
		     and will have a bare STATEMENT_LIST.  But that's ok
		     because (recursively) all of the component statments
		     should already have line numbers assigned.  */
		  if ((yyvsp[0].ttype) && EXPR_P ((yyvsp[0].ttype)))
		    SET_EXPR_LOCATION ((yyvsp[0].ttype), (yyvsp[-1].location));
		;}
    break;

  case 475:
#line 2020 "c-parse.y"
    { if ((yyvsp[0].ttype)) SET_EXPR_LOCATION ((yyvsp[0].ttype), (yyvsp[-1].location)); ;}
    break;

  case 476:
#line 2024 "c-parse.y"
    { (yyval.ttype) = lang_hooks.truthvalue_conversion ((yyvsp[0].exprtype).value);
		  if (EXPR_P ((yyval.ttype)))
		    SET_EXPR_LOCATION ((yyval.ttype), (yyvsp[-1].location)); ;}
    break;

  case 477:
#line 2037 "c-parse.y"
    { (yyval.ttype) = c_end_compound_stmt ((yyvsp[-2].ttype), flag_isoc99); ;}
    break;

  case 478:
#line 2042 "c-parse.y"
    { if (extra_warnings)
		    add_stmt (build (NOP_EXPR, NULL_TREE, NULL_TREE));
		  (yyval.ttype) = c_end_compound_stmt ((yyvsp[-2].ttype), flag_isoc99); ;}
    break;

  case 480:
#line 2051 "c-parse.y"
    { c_finish_if_stmt ((yyvsp[-6].location), (yyvsp[-4].ttype), (yyvsp[-2].ttype), (yyvsp[0].ttype), true);
		  add_stmt (c_end_compound_stmt ((yyvsp[-7].ttype), flag_isoc99)); ;}
    break;

  case 481:
#line 2055 "c-parse.y"
    { c_finish_if_stmt ((yyvsp[-6].location), (yyvsp[-4].ttype), (yyvsp[-2].ttype), (yyvsp[0].ttype), false);
		  add_stmt (c_end_compound_stmt ((yyvsp[-7].ttype), flag_isoc99)); ;}
    break;

  case 482:
#line 2059 "c-parse.y"
    { c_finish_if_stmt ((yyvsp[-4].location), (yyvsp[-2].ttype), (yyvsp[0].ttype), NULL, true);
		  add_stmt (c_end_compound_stmt ((yyvsp[-5].ttype), flag_isoc99)); ;}
    break;

  case 483:
#line 2063 "c-parse.y"
    { c_finish_if_stmt ((yyvsp[-4].location), (yyvsp[-2].ttype), (yyvsp[0].ttype), NULL, false);
		  add_stmt (c_end_compound_stmt ((yyvsp[-5].ttype), flag_isoc99)); ;}
    break;

  case 484:
#line 2068 "c-parse.y"
    { (yyval.ttype) = c_break_label; c_break_label = NULL; ;}
    break;

  case 485:
#line 2072 "c-parse.y"
    { (yyval.ttype) = c_cont_label; c_cont_label = NULL; ;}
    break;

  case 486:
#line 2078 "c-parse.y"
    { c_finish_loop ((yyvsp[-6].location), (yyvsp[-4].ttype), NULL, (yyvsp[0].ttype), c_break_label,
				 c_cont_label, true);
		  add_stmt (c_end_compound_stmt ((yyvsp[-7].ttype), flag_isoc99));
		  c_break_label = (yyvsp[-2].ttype); c_cont_label = (yyvsp[-1].ttype); ;}
    break;

  case 487:
#line 2087 "c-parse.y"
    { (yyval.ttype) = c_break_label; c_break_label = (yyvsp[-3].ttype); ;}
    break;

  case 488:
#line 2088 "c-parse.y"
    { (yyval.ttype) = c_cont_label; c_cont_label = (yyvsp[-3].ttype); ;}
    break;

  case 489:
#line 2090 "c-parse.y"
    { c_finish_loop ((yyvsp[-10].location), (yyvsp[-2].ttype), NULL, (yyvsp[-7].ttype), (yyvsp[-5].ttype),
				 (yyvsp[-4].ttype), false);
		  add_stmt (c_end_compound_stmt ((yyvsp[-11].ttype), flag_isoc99)); ;}
    break;

  case 490:
#line 2097 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 491:
#line 2099 "c-parse.y"
    { (yyval.ttype) = (yyvsp[0].exprtype).value; ;}
    break;

  case 492:
#line 2104 "c-parse.y"
    { c_finish_expr_stmt ((yyvsp[-1].ttype)); ;}
    break;

  case 493:
#line 2106 "c-parse.y"
    { check_for_loop_decls (); ;}
    break;

  case 494:
#line 2110 "c-parse.y"
    { if ((yyvsp[0].ttype))
		    {
		      (yyval.ttype) = lang_hooks.truthvalue_conversion ((yyvsp[0].ttype));
		      if (EXPR_P ((yyval.ttype)))
			SET_EXPR_LOCATION ((yyval.ttype), (yyvsp[-1].location));
		    }
		  else
		    (yyval.ttype) = NULL;
		;}
    break;

  case 495:
#line 2122 "c-parse.y"
    { (yyval.ttype) = c_process_expr_stmt ((yyvsp[0].ttype)); ;}
    break;

  case 496:
#line 2129 "c-parse.y"
    { c_finish_loop ((yyvsp[-7].location), (yyvsp[-6].ttype), (yyvsp[-4].ttype), (yyvsp[0].ttype), c_break_label,
				 c_cont_label, true);
		  add_stmt (c_end_compound_stmt ((yyvsp[-10].ttype), flag_isoc99));
		  c_break_label = (yyvsp[-2].ttype); c_cont_label = (yyvsp[-1].ttype); ;}
    break;

  case 497:
#line 2137 "c-parse.y"
    { (yyval.ttype) = c_start_case ((yyvsp[-1].exprtype).value); ;}
    break;

  case 498:
#line 2139 "c-parse.y"
    { c_finish_case ((yyvsp[0].ttype));
		  if (c_break_label)
		    add_stmt (build (LABEL_EXPR, void_type_node,
				     c_break_label));
		  c_break_label = (yyvsp[-1].ttype);
		  add_stmt (c_end_compound_stmt ((yyvsp[-6].ttype), flag_isoc99)); ;}
    break;

  case 499:
#line 2150 "c-parse.y"
    { (yyval.ttype) = c_finish_expr_stmt ((yyvsp[-1].exprtype).value); ;}
    break;

  case 500:
#line 2152 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 501:
#line 2154 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 502:
#line 2156 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 503:
#line 2158 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 504:
#line 2160 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 505:
#line 2162 "c-parse.y"
    { (yyval.ttype) = c_finish_bc_stmt (&c_break_label, true); ;}
    break;

  case 506:
#line 2164 "c-parse.y"
    { (yyval.ttype) = c_finish_bc_stmt (&c_cont_label, false); ;}
    break;

  case 507:
#line 2166 "c-parse.y"
    { (yyval.ttype) = c_finish_return (NULL_TREE); ;}
    break;

  case 508:
#line 2168 "c-parse.y"
    { (yyval.ttype) = c_finish_return ((yyvsp[-1].exprtype).value); ;}
    break;

  case 510:
#line 2171 "c-parse.y"
    { (yyval.ttype) = c_finish_goto_label ((yyvsp[-1].ttype)); ;}
    break;

  case 511:
#line 2173 "c-parse.y"
    { (yyval.ttype) = c_finish_goto_ptr ((yyvsp[-1].exprtype).value); ;}
    break;

  case 512:
#line 2175 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 513:
#line 2181 "c-parse.y"
    { add_stmt ((yyvsp[0].ttype)); (yyval.ttype) = NULL_TREE; ;}
    break;

  case 515:
#line 2190 "c-parse.y"
    { (yyval.ttype) = do_case ((yyvsp[-1].exprtype).value, NULL_TREE); ;}
    break;

  case 516:
#line 2192 "c-parse.y"
    { (yyval.ttype) = do_case ((yyvsp[-3].exprtype).value, (yyvsp[-1].exprtype).value); ;}
    break;

  case 517:
#line 2194 "c-parse.y"
    { (yyval.ttype) = do_case (NULL_TREE, NULL_TREE); ;}
    break;

  case 518:
#line 2196 "c-parse.y"
    { tree label = define_label ((yyvsp[-2].location), (yyvsp[-3].ttype));
		  if (label)
		    {
		      decl_attributes (&label, (yyvsp[0].ttype), 0);
		      (yyval.ttype) = add_stmt (build_stmt (LABEL_EXPR, label));
		    }
		  else
		    (yyval.ttype) = NULL_TREE;
		;}
    break;

  case 519:
#line 2214 "c-parse.y"
    { (yyval.ttype) = (yyvsp[-2].ttype); ;}
    break;

  case 520:
#line 2220 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 522:
#line 2227 "c-parse.y"
    { assemble_asm ((yyvsp[-1].ttype)); ;}
    break;

  case 523:
#line 2229 "c-parse.y"
    {;}
    break;

  case 524:
#line 2237 "c-parse.y"
    { (yyval.ttype) = build_asm_stmt ((yyvsp[-6].ttype), (yyvsp[-3].ttype)); ;}
    break;

  case 525:
#line 2243 "c-parse.y"
    { (yyval.ttype) = build_asm_expr ((yyvsp[0].ttype), 0, 0, 0, true); ;}
    break;

  case 526:
#line 2246 "c-parse.y"
    { (yyval.ttype) = build_asm_expr ((yyvsp[-2].ttype), (yyvsp[0].ttype), 0, 0, false); ;}
    break;

  case 527:
#line 2249 "c-parse.y"
    { (yyval.ttype) = build_asm_expr ((yyvsp[-4].ttype), (yyvsp[-2].ttype), (yyvsp[0].ttype), 0, false); ;}
    break;

  case 528:
#line 2252 "c-parse.y"
    { (yyval.ttype) = build_asm_expr ((yyvsp[-6].ttype), (yyvsp[-4].ttype), (yyvsp[-2].ttype), (yyvsp[0].ttype), false); ;}
    break;

  case 529:
#line 2259 "c-parse.y"
    { (yyval.ttype) = 0; ;}
    break;

  case 530:
#line 2261 "c-parse.y"
    { if ((yyvsp[0].ttype) != ridpointers[RID_VOLATILE])
		    {
		      warning ("%E qualifier ignored on asm", (yyvsp[0].ttype));
		      (yyval.ttype) = 0;
		    }
		  else
		    (yyval.ttype) = (yyvsp[0].ttype);
		;}
    break;

  case 531:
#line 2274 "c-parse.y"
    { (yyval.ttype) = NULL_TREE; ;}
    break;

  case 534:
#line 2281 "c-parse.y"
    { (yyval.ttype) = chainon ((yyvsp[-2].ttype), (yyvsp[0].ttype)); ;}
    break;

  case 535:
#line 2287 "c-parse.y"
    { (yyval.ttype) = build_tree_list (build_tree_list (NULL_TREE, (yyvsp[-5].ttype)),
					(yyvsp[-2].exprtype).value); ;}
    break;

  case 536:
#line 2291 "c-parse.y"
    { (yyvsp[-7].ttype) = build_string (IDENTIFIER_LENGTH ((yyvsp[-7].ttype)),
				     IDENTIFIER_POINTER ((yyvsp[-7].ttype)));
		  (yyval.ttype) = build_tree_list (build_tree_list ((yyvsp[-7].ttype), (yyvsp[-5].ttype)), (yyvsp[-2].exprtype).value); ;}
    break;

  case 537:
#line 2298 "c-parse.y"
    { (yyval.ttype) = tree_cons (NULL_TREE, (yyvsp[0].ttype), NULL_TREE); ;}
    break;

  case 538:
#line 2300 "c-parse.y"
    { (yyval.ttype) = tree_cons (NULL_TREE, (yyvsp[0].ttype), (yyvsp[-2].ttype)); ;}
    break;

  case 539:
#line 2306 "c-parse.y"
    { if (TYPE_MAIN_VARIANT (TREE_TYPE (TREE_TYPE ((yyvsp[0].ttype))))
		      != char_type_node)
		    {
		      error ("wide string literal in %<asm%>");
		      (yyval.ttype) = build_string (1, "");
		    }
		  else
		    (yyval.ttype) = (yyvsp[0].ttype); ;}
    break;

  case 540:
#line 2317 "c-parse.y"
    { c_lex_string_translate = 0; ;}
    break;

  case 541:
#line 2321 "c-parse.y"
    { c_lex_string_translate = 1; ;}
    break;

  case 542:
#line 2332 "c-parse.y"
    { push_scope ();
		  declare_parm_level (); ;}
    break;

  case 543:
#line 2335 "c-parse.y"
    { (yyval.arginfotype) = (yyvsp[0].arginfotype);
		  pop_scope (); ;}
    break;

  case 545:
#line 2342 "c-parse.y"
    { mark_forward_parm_decls (); ;}
    break;

  case 546:
#line 2344 "c-parse.y"
    { /* Dummy action so attributes are in known place
		     on parser stack.  */ ;}
    break;

  case 547:
#line 2347 "c-parse.y"
    { (yyval.arginfotype) = (yyvsp[0].arginfotype); ;}
    break;

  case 548:
#line 2349 "c-parse.y"
    { (yyval.arginfotype) = XOBNEW (&parser_obstack, struct c_arg_info);
		  (yyval.arginfotype)->parms = 0;
		  (yyval.arginfotype)->tags = 0;
		  (yyval.arginfotype)->types = 0;
		  (yyval.arginfotype)->others = 0; ;}
    break;

  case 549:
#line 2359 "c-parse.y"
    { (yyval.arginfotype) = XOBNEW (&parser_obstack, struct c_arg_info);
		  (yyval.arginfotype)->parms = 0;
		  (yyval.arginfotype)->tags = 0;
		  (yyval.arginfotype)->types = 0;
		  (yyval.arginfotype)->others = 0; ;}
    break;

  case 550:
#line 2365 "c-parse.y"
    { (yyval.arginfotype) = XOBNEW (&parser_obstack, struct c_arg_info);
		  (yyval.arginfotype)->parms = 0;
		  (yyval.arginfotype)->tags = 0;
		  (yyval.arginfotype)->others = 0;
		  /* Suppress -Wold-style-definition for this case.  */
		  (yyval.arginfotype)->types = error_mark_node;
		  error ("ISO C requires a named argument before %<...%>");
		;}
    break;

  case 551:
#line 2374 "c-parse.y"
    { (yyval.arginfotype) = get_parm_info (/*ellipsis=*/false); ;}
    break;

  case 552:
#line 2376 "c-parse.y"
    { (yyval.arginfotype) = get_parm_info (/*ellipsis=*/true); ;}
    break;

  case 553:
#line 2381 "c-parse.y"
    { push_parm_decl ((yyvsp[0].parmtype)); ;}
    break;

  case 554:
#line 2383 "c-parse.y"
    { push_parm_decl ((yyvsp[0].parmtype)); ;}
    break;

  case 555:
#line 2390 "c-parse.y"
    { (yyval.parmtype) = build_c_parm (current_declspecs,
				     chainon ((yyvsp[0].ttype), all_prefix_attributes), (yyvsp[-1].dtrtype));
		  POP_DECLSPEC_STACK; ;}
    break;

  case 556:
#line 2394 "c-parse.y"
    { (yyval.parmtype) = build_c_parm (current_declspecs,
				     chainon ((yyvsp[0].ttype), all_prefix_attributes), (yyvsp[-1].dtrtype));
		  POP_DECLSPEC_STACK; ;}
    break;

  case 557:
#line 2398 "c-parse.y"
    { (yyval.parmtype) = (yyvsp[0].parmtype);
		  POP_DECLSPEC_STACK; ;}
    break;

  case 558:
#line 2401 "c-parse.y"
    { (yyval.parmtype) = build_c_parm (current_declspecs,
				     chainon ((yyvsp[0].ttype), all_prefix_attributes), (yyvsp[-1].dtrtype));
		  POP_DECLSPEC_STACK; ;}
    break;

  case 559:
#line 2406 "c-parse.y"
    { (yyval.parmtype) = (yyvsp[0].parmtype);
		  POP_DECLSPEC_STACK; ;}
    break;

  case 560:
#line 2414 "c-parse.y"
    { (yyval.parmtype) = build_c_parm (current_declspecs,
				     chainon ((yyvsp[0].ttype), all_prefix_attributes), (yyvsp[-1].dtrtype));
		  POP_DECLSPEC_STACK; ;}
    break;

  case 561:
#line 2418 "c-parse.y"
    { (yyval.parmtype) = build_c_parm (current_declspecs,
				     chainon ((yyvsp[0].ttype), all_prefix_attributes), (yyvsp[-1].dtrtype));
		  POP_DECLSPEC_STACK; ;}
    break;

  case 562:
#line 2422 "c-parse.y"
    { (yyval.parmtype) = (yyvsp[0].parmtype);
		  POP_DECLSPEC_STACK; ;}
    break;

  case 563:
#line 2425 "c-parse.y"
    { (yyval.parmtype) = build_c_parm (current_declspecs,
				     chainon ((yyvsp[0].ttype), all_prefix_attributes), (yyvsp[-1].dtrtype));
		  POP_DECLSPEC_STACK; ;}
    break;

  case 564:
#line 2430 "c-parse.y"
    { (yyval.parmtype) = (yyvsp[0].parmtype);
		  POP_DECLSPEC_STACK; ;}
    break;

  case 565:
#line 2436 "c-parse.y"
    { prefix_attributes = chainon (prefix_attributes, (yyvsp[-3].ttype));
		  all_prefix_attributes = prefix_attributes; ;}
    break;

  case 566:
#line 2445 "c-parse.y"
    { push_scope ();
		  declare_parm_level (); ;}
    break;

  case 567:
#line 2448 "c-parse.y"
    { (yyval.arginfotype) = (yyvsp[0].arginfotype);
		  pop_scope (); ;}
    break;

  case 569:
#line 2455 "c-parse.y"
    { (yyval.arginfotype) = XOBNEW (&parser_obstack, struct c_arg_info);
		  (yyval.arginfotype)->parms = 0;
		  (yyval.arginfotype)->tags = 0;
		  (yyval.arginfotype)->types = (yyvsp[-1].ttype);
		  (yyval.arginfotype)->others = 0;

		  /* Make sure we have a parmlist after attributes.  */
		  if ((yyvsp[-3].ttype) != 0)
		    YYERROR1;
		;}
    break;

  case 570:
#line 2470 "c-parse.y"
    { (yyval.ttype) = build_tree_list (NULL_TREE, (yyvsp[0].ttype)); ;}
    break;

  case 571:
#line 2472 "c-parse.y"
    { (yyval.ttype) = chainon ((yyvsp[-2].ttype), build_tree_list (NULL_TREE, (yyvsp[0].ttype))); ;}
    break;

  case 572:
#line 2478 "c-parse.y"
    { (yyval.ttype) = build_tree_list (NULL_TREE, (yyvsp[0].ttype)); ;}
    break;

  case 573:
#line 2480 "c-parse.y"
    { (yyval.ttype) = chainon ((yyvsp[-2].ttype), build_tree_list (NULL_TREE, (yyvsp[0].ttype))); ;}
    break;

  case 574:
#line 2485 "c-parse.y"
    { (yyval.itype) = SAVE_EXT_FLAGS ();
		  pedantic = 0;
		  warn_pointer_arith = 0;
		  warn_traditional = 0;
		  flag_iso = 0; ;}
    break;


    }

/* Line 1037 of yacc.c.  */
#line 5293 "c-parse.c"

  yyvsp -= yylen;
  yyssp -= yylen;


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
#if YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (YYPACT_NINF < yyn && yyn < YYLAST)
	{
	  YYSIZE_T yysize = 0;
	  int yytype = YYTRANSLATE (yychar);
	  const char* yyprefix;
	  char *yymsg;
	  int yyx;

	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  int yyxbegin = yyn < 0 ? -yyn : 0;

	  /* Stay within bounds of both yycheck and yytname.  */
	  int yychecklim = YYLAST - yyn;
	  int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
	  int yycount = 0;

	  yyprefix = ", expecting ";
	  for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	      {
		yysize += yystrlen (yyprefix) + yystrlen (yytname [yyx]);
		yycount += 1;
		if (yycount == 5)
		  {
		    yysize = 0;
		    break;
		  }
	      }
	  yysize += (sizeof ("syntax error, unexpected ")
		     + yystrlen (yytname[yytype]));
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "syntax error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[yytype]);

	      if (yycount < 5)
		{
		  yyprefix = ", expecting ";
		  for (yyx = yyxbegin; yyx < yyxend; ++yyx)
		    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
		      {
			yyp = yystpcpy (yyp, yyprefix);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yyprefix = " or ";
		      }
		}
	      yyerror (yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    yyerror ("syntax error; also virtual memory exhausted");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror ("syntax error");
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* If at end of input, pop the error token,
	     then the rest of the stack, then return failure.  */
	  if (yychar == YYEOF)
	     for (;;)
	       {

		 YYPOPSTACK;
		 if (yyssp == yyss)
		   YYABORT;
		 yydestruct ("Error: popping",
                             yystos[*yyssp], yyvsp);
	       }
        }
      else
	{
	  yydestruct ("Error: discarding", yytoken, &yylval);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse look-ahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

#ifdef __GNUC__
  /* Pacify GCC when the user code never invokes YYERROR and the label
     yyerrorlab therefore never appears in user code.  */
  if (0)
     goto yyerrorlab;
#endif

yyvsp -= yylen;
  yyssp -= yylen;
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


      yydestruct ("Error: popping", yystos[yystate], yyvsp);
      YYPOPSTACK;
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;


  /* Shift the error token. */
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
  yydestruct ("Error: discarding lookahead",
              yytoken, &yylval);
  yychar = YYEMPTY;
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*----------------------------------------------.
| yyoverflowlab -- parser overflow comes here.  |
`----------------------------------------------*/
yyoverflowlab:
  yyerror ("parser stack overflow");
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}


#line 2492 "c-parse.y"


/* yylex() is a thin wrapper around c_lex(), all it does is translate
   cpplib.h's token codes into yacc's token codes.  */

static enum cpp_ttype last_token;

/* The reserved keyword table.  */
struct resword
{
  const char *word;
  ENUM_BITFIELD(rid) rid : 16;
  unsigned int disable   : 16;
};

/* Disable mask.  Keywords are disabled if (reswords[i].disable & mask) is
   _true_.  */
#define D_C89	0x01	/* not in C89 */
#define D_EXT	0x02	/* GCC extension */
#define D_EXT89	0x04	/* GCC extension incorporated in C99 */
#define D_OBJC	0x08	/* Objective C only */

static const struct resword reswords[] =
{
  { "_Bool",		RID_BOOL,	0 },
  { "_Complex",		RID_COMPLEX,	0 },
  { "__FUNCTION__",	RID_FUNCTION_NAME, 0 },
  { "__PRETTY_FUNCTION__", RID_PRETTY_FUNCTION_NAME, 0 },
  { "__alignof",	RID_ALIGNOF,	0 },
  { "__alignof__",	RID_ALIGNOF,	0 },
  { "__asm",		RID_ASM,	0 },
  { "__asm__",		RID_ASM,	0 },
  { "__attribute",	RID_ATTRIBUTE,	0 },
  { "__attribute__",	RID_ATTRIBUTE,	0 },
  { "__builtin_choose_expr", RID_CHOOSE_EXPR, 0 },
  { "__builtin_offsetof", RID_OFFSETOF, 0 },
  { "__builtin_types_compatible_p", RID_TYPES_COMPATIBLE_P, 0 },
  { "__builtin_va_arg",	RID_VA_ARG,	0 },
  { "__complex",	RID_COMPLEX,	0 },
  { "__complex__",	RID_COMPLEX,	0 },
  { "__const",		RID_CONST,	0 },
  { "__const__",	RID_CONST,	0 },
  { "__extension__",	RID_EXTENSION,	0 },
  { "__func__",		RID_C99_FUNCTION_NAME, 0 },
  { "__imag",		RID_IMAGPART,	0 },
  { "__imag__",		RID_IMAGPART,	0 },
  { "__inline",		RID_INLINE,	0 },
  { "__inline__",	RID_INLINE,	0 },
  { "__label__",	RID_LABEL,	0 },
  { "__real",		RID_REALPART,	0 },
  { "__real__",		RID_REALPART,	0 },
  { "__restrict",	RID_RESTRICT,	0 },
  { "__restrict__",	RID_RESTRICT,	0 },
  { "__signed",		RID_SIGNED,	0 },
  { "__signed__",	RID_SIGNED,	0 },
  { "__thread",		RID_THREAD,	0 },
  { "__typeof",		RID_TYPEOF,	0 },
  { "__typeof__",	RID_TYPEOF,	0 },
  { "__volatile",	RID_VOLATILE,	0 },
  { "__volatile__",	RID_VOLATILE,	0 },
  { "asm",		RID_ASM,	D_EXT },
  { "auto",		RID_AUTO,	0 },
  { "break",		RID_BREAK,	0 },
  { "case",		RID_CASE,	0 },
  { "char",		RID_CHAR,	0 },
  { "const",		RID_CONST,	0 },
  { "continue",		RID_CONTINUE,	0 },
  { "default",		RID_DEFAULT,	0 },
  { "do",		RID_DO,		0 },
  { "double",		RID_DOUBLE,	0 },
  { "else",		RID_ELSE,	0 },
  { "enum",		RID_ENUM,	0 },
  { "extern",		RID_EXTERN,	0 },
  { "float",		RID_FLOAT,	0 },
  { "for",		RID_FOR,	0 },
  { "goto",		RID_GOTO,	0 },
  { "if",		RID_IF,		0 },
  { "inline",		RID_INLINE,	D_EXT89 },
  { "int",		RID_INT,	0 },
  { "long",		RID_LONG,	0 },
  { "register",		RID_REGISTER,	0 },
  { "restrict",		RID_RESTRICT,	D_C89 },
  { "return",		RID_RETURN,	0 },
  { "short",		RID_SHORT,	0 },
  { "signed",		RID_SIGNED,	0 },
  { "sizeof",		RID_SIZEOF,	0 },
  { "static",		RID_STATIC,	0 },
  { "struct",		RID_STRUCT,	0 },
  { "switch",		RID_SWITCH,	0 },
  { "typedef",		RID_TYPEDEF,	0 },
  { "typeof",		RID_TYPEOF,	D_EXT },
  { "union",		RID_UNION,	0 },
  { "unsigned",		RID_UNSIGNED,	0 },
  { "void",		RID_VOID,	0 },
  { "volatile",		RID_VOLATILE,	0 },
  { "while",		RID_WHILE,	0 },

};
#define N_reswords (sizeof reswords / sizeof (struct resword))

/* Table mapping from RID_* constants to yacc token numbers.
   Unfortunately we have to have entries for all the keywords in all
   three languages.  */
static const short rid_to_yy[RID_MAX] =
{
  /* RID_STATIC */	STATIC,
  /* RID_UNSIGNED */	TYPESPEC,
  /* RID_LONG */	TYPESPEC,
  /* RID_CONST */	TYPE_QUAL,
  /* RID_EXTERN */	SCSPEC,
  /* RID_REGISTER */	SCSPEC,
  /* RID_TYPEDEF */	SCSPEC,
  /* RID_SHORT */	TYPESPEC,
  /* RID_INLINE */	SCSPEC,
  /* RID_VOLATILE */	TYPE_QUAL,
  /* RID_SIGNED */	TYPESPEC,
  /* RID_AUTO */	SCSPEC,
  /* RID_RESTRICT */	TYPE_QUAL,

  /* C extensions */
  /* RID_COMPLEX */	TYPESPEC,
  /* RID_THREAD */	SCSPEC,

  /* C++ */
  /* RID_FRIEND */	0,
  /* RID_VIRTUAL */	0,
  /* RID_EXPLICIT */	0,
  /* RID_EXPORT */	0,
  /* RID_MUTABLE */	0,

  /* ObjC */
  /* RID_IN */		OBJC_TYPE_QUAL,
  /* RID_OUT */		OBJC_TYPE_QUAL,
  /* RID_INOUT */	OBJC_TYPE_QUAL,
  /* RID_BYCOPY */	OBJC_TYPE_QUAL,
  /* RID_BYREF */	OBJC_TYPE_QUAL,
  /* RID_ONEWAY */	OBJC_TYPE_QUAL,

  /* C */
  /* RID_INT */		TYPESPEC,
  /* RID_CHAR */	TYPESPEC,
  /* RID_FLOAT */	TYPESPEC,
  /* RID_DOUBLE */	TYPESPEC,
  /* RID_VOID */	TYPESPEC,
  /* RID_ENUM */	ENUM,
  /* RID_STRUCT */	STRUCT,
  /* RID_UNION */	UNION,
  /* RID_IF */		IF,
  /* RID_ELSE */	ELSE,
  /* RID_WHILE */	WHILE,
  /* RID_DO */		DO,
  /* RID_FOR */		FOR,
  /* RID_SWITCH */	SWITCH,
  /* RID_CASE */	CASE,
  /* RID_DEFAULT */	DEFAULT,
  /* RID_BREAK */	BREAK,
  /* RID_CONTINUE */	CONTINUE,
  /* RID_RETURN */	RETURN,
  /* RID_GOTO */	GOTO,
  /* RID_SIZEOF */	SIZEOF,

  /* C extensions */
  /* RID_ASM */		ASM_KEYWORD,
  /* RID_TYPEOF */	TYPEOF,
  /* RID_ALIGNOF */	ALIGNOF,
  /* RID_ATTRIBUTE */	ATTRIBUTE,
  /* RID_VA_ARG */	VA_ARG,
  /* RID_EXTENSION */	EXTENSION,
  /* RID_IMAGPART */	IMAGPART,
  /* RID_REALPART */	REALPART,
  /* RID_LABEL */	LABEL,

  /* RID_CHOOSE_EXPR */			CHOOSE_EXPR,
  /* RID_TYPES_COMPATIBLE_P */		TYPES_COMPATIBLE_P,

  /* RID_FUNCTION_NAME */		FUNC_NAME,
  /* RID_PRETTY_FUNCTION_NAME */	FUNC_NAME,
  /* RID_C99_FUNCTION_NAME */		FUNC_NAME,

  /* C++ */
  /* RID_BOOL */	TYPESPEC,
  /* RID_WCHAR */	0,
  /* RID_CLASS */	0,
  /* RID_PUBLIC */	0,
  /* RID_PRIVATE */	0,
  /* RID_PROTECTED */	0,
  /* RID_TEMPLATE */	0,
  /* RID_NULL */	0,
  /* RID_CATCH */	0,
  /* RID_DELETE */	0,
  /* RID_FALSE */	0,
  /* RID_NAMESPACE */	0,
  /* RID_NEW */		0,
  /* RID_OFFSETOF */    OFFSETOF,
  /* RID_OPERATOR */	0,
  /* RID_THIS */	0,
  /* RID_THROW */	0,
  /* RID_TRUE */	0,
  /* RID_TRY */		0,
  /* RID_TYPENAME */	0,
  /* RID_TYPEID */	0,
  /* RID_USING */	0,

  /* casts */
  /* RID_CONSTCAST */	0,
  /* RID_DYNCAST */	0,
  /* RID_REINTCAST */	0,
  /* RID_STATCAST */	0,

  /* Objective C */
  /* RID_AT_ENCODE */		AT_ENCODE,
  /* RID_AT_END */		AT_END,
  /* RID_AT_CLASS */		AT_CLASS,
  /* RID_AT_ALIAS */		AT_ALIAS,
  /* RID_AT_DEFS */		AT_DEFS,
  /* RID_AT_PRIVATE */		AT_PRIVATE,
  /* RID_AT_PROTECTED */	AT_PROTECTED,
  /* RID_AT_PUBLIC */		AT_PUBLIC,
  /* RID_AT_PROTOCOL */		AT_PROTOCOL,
  /* RID_AT_SELECTOR */		AT_SELECTOR,
  /* RID_AT_THROW */		AT_THROW,
  /* RID_AT_TRY */		AT_TRY,
  /* RID_AT_CATCH */		AT_CATCH,
  /* RID_AT_FINALLY */		AT_FINALLY,
  /* RID_AT_SYNCHRONIZED */	AT_SYNCHRONIZED,
  /* RID_AT_INTERFACE */	AT_INTERFACE,
  /* RID_AT_IMPLEMENTATION */	AT_IMPLEMENTATION
};

static void
init_reswords (void)
{
  unsigned int i;
  tree id;
  int mask = (flag_isoc99 ? 0 : D_C89)
	      | (flag_no_asm ? (flag_isoc99 ? D_EXT : D_EXT|D_EXT89) : 0);

  if (!c_dialect_objc ())
     mask |= D_OBJC;

  ridpointers = GGC_CNEWVEC (tree, (int) RID_MAX);
  for (i = 0; i < N_reswords; i++)
    {
      /* If a keyword is disabled, do not enter it into the table
	 and so create a canonical spelling that isn't a keyword.  */
      if (reswords[i].disable & mask)
	continue;

      id = get_identifier (reswords[i].word);
      C_RID_CODE (id) = reswords[i].rid;
      C_IS_RESERVED_WORD (id) = 1;
      ridpointers [(int) reswords[i].rid] = id;
    }
}

#define NAME(type) cpp_type2name (type)

static void
yyerror (const char *msgid)
{
  c_parse_error (msgid, last_token, yylval.ttype);
}

static int
yylexname (void)
{
  tree decl;


  if (C_IS_RESERVED_WORD (yylval.ttype))
    {
      enum rid rid_code = C_RID_CODE (yylval.ttype);

      {
	/* Return the canonical spelling for this keyword.  */
	yylval.ttype = ridpointers[(int) rid_code];
	return rid_to_yy[(int) rid_code];
      }
    }

  decl = lookup_name (yylval.ttype);
  if (decl)
    {
      if (TREE_CODE (decl) == TYPE_DECL)
	return TYPENAME;
    }

  return IDENTIFIER;
}

static inline int
_yylex (void)
{
 get_next:
  last_token = c_lex (&yylval.ttype);
  switch (last_token)
    {
    case CPP_EQ:					return '=';
    case CPP_NOT:					return '!';
    case CPP_GREATER:	yylval.code = GT_EXPR;		return ARITHCOMPARE;
    case CPP_LESS:	yylval.code = LT_EXPR;		return ARITHCOMPARE;
    case CPP_PLUS:	yylval.code = PLUS_EXPR;	return '+';
    case CPP_MINUS:	yylval.code = MINUS_EXPR;	return '-';
    case CPP_MULT:	yylval.code = MULT_EXPR;	return '*';
    case CPP_DIV:	yylval.code = TRUNC_DIV_EXPR;	return '/';
    case CPP_MOD:	yylval.code = TRUNC_MOD_EXPR;	return '%';
    case CPP_AND:	yylval.code = BIT_AND_EXPR;	return '&';
    case CPP_OR:	yylval.code = BIT_IOR_EXPR;	return '|';
    case CPP_XOR:	yylval.code = BIT_XOR_EXPR;	return '^';
    case CPP_RSHIFT:	yylval.code = RSHIFT_EXPR;	return RSHIFT;
    case CPP_LSHIFT:	yylval.code = LSHIFT_EXPR;	return LSHIFT;

    case CPP_COMPL:					return '~';
    case CPP_AND_AND:					return ANDAND;
    case CPP_OR_OR:					return OROR;
    case CPP_QUERY:					return '?';
    case CPP_OPEN_PAREN:				return '(';
    case CPP_EQ_EQ:	yylval.code = EQ_EXPR;		return EQCOMPARE;
    case CPP_NOT_EQ:	yylval.code = NE_EXPR;		return EQCOMPARE;
    case CPP_GREATER_EQ:yylval.code = GE_EXPR;		return ARITHCOMPARE;
    case CPP_LESS_EQ:	yylval.code = LE_EXPR;		return ARITHCOMPARE;

    case CPP_PLUS_EQ:	yylval.code = PLUS_EXPR;	return ASSIGN;
    case CPP_MINUS_EQ:	yylval.code = MINUS_EXPR;	return ASSIGN;
    case CPP_MULT_EQ:	yylval.code = MULT_EXPR;	return ASSIGN;
    case CPP_DIV_EQ:	yylval.code = TRUNC_DIV_EXPR;	return ASSIGN;
    case CPP_MOD_EQ:	yylval.code = TRUNC_MOD_EXPR;	return ASSIGN;
    case CPP_AND_EQ:	yylval.code = BIT_AND_EXPR;	return ASSIGN;
    case CPP_OR_EQ:	yylval.code = BIT_IOR_EXPR;	return ASSIGN;
    case CPP_XOR_EQ:	yylval.code = BIT_XOR_EXPR;	return ASSIGN;
    case CPP_RSHIFT_EQ:	yylval.code = RSHIFT_EXPR;	return ASSIGN;
    case CPP_LSHIFT_EQ:	yylval.code = LSHIFT_EXPR;	return ASSIGN;

    case CPP_OPEN_SQUARE:				return '[';
    case CPP_CLOSE_SQUARE:				return ']';
    case CPP_OPEN_BRACE:				return '{';
    case CPP_CLOSE_BRACE:				return '}';
    case CPP_ELLIPSIS:					return ELLIPSIS;

    case CPP_PLUS_PLUS:					return PLUSPLUS;
    case CPP_MINUS_MINUS:				return MINUSMINUS;
    case CPP_DEREF:					return POINTSAT;
    case CPP_DOT:					return '.';

      /* The following tokens may affect the interpretation of any
	 identifiers following, if doing Objective-C.  */
    case CPP_COLON:		OBJC_NEED_RAW_IDENTIFIER (0);	return ':';
    case CPP_COMMA:		OBJC_NEED_RAW_IDENTIFIER (0);	return ',';
    case CPP_CLOSE_PAREN:	OBJC_NEED_RAW_IDENTIFIER (0);	return ')';
    case CPP_SEMICOLON:		OBJC_NEED_RAW_IDENTIFIER (0);	return ';';

    case CPP_EOF:
      return 0;

    case CPP_NAME:
      return yylexname ();

    case CPP_AT_NAME:
      /* This only happens in Objective-C; it must be a keyword.  */
      return rid_to_yy [(int) C_RID_CODE (yylval.ttype)];

    case CPP_NUMBER:
    case CPP_CHAR:
    case CPP_WCHAR:
      return CONSTANT;

    case CPP_STRING:
    case CPP_WSTRING:
      return STRING;

    case CPP_OBJC_STRING:
      return OBJC_STRING;

      /* These tokens are C++ specific (and will not be generated
         in C mode, but let's be cautious).  */
    case CPP_SCOPE:
    case CPP_DEREF_STAR:
    case CPP_DOT_STAR:
    case CPP_MIN_EQ:
    case CPP_MAX_EQ:
    case CPP_MIN:
    case CPP_MAX:
      /* These tokens should not survive translation phase 4.  */
    case CPP_HASH:
    case CPP_PASTE:
      error ("syntax error at %qs token", NAME(last_token));
      goto get_next;

    default:
      abort ();
    }
  /* NOTREACHED */
}

static int
yylex (void)
{
  int r;
  timevar_push (TV_LEX);
  r = _yylex();
  timevar_pop (TV_LEX);
  return r;
}

/* Function used when yydebug is set, to print a token in more detail.  */

static void
yyprint (FILE *file, int yychar, YYSTYPE yyl)
{
  tree t = yyl.ttype;

  fprintf (file, " [%s]", NAME(last_token));

  switch (yychar)
    {
    case IDENTIFIER:
    case TYPENAME:
    case TYPESPEC:
    case TYPE_QUAL:
    case SCSPEC:
    case STATIC:
      if (IDENTIFIER_POINTER (t))
	fprintf (file, " '%s'", IDENTIFIER_POINTER (t));
      break;

    case CONSTANT:
      fprintf (file, " %s", GET_MODE_NAME (TYPE_MODE (TREE_TYPE (t))));
      if (TREE_CODE (t) == INTEGER_CST)
	{
	  fputs (" ", file);
	  fprintf (file, HOST_WIDE_INT_PRINT_DOUBLE_HEX,
		   TREE_INT_CST_HIGH (t), TREE_INT_CST_LOW (t));
	}
      break;
    }
}

/* This is not the ideal place to put this, but we have to get it out
   of c-lex.c because cp/lex.c has its own version.  */

/* Parse the file.  */
void
c_parse_file (void)
{
  yyparse ();

  if (malloced_yyss)
    {
      free (malloced_yyss);
      free (malloced_yyvs);
      malloced_yyss = 0;
    }
}

#ifdef __XGETTEXT__
/* Depending on the version of Bison used to compile this grammar,
   it may issue generic diagnostics spelled "syntax error" or
   "parse error".  To prevent this from changing the translation
   template randomly, we list all the variants of this particular
   diagnostic here.  Translators: there is no fine distinction
   between diagnostics with "syntax error" in them, and diagnostics
   with "parse error" in them.  It's okay to give them both the same
   translation.  */
const char d1[] = N_("syntax error");
const char d2[] = N_("parse error");
const char d3[] = N_("syntax error; also virtual memory exhausted");
const char d4[] = N_("parse error; also virtual memory exhausted");
const char d5[] = N_("syntax error: cannot back up");
const char d6[] = N_("parse error: cannot back up");
#endif

#include "gt-c-parse.h"


