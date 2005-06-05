/* A Bison parser, made from c-parse.y
   by GNU bison 1.35.  */

#define YYBISON 1  /* Identify Bison output.  */

# define	IDENTIFIER	257
# define	TYPENAME	258
# define	SCSPEC	259
# define	STATIC	260
# define	TYPESPEC	261
# define	TYPE_QUAL	262
# define	OBJC_TYPE_QUAL	263
# define	CONSTANT	264
# define	STRING	265
# define	ELLIPSIS	266
# define	SIZEOF	267
# define	ENUM	268
# define	STRUCT	269
# define	UNION	270
# define	IF	271
# define	ELSE	272
# define	WHILE	273
# define	DO	274
# define	FOR	275
# define	SWITCH	276
# define	CASE	277
# define	DEFAULT	278
# define	BREAK	279
# define	CONTINUE	280
# define	RETURN	281
# define	GOTO	282
# define	ASM_KEYWORD	283
# define	TYPEOF	284
# define	ALIGNOF	285
# define	ATTRIBUTE	286
# define	EXTENSION	287
# define	LABEL	288
# define	REALPART	289
# define	IMAGPART	290
# define	VA_ARG	291
# define	CHOOSE_EXPR	292
# define	TYPES_COMPATIBLE_P	293
# define	FUNC_NAME	294
# define	OFFSETOF	295
# define	ASSIGN	296
# define	OROR	297
# define	ANDAND	298
# define	EQCOMPARE	299
# define	ARITHCOMPARE	300
# define	LSHIFT	301
# define	RSHIFT	302
# define	UNARY	303
# define	PLUSPLUS	304
# define	MINUSMINUS	305
# define	HYPERUNARY	306
# define	POINTSAT	307
# define	AT_INTERFACE	308
# define	AT_IMPLEMENTATION	309
# define	AT_END	310
# define	AT_SELECTOR	311
# define	AT_DEFS	312
# define	AT_ENCODE	313
# define	CLASSNAME	314
# define	AT_PUBLIC	315
# define	AT_PRIVATE	316
# define	AT_PROTECTED	317
# define	AT_PROTOCOL	318
# define	AT_CLASS	319
# define	AT_ALIAS	320
# define	AT_THROW	321
# define	AT_TRY	322
# define	AT_CATCH	323
# define	AT_FINALLY	324
# define	AT_SYNCHRONIZED	325
# define	OBJC_STRING	326

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

#line 100 "c-parse.y"
#ifndef YYSTYPE
typedef union {long itype; tree ttype; void *otype; struct c_expr exprtype;
	struct c_arg_info *arginfotype; struct c_declarator *dtrtype;
	struct c_type_name *typenametype; struct c_parm *parmtype;
	struct c_declspecs *dsptype; struct c_typespec tstype;
	enum tree_code code; location_t location; } yystype;
# define YYSTYPE yystype
# define YYSTYPE_IS_TRIVIAL 1
#endif
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

#ifndef YYDEBUG
# define YYDEBUG 0
#endif



#define	YYFINAL		933
#define	YYFLAG		-32768
#define	YYNTBASE	95

/* YYTRANSLATE(YYLEX) -- Bison token number corresponding to YYLEX. */
#define YYTRANSLATE(x) ((unsigned)(x) <= 326 ? yytranslate[x] : 303)

/* YYTRANSLATE[YYLEX] -- Bison token number corresponding to YYLEX. */
static const char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    90,     2,     2,     2,    59,    50,     2,
      66,    92,    57,    55,    91,    56,    65,    58,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    45,    87,
       2,    43,     2,    44,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    67,     2,    94,    49,     2,     2,     2,     2,     2,
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
       2,     2,     2,     2,     2,     2,     1,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    46,    47,    51,
      52,    53,    54,    60,    61,    62,    63,    64,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86
};

#if YYDEBUG
static const short yyprhs[] =
{
       0,     0,     1,     3,     4,     8,     9,    14,    16,    18,
      20,    23,    24,    28,    33,    38,    41,    44,    47,    49,
      50,    51,    60,    65,    66,    67,    76,    81,    82,    83,
      91,    95,    97,    99,   101,   103,   105,   107,   109,   111,
     113,   115,   119,   120,   122,   124,   128,   130,   133,   136,
     139,   142,   145,   150,   153,   158,   161,   164,   166,   168,
     170,   172,   177,   179,   183,   187,   191,   195,   199,   203,
     207,   211,   215,   219,   223,   227,   228,   233,   234,   239,
     240,   241,   249,   250,   256,   260,   264,   266,   268,   270,
     272,   273,   281,   285,   289,   293,   297,   302,   309,   310,
     318,   323,   332,   337,   344,   349,   354,   358,   362,   365,
     368,   370,   374,   379,   380,   382,   385,   387,   389,   392,
     395,   400,   405,   408,   411,   414,   415,   417,   422,   427,
     431,   435,   438,   441,   443,   446,   449,   452,   455,   458,
     460,   463,   465,   468,   471,   474,   477,   480,   483,   485,
     488,   491,   494,   497,   500,   503,   506,   509,   512,   515,
     518,   521,   524,   527,   530,   533,   535,   538,   541,   544,
     547,   550,   553,   556,   559,   562,   565,   568,   571,   574,
     577,   580,   583,   586,   589,   592,   595,   598,   601,   604,
     607,   610,   613,   616,   619,   622,   625,   628,   631,   634,
     637,   640,   643,   646,   649,   652,   655,   658,   661,   664,
     667,   669,   671,   673,   675,   677,   679,   681,   683,   685,
     687,   689,   691,   693,   695,   697,   699,   701,   703,   705,
     707,   709,   711,   713,   715,   717,   719,   721,   723,   725,
     727,   729,   731,   733,   735,   737,   739,   741,   743,   745,
     747,   749,   751,   753,   755,   757,   759,   761,   763,   765,
     767,   769,   771,   773,   775,   777,   779,   780,   782,   784,
     786,   788,   790,   792,   794,   796,   801,   806,   808,   813,
     815,   820,   821,   828,   832,   833,   840,   844,   845,   847,
     849,   852,   861,   865,   867,   871,   872,   874,   879,   886,
     891,   893,   895,   897,   899,   901,   903,   905,   906,   911,
     913,   914,   917,   919,   923,   927,   930,   931,   936,   938,
     939,   944,   946,   948,   950,   953,   956,   958,   964,   968,
     969,   970,   977,   978,   979,   986,   988,   990,   995,   999,
    1002,  1006,  1008,  1010,  1012,  1016,  1019,  1021,  1025,  1028,
    1032,  1036,  1041,  1045,  1050,  1054,  1057,  1059,  1061,  1064,
    1066,  1069,  1071,  1074,  1075,  1083,  1089,  1090,  1098,  1104,
    1105,  1114,  1115,  1123,  1126,  1129,  1132,  1133,  1135,  1136,
    1138,  1140,  1143,  1144,  1148,  1151,  1155,  1158,  1162,  1164,
    1166,  1169,  1171,  1176,  1178,  1183,  1186,  1191,  1195,  1198,
    1203,  1207,  1209,  1213,  1215,  1217,  1221,  1222,  1226,  1227,
    1229,  1230,  1232,  1235,  1237,  1239,  1241,  1245,  1248,  1252,
    1257,  1261,  1264,  1267,  1269,  1274,  1278,  1283,  1289,  1295,
    1297,  1299,  1301,  1303,  1305,  1308,  1311,  1314,  1317,  1319,
    1322,  1325,  1328,  1330,  1333,  1336,  1339,  1342,  1344,  1347,
    1349,  1351,  1353,  1355,  1358,  1359,  1360,  1362,  1364,  1367,
    1371,  1373,  1376,  1378,  1380,  1384,  1386,  1388,  1391,  1394,
    1395,  1396,  1399,  1403,  1406,  1409,  1412,  1416,  1420,  1422,
    1432,  1442,  1450,  1458,  1459,  1460,  1470,  1471,  1472,  1486,
    1487,  1489,  1492,  1494,  1497,  1499,  1512,  1513,  1522,  1525,
    1527,  1529,  1531,  1533,  1535,  1538,  1541,  1544,  1548,  1550,
    1554,  1559,  1561,  1563,  1565,  1569,  1575,  1578,  1583,  1590,
    1591,  1593,  1596,  1601,  1610,  1612,  1616,  1622,  1630,  1631,
    1633,  1634,  1636,  1638,  1642,  1649,  1659,  1661,  1665,  1667,
    1668,  1669,  1670,  1674,  1677,  1678,  1679,  1686,  1689,  1690,
    1692,  1694,  1698,  1700,  1704,  1709,  1714,  1718,  1723,  1727,
    1732,  1737,  1741,  1746,  1750,  1752,  1753,  1757,  1759,  1762,
    1764,  1768,  1770,  1774
};
static const short yyrhs[] =
{
      -1,    96,     0,     0,   100,    97,    99,     0,     0,    96,
     100,    98,    99,     0,   102,     0,   101,     0,   276,     0,
     302,    99,     0,     0,   134,   168,    87,     0,   154,   134,
     168,    87,     0,   153,   134,   167,    87,     0,   160,    87,
       0,     1,    87,     0,     1,    88,     0,    87,     0,     0,
       0,   153,   134,   197,   103,   129,   249,   104,   243,     0,
     153,   134,   197,     1,     0,     0,     0,   154,   134,   202,
     105,   129,   249,   106,   243,     0,   154,   134,   202,     1,
       0,     0,     0,   134,   202,   107,   129,   249,   108,   243,
       0,   134,   202,     1,     0,     3,     0,     4,     0,    50,
       0,    56,     0,    55,     0,    61,     0,    62,     0,    89,
       0,    90,     0,   119,     0,   111,    91,   119,     0,     0,
     113,     0,   119,     0,   113,    91,   119,     0,   125,     0,
      57,   118,     0,   302,   118,     0,   110,   118,     0,    47,
     109,     0,   115,   114,     0,   115,    66,   223,    92,     0,
     116,   114,     0,   116,    66,   223,    92,     0,    35,   118,
       0,    36,   118,     0,    13,     0,    31,     0,    30,     0,
     114,     0,    66,   223,    92,   118,     0,   118,     0,   119,
      55,   119,     0,   119,    56,   119,     0,   119,    57,   119,
       0,   119,    58,   119,     0,   119,    59,   119,     0,   119,
      53,   119,     0,   119,    54,   119,     0,   119,    52,   119,
       0,   119,    51,   119,     0,   119,    50,   119,     0,   119,
      48,   119,     0,   119,    49,   119,     0,     0,   119,    47,
     120,   119,     0,     0,   119,    46,   121,   119,     0,     0,
       0,   119,    44,   122,   111,    45,   123,   119,     0,     0,
     119,    44,   124,    45,   119,     0,   119,    43,   119,     0,
     119,    42,   119,     0,     3,     0,    10,     0,    11,     0,
      40,     0,     0,    66,   223,    92,    93,   126,   182,    88,
       0,    66,   111,    92,     0,    66,     1,    92,     0,   247,
     245,    92,     0,   247,     1,    92,     0,   125,    66,   112,
      92,     0,    37,    66,   119,    91,   223,    92,     0,     0,
      41,    66,   223,    91,   127,   128,    92,     0,    41,    66,
       1,    92,     0,    38,    66,   119,    91,   119,    91,   119,
      92,     0,    38,    66,     1,    92,     0,    39,    66,   223,
      91,   223,    92,     0,    39,    66,     1,    92,     0,   125,
      67,   111,    94,     0,   125,    65,   109,     0,   125,    64,
     109,     0,   125,    61,     0,   125,    62,     0,   109,     0,
     128,    65,   109,     0,   128,    67,   111,    94,     0,     0,
     131,     0,   249,   132,     0,   130,     0,   238,     0,   131,
     130,     0,   130,   238,     0,   155,   134,   167,    87,     0,
     156,   134,   168,    87,     0,   155,    87,     0,   156,    87,
       0,   249,   136,     0,     0,   173,     0,   153,   134,   167,
      87,     0,   154,   134,   168,    87,     0,   153,   134,   191,
       0,   154,   134,   194,     0,   160,    87,     0,   302,   136,
       0,     8,     0,   137,     8,     0,   138,     8,     0,   137,
     174,     0,   139,     8,     0,   140,     8,     0,   174,     0,
     139,   174,     0,   162,     0,   141,     8,     0,   142,     8,
       0,   141,   164,     0,   142,   164,     0,   137,   162,     0,
     138,   162,     0,   163,     0,   141,   174,     0,   141,   165,
       0,   142,   165,     0,   137,   163,     0,   138,   163,     0,
     143,     8,     0,   144,     8,     0,   143,   164,     0,   144,
     164,     0,   139,   162,     0,   140,   162,     0,   143,   174,
       0,   143,   165,     0,   144,   165,     0,   139,   163,     0,
     140,   163,     0,   179,     0,   145,     8,     0,   146,     8,
       0,   137,   179,     0,   138,   179,     0,   145,   179,     0,
     146,   179,     0,   145,   174,     0,   147,     8,     0,   148,
       8,     0,   139,   179,     0,   140,   179,     0,   147,   179,
       0,   148,   179,     0,   147,   174,     0,   149,     8,     0,
     150,     8,     0,   149,   164,     0,   150,   164,     0,   145,
     162,     0,   146,   162,     0,   141,   179,     0,   142,   179,
       0,   149,   179,     0,   150,   179,     0,   149,   174,     0,
     149,   165,     0,   150,   165,     0,   145,   163,     0,   146,
     163,     0,   151,     8,     0,   152,     8,     0,   151,   164,
       0,   152,   164,     0,   147,   162,     0,   148,   162,     0,
     143,   179,     0,   144,   179,     0,   151,   179,     0,   152,
     179,     0,   151,   174,     0,   151,   165,     0,   152,   165,
       0,   147,   163,     0,   148,   163,     0,   141,     0,   142,
       0,   143,     0,   144,     0,   149,     0,   150,     0,   151,
       0,   152,     0,   137,     0,   138,     0,   139,     0,   140,
       0,   145,     0,   146,     0,   147,     0,   148,     0,   141,
       0,   142,     0,   149,     0,   150,     0,   137,     0,   138,
       0,   145,     0,   146,     0,   141,     0,   142,     0,   143,
       0,   144,     0,   137,     0,   138,     0,   139,     0,   140,
       0,   141,     0,   142,     0,   143,     0,   144,     0,   137,
       0,   138,     0,   139,     0,   140,     0,   137,     0,   138,
       0,   139,     0,   140,     0,   141,     0,   142,     0,   143,
       0,   144,     0,   145,     0,   146,     0,   147,     0,   148,
       0,   149,     0,   150,     0,   151,     0,   152,     0,     0,
     158,     0,   164,     0,   166,     0,   165,     0,     7,     0,
     211,     0,   206,     0,     4,     0,   117,    66,   111,    92,
       0,   117,    66,   223,    92,     0,   169,     0,   167,    91,
     135,   169,     0,   171,     0,   168,    91,   135,   171,     0,
       0,   197,   275,   173,    43,   170,   180,     0,   197,   275,
     173,     0,     0,   202,   275,   173,    43,   172,   180,     0,
     202,   275,   173,     0,     0,   174,     0,   175,     0,   174,
     175,     0,    32,   285,    66,    66,   176,    92,    92,   286,
       0,    32,     1,   286,     0,   177,     0,   176,    91,   177,
       0,     0,   178,     0,   178,    66,     3,    92,     0,   178,
      66,     3,    91,   113,    92,     0,   178,    66,   112,    92,
       0,   109,     0,   179,     0,     7,     0,     8,     0,     6,
       0,     5,     0,   119,     0,     0,    93,   181,   182,    88,
       0,     1,     0,     0,   183,   212,     0,   184,     0,   183,
      91,   184,     0,   188,    43,   186,     0,   190,   186,     0,
       0,   109,    45,   185,   186,     0,   186,     0,     0,    93,
     187,   182,    88,     0,   119,     0,     1,     0,   189,     0,
     188,   189,     0,    65,   109,     0,   190,     0,    67,   119,
      12,   119,    94,     0,    67,   119,    94,     0,     0,     0,
     197,   192,   129,   249,   193,   248,     0,     0,     0,   202,
     195,   129,   249,   196,   248,     0,   198,     0,   202,     0,
      66,   173,   198,    92,     0,   198,    66,   297,     0,   198,
     231,     0,    57,   161,   198,     0,     4,     0,   200,     0,
     201,     0,   200,    66,   297,     0,   200,   231,     0,     4,
       0,   201,    66,   297,     0,   201,   231,     0,    57,   161,
     200,     0,    57,   161,   201,     0,    66,   173,   201,    92,
       0,   202,    66,   297,     0,    66,   173,   202,    92,     0,
      57,   161,   202,     0,   202,   231,     0,     3,     0,    15,
       0,    15,   174,     0,    16,     0,    16,   174,     0,    14,
       0,    14,   174,     0,     0,   203,   109,    93,   207,   214,
      88,   173,     0,   203,    93,   214,    88,   173,     0,     0,
     204,   109,    93,   208,   214,    88,   173,     0,   204,    93,
     214,    88,   173,     0,     0,   205,   109,    93,   209,   221,
     213,    88,   173,     0,     0,   205,    93,   210,   221,   213,
      88,   173,     0,   203,   109,     0,   204,   109,     0,   205,
     109,     0,     0,    91,     0,     0,    91,     0,   215,     0,
     215,   216,     0,     0,   215,   216,    87,     0,   215,    87,
       0,   157,   134,   217,     0,   157,   134,     0,   158,   134,
     218,     0,   158,     0,     1,     0,   302,   216,     0,   219,
       0,   217,    91,   135,   219,     0,   220,     0,   218,    91,
     135,   220,     0,   197,   173,     0,   197,    45,   119,   173,
       0,    45,   119,   173,     0,   202,   173,     0,   202,    45,
     119,   173,     0,    45,   119,   173,     0,   222,     0,   221,
      91,   222,     0,     1,     0,   109,     0,   109,    43,   119,
       0,     0,   159,   224,   225,     0,     0,   227,     0,     0,
     227,     0,   228,   174,     0,   229,     0,   228,     0,   230,
       0,    57,   161,   228,     0,    57,   161,     0,    57,   161,
     229,     0,    66,   173,   227,    92,     0,   230,    66,   287,
       0,   230,   231,     0,    66,   287,     0,   231,     0,    67,
     161,   119,    94,     0,    67,   161,    94,     0,    67,   161,
      57,    94,     0,    67,     6,   161,   119,    94,     0,    67,
     158,     6,   119,    94,     0,   233,     0,   234,     0,   235,
       0,   236,     0,   252,     0,   233,   252,     0,   234,   252,
       0,   235,   252,     0,   236,   252,     0,   133,     0,   233,
     133,     0,   234,   133,     0,   236,   133,     0,   253,     0,
     233,   253,     0,   234,   253,     0,   235,   253,     0,   236,
     253,     0,   238,     0,   237,   238,     0,   233,     0,   234,
       0,   235,     0,   236,     0,     1,    87,     0,     0,     0,
     241,     0,   242,     0,   241,   242,     0,    34,   301,    87,
       0,   248,     0,     1,   248,     0,    93,     0,    88,     0,
     240,   246,    88,     0,   232,     0,     1,     0,    66,    93,
       0,   244,   245,     0,     0,     0,   250,   253,     0,   239,
     250,   252,     0,   249,   272,     0,   249,   273,     0,   249,
     111,     0,   239,   250,   257,     0,   239,   250,    87,     0,
     251,     0,    17,   239,   249,    66,   254,    92,   255,    18,
     256,     0,    17,   239,   249,    66,   254,    92,   256,    18,
     256,     0,    17,   239,   249,    66,   254,    92,   255,     0,
      17,   239,   249,    66,   254,    92,   256,     0,     0,     0,
      19,   239,   249,    66,   254,    92,   258,   259,   251,     0,
       0,     0,    20,   239,   249,   258,   259,   251,    19,   262,
     263,    66,   254,    92,    87,     0,     0,   111,     0,   264,
      87,     0,   136,     0,   249,   264,     0,   264,     0,    21,
     239,    66,   265,   249,   266,    87,   267,    92,   258,   259,
     251,     0,     0,    22,   239,    66,   111,    92,   270,   258,
     251,     0,   111,    87,     0,   257,     0,   260,     0,   261,
       0,   268,     0,   269,     0,    25,    87,     0,    26,    87,
       0,    27,    87,     0,    27,   111,    87,     0,   277,     0,
      28,   109,    87,     0,    28,    57,   111,    87,     0,    87,
       0,   248,     0,   271,     0,    23,   119,    45,     0,    23,
     119,    12,   119,    45,     0,    24,    45,     0,   109,   249,
      45,   173,     0,    29,   285,    66,   284,    92,   286,     0,
       0,   274,     0,   274,    87,     0,    29,     1,   286,    87,
       0,    29,   279,   285,    66,   278,    92,   286,    87,     0,
     284,     0,   284,    45,   280,     0,   284,    45,   280,    45,
     280,     0,   284,    45,   280,    45,   280,    45,   283,     0,
       0,     8,     0,     0,   281,     0,   282,     0,   281,    91,
     282,     0,   284,   286,    66,   111,    92,   285,     0,    67,
     109,    94,   284,   286,    66,   111,    92,   285,     0,   284,
       0,   283,    91,   284,     0,    11,     0,     0,     0,     0,
     173,   288,   289,     0,   292,    92,     0,     0,     0,   293,
      87,   290,   173,   291,   289,     0,     1,    92,     0,     0,
      12,     0,   293,     0,   293,    91,    12,     0,   295,     0,
     293,    91,   294,     0,   153,   134,   199,   173,     0,   153,
     134,   202,   173,     0,   153,   134,   226,     0,   154,   134,
     202,   173,     0,   154,   134,   226,     0,   155,   296,   199,
     173,     0,   155,   296,   202,   173,     0,   155,   296,   226,
       0,   156,   296,   202,   173,     0,   156,   296,   226,     0,
     134,     0,     0,   173,   298,   299,     0,   289,     0,   300,
      92,     0,     3,     0,   300,    91,     3,     0,   109,     0,
     301,    91,   109,     0,    33,     0
};

#endif

#if YYDEBUG
/* YYRLINE[YYN] -- source line where rule number YYN was defined. */
static const short yyrline[] =
{
       0,   341,   345,   352,   352,   355,   355,   360,   362,   363,
     364,   370,   374,   378,   380,   382,   384,   385,   386,   391,
     391,   391,   403,   405,   405,   405,   416,   418,   418,   418,
     429,   433,   435,   438,   440,   442,   447,   449,   451,   453,
     457,   458,   463,   466,   469,   472,   476,   478,   482,   485,
     490,   493,   500,   504,   509,   514,   517,   522,   526,   530,
     534,   536,   541,   543,   545,   547,   549,   551,   553,   555,
     557,   559,   561,   563,   565,   567,   567,   574,   574,   581,
     581,   581,   593,   593,   605,   609,   616,   624,   626,   628,
     631,   631,   652,   657,   659,   665,   670,   673,   677,   677,
     687,   689,   701,   703,   715,   717,   720,   723,   729,   732,
     741,   744,   746,   750,   752,   758,   763,   765,   766,   767,
     774,   777,   779,   782,   790,   799,   819,   824,   827,   829,
     831,   833,   835,   880,   883,   885,   889,   894,   897,   901,
     904,   908,   911,   913,   915,   917,   919,   921,   925,   928,
     930,   932,   934,   936,   940,   943,   945,   947,   949,   951,
     955,   958,   960,   962,   964,   968,   971,   973,   975,   977,
     979,   981,   985,   990,   993,   995,   997,   999,  1001,  1005,
    1010,  1013,  1015,  1017,  1019,  1021,  1023,  1025,  1027,  1029,
    1033,  1036,  1038,  1040,  1042,  1046,  1049,  1051,  1053,  1055,
    1057,  1059,  1061,  1063,  1065,  1069,  1072,  1074,  1076,  1078,
    1083,  1085,  1086,  1087,  1088,  1089,  1090,  1091,  1094,  1096,
    1097,  1098,  1099,  1100,  1101,  1102,  1105,  1107,  1108,  1109,
    1112,  1114,  1115,  1116,  1119,  1121,  1122,  1123,  1126,  1128,
    1129,  1130,  1133,  1135,  1136,  1137,  1138,  1139,  1140,  1141,
    1144,  1146,  1147,  1148,  1149,  1150,  1151,  1152,  1153,  1154,
    1155,  1156,  1157,  1158,  1159,  1160,  1164,  1167,  1192,  1194,
    1197,  1201,  1206,  1209,  1213,  1219,  1229,  1240,  1242,  1245,
    1247,  1250,  1250,  1266,  1274,  1274,  1290,  1298,  1301,  1305,
    1308,  1312,  1316,  1320,  1323,  1327,  1330,  1332,  1334,  1336,
    1343,  1345,  1346,  1347,  1350,  1352,  1357,  1360,  1360,  1364,
    1369,  1373,  1376,  1378,  1383,  1387,  1390,  1390,  1396,  1399,
    1399,  1404,  1406,  1409,  1411,  1414,  1417,  1420,  1425,  1429,
    1429,  1429,  1459,  1459,  1459,  1492,  1494,  1499,  1502,  1504,
    1506,  1508,  1516,  1518,  1521,  1524,  1526,  1530,  1533,  1535,
    1537,  1539,  1546,  1549,  1551,  1553,  1555,  1559,  1562,  1566,
    1569,  1573,  1576,  1586,  1586,  1595,  1601,  1601,  1607,  1613,
    1613,  1619,  1619,  1627,  1630,  1632,  1640,  1642,  1645,  1647,
    1664,  1667,  1672,  1674,  1676,  1681,  1685,  1693,  1696,  1701,
    1703,  1708,  1710,  1714,  1716,  1720,  1725,  1729,  1736,  1741,
    1745,  1755,  1757,  1762,  1767,  1770,  1774,  1774,  1784,  1787,
    1790,  1794,  1797,  1803,  1805,  1808,  1810,  1814,  1818,  1822,
    1825,  1827,  1829,  1832,  1839,  1842,  1844,  1846,  1849,  1859,
    1861,  1862,  1866,  1869,  1871,  1872,  1873,  1874,  1877,  1879,
    1885,  1886,  1889,  1891,  1892,  1893,  1894,  1897,  1899,  1902,
    1904,  1905,  1906,  1909,  1913,  1919,  1921,  1926,  1928,  1931,
    1945,  1948,  1951,  1954,  1955,  1958,  1960,  1963,  1975,  1983,
    1989,  1991,  1995,  2000,  2018,  2023,  2035,  2040,  2045,  2048,
    2053,  2057,  2061,  2067,  2071,  2075,  2084,  2084,  2084,  2095,
    2098,  2102,  2105,  2109,  2121,  2125,  2135,  2135,  2148,  2151,
    2153,  2155,  2157,  2159,  2161,  2163,  2165,  2167,  2169,  2170,
    2172,  2174,  2179,  2182,  2189,  2191,  2193,  2195,  2211,  2218,
    2221,  2225,  2228,  2234,  2240,  2245,  2248,  2251,  2257,  2260,
    2273,  2275,  2278,  2280,  2284,  2289,  2296,  2299,  2304,  2316,
    2320,  2330,  2330,  2339,  2341,  2341,  2341,  2348,  2358,  2364,
    2373,  2375,  2379,  2382,  2388,  2393,  2397,  2400,  2405,  2412,
    2417,  2421,  2424,  2429,  2434,  2443,  2443,  2452,  2454,  2468,
    2471,  2476,  2479,  2483
};
#endif


#if (YYDEBUG) || defined YYERROR_VERBOSE

/* YYTNAME[TOKEN_NUM] -- String name of the token TOKEN_NUM. */
static const char *const yytname[] =
{
  "$", "error", "$undefined.", "IDENTIFIER", "TYPENAME", "SCSPEC", "STATIC", 
  "TYPESPEC", "TYPE_QUAL", "OBJC_TYPE_QUAL", "CONSTANT", "STRING", 
  "ELLIPSIS", "SIZEOF", "ENUM", "STRUCT", "UNION", "IF", "ELSE", "WHILE", 
  "DO", "FOR", "SWITCH", "CASE", "DEFAULT", "BREAK", "CONTINUE", "RETURN", 
  "GOTO", "ASM_KEYWORD", "TYPEOF", "ALIGNOF", "ATTRIBUTE", "EXTENSION", 
  "LABEL", "REALPART", "IMAGPART", "VA_ARG", "CHOOSE_EXPR", 
  "TYPES_COMPATIBLE_P", "FUNC_NAME", "OFFSETOF", "ASSIGN", "'='", "'?'", 
  "':'", "OROR", "ANDAND", "'|'", "'^'", "'&'", "EQCOMPARE", 
  "ARITHCOMPARE", "LSHIFT", "RSHIFT", "'+'", "'-'", "'*'", "'/'", "'%'", 
  "UNARY", "PLUSPLUS", "MINUSMINUS", "HYPERUNARY", "POINTSAT", "'.'", 
  "'('", "'['", "AT_INTERFACE", "AT_IMPLEMENTATION", "AT_END", 
  "AT_SELECTOR", "AT_DEFS", "AT_ENCODE", "CLASSNAME", "AT_PUBLIC", 
  "AT_PRIVATE", "AT_PROTECTED", "AT_PROTOCOL", "AT_CLASS", "AT_ALIAS", 
  "AT_THROW", "AT_TRY", "AT_CATCH", "AT_FINALLY", "AT_SYNCHRONIZED", 
  "OBJC_STRING", "';'", "'}'", "'~'", "'!'", "','", "')'", "'{'", "']'", 
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

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives. */
static const short yyr1[] =
{
       0,    95,    95,    97,    96,    98,    96,    99,    99,    99,
      99,   100,   101,   101,   101,   101,   101,   101,   101,   103,
     104,   102,   102,   105,   106,   102,   102,   107,   108,   102,
     102,   109,   109,   110,   110,   110,   110,   110,   110,   110,
     111,   111,   112,   112,   113,   113,   114,   114,   114,   114,
     114,   114,   114,   114,   114,   114,   114,   115,   116,   117,
     118,   118,   119,   119,   119,   119,   119,   119,   119,   119,
     119,   119,   119,   119,   119,   120,   119,   121,   119,   122,
     123,   119,   124,   119,   119,   119,   125,   125,   125,   125,
     126,   125,   125,   125,   125,   125,   125,   125,   127,   125,
     125,   125,   125,   125,   125,   125,   125,   125,   125,   125,
     128,   128,   128,   129,   129,   130,   131,   131,   131,   131,
     132,   132,   132,   132,   133,   134,   135,   136,   136,   136,
     136,   136,   136,   137,   137,   137,   138,   139,   139,   140,
     140,   141,   141,   141,   141,   141,   141,   141,   142,   142,
     142,   142,   142,   142,   143,   143,   143,   143,   143,   143,
     144,   144,   144,   144,   144,   145,   145,   145,   145,   145,
     145,   145,   146,   147,   147,   147,   147,   147,   147,   148,
     149,   149,   149,   149,   149,   149,   149,   149,   149,   149,
     150,   150,   150,   150,   150,   151,   151,   151,   151,   151,
     151,   151,   151,   151,   151,   152,   152,   152,   152,   152,
     153,   153,   153,   153,   153,   153,   153,   153,   154,   154,
     154,   154,   154,   154,   154,   154,   155,   155,   155,   155,
     156,   156,   156,   156,   157,   157,   157,   157,   158,   158,
     158,   158,   159,   159,   159,   159,   159,   159,   159,   159,
     160,   160,   160,   160,   160,   160,   160,   160,   160,   160,
     160,   160,   160,   160,   160,   160,   161,   161,   162,   162,
     163,   164,   164,   165,   166,   166,   166,   167,   167,   168,
     168,   170,   169,   169,   172,   171,   171,   173,   173,   174,
     174,   175,   175,   176,   176,   177,   177,   177,   177,   177,
     178,   178,   178,   178,   179,   179,   180,   181,   180,   180,
     182,   182,   183,   183,   184,   184,   185,   184,   184,   187,
     186,   186,   186,   188,   188,   189,   189,   190,   190,   192,
     193,   191,   195,   196,   194,   197,   197,   198,   198,   198,
     198,   198,   199,   199,   200,   200,   200,   201,   201,   201,
     201,   201,   202,   202,   202,   202,   202,   203,   203,   204,
     204,   205,   205,   207,   206,   206,   208,   206,   206,   209,
     206,   210,   206,   211,   211,   211,   212,   212,   213,   213,
     214,   214,   215,   215,   215,   216,   216,   216,   216,   216,
     216,   217,   217,   218,   218,   219,   219,   219,   220,   220,
     220,   221,   221,   221,   222,   222,   224,   223,   225,   225,
     226,   226,   226,   227,   227,   228,   228,   229,   229,   230,
     230,   230,   230,   230,   231,   231,   231,   231,   231,   232,
     232,   232,   232,   233,   233,   233,   233,   233,   234,   234,
     234,   234,   235,   235,   235,   235,   235,   236,   236,   237,
     237,   237,   237,   238,   239,   240,   240,   241,   241,   242,
     243,   243,   244,   245,   245,   246,   246,   247,   248,   249,
     250,   250,   251,   252,   253,   254,   255,   256,   256,   257,
     257,   257,   257,   258,   259,   260,   262,   263,   261,   264,
     264,   265,   265,   266,   267,   268,   270,   269,   271,   271,
     271,   271,   271,   271,   271,   271,   271,   271,   271,   271,
     271,   271,   272,   272,   273,   273,   273,   273,   274,   275,
     275,   276,   276,   277,   278,   278,   278,   278,   279,   279,
     280,   280,   281,   281,   282,   282,   283,   283,   284,   285,
     286,   288,   287,   289,   290,   291,   289,   289,   292,   292,
     292,   292,   293,   293,   294,   294,   294,   294,   294,   295,
     295,   295,   295,   295,   296,   298,   297,   299,   299,   300,
     300,   301,   301,   302
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN. */
static const short yyr2[] =
{
       0,     0,     1,     0,     3,     0,     4,     1,     1,     1,
       2,     0,     3,     4,     4,     2,     2,     2,     1,     0,
       0,     8,     4,     0,     0,     8,     4,     0,     0,     7,
       3,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     3,     0,     1,     1,     3,     1,     2,     2,     2,
       2,     2,     4,     2,     4,     2,     2,     1,     1,     1,
       1,     4,     1,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     0,     4,     0,     4,     0,
       0,     7,     0,     5,     3,     3,     1,     1,     1,     1,
       0,     7,     3,     3,     3,     3,     4,     6,     0,     7,
       4,     8,     4,     6,     4,     4,     3,     3,     2,     2,
       1,     3,     4,     0,     1,     2,     1,     1,     2,     2,
       4,     4,     2,     2,     2,     0,     1,     4,     4,     3,
       3,     2,     2,     1,     2,     2,     2,     2,     2,     1,
       2,     1,     2,     2,     2,     2,     2,     2,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     1,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     0,     1,     1,     1,
       1,     1,     1,     1,     1,     4,     4,     1,     4,     1,
       4,     0,     6,     3,     0,     6,     3,     0,     1,     1,
       2,     8,     3,     1,     3,     0,     1,     4,     6,     4,
       1,     1,     1,     1,     1,     1,     1,     0,     4,     1,
       0,     2,     1,     3,     3,     2,     0,     4,     1,     0,
       4,     1,     1,     1,     2,     2,     1,     5,     3,     0,
       0,     6,     0,     0,     6,     1,     1,     4,     3,     2,
       3,     1,     1,     1,     3,     2,     1,     3,     2,     3,
       3,     4,     3,     4,     3,     2,     1,     1,     2,     1,
       2,     1,     2,     0,     7,     5,     0,     7,     5,     0,
       8,     0,     7,     2,     2,     2,     0,     1,     0,     1,
       1,     2,     0,     3,     2,     3,     2,     3,     1,     1,
       2,     1,     4,     1,     4,     2,     4,     3,     2,     4,
       3,     1,     3,     1,     1,     3,     0,     3,     0,     1,
       0,     1,     2,     1,     1,     1,     3,     2,     3,     4,
       3,     2,     2,     1,     4,     3,     4,     5,     5,     1,
       1,     1,     1,     1,     2,     2,     2,     2,     1,     2,
       2,     2,     1,     2,     2,     2,     2,     1,     2,     1,
       1,     1,     1,     2,     0,     0,     1,     1,     2,     3,
       1,     2,     1,     1,     3,     1,     1,     2,     2,     0,
       0,     2,     3,     2,     2,     2,     3,     3,     1,     9,
       9,     7,     7,     0,     0,     9,     0,     0,    13,     0,
       1,     2,     1,     2,     1,    12,     0,     8,     2,     1,
       1,     1,     1,     1,     2,     2,     2,     3,     1,     3,
       4,     1,     1,     1,     3,     5,     2,     4,     6,     0,
       1,     2,     4,     8,     1,     3,     5,     7,     0,     1,
       0,     1,     1,     3,     6,     9,     1,     3,     1,     0,
       0,     0,     3,     2,     0,     0,     6,     2,     0,     1,
       1,     3,     1,     3,     4,     4,     3,     4,     3,     4,
       4,     3,     4,     3,     1,     0,     3,     1,     2,     1,
       3,     1,     3,     1
};

/* YYDEFACT[S] -- default rule to reduce with in state S when YYTABLE
   doesn't specify something else to do.  Zero means the default is an
   error. */
static const short yydefact[] =
{
      11,    11,     3,     5,     0,     0,     0,   274,   305,   304,
     271,   133,   361,   357,   359,     0,    59,     0,   573,    18,
       4,     8,     7,     0,     0,   218,   219,   220,   221,   210,
     211,   212,   213,   222,   223,   224,   225,   214,   215,   216,
     217,   125,   125,     0,   141,   148,   268,   270,   269,   139,
     289,   165,     0,     0,     0,   273,   272,     0,     9,     0,
       6,    16,    17,   362,   358,   360,   540,     0,   540,     0,
       0,   356,   266,   287,     0,   279,     0,   134,   146,   152,
     136,   168,   135,   147,   153,   169,   137,   158,   163,   140,
     175,   138,   159,   164,   176,   142,   144,   150,   149,   186,
     143,   145,   151,   187,   154,   156,   161,   160,   201,   155,
     157,   162,   202,   166,   184,   193,   172,   170,   167,   185,
     194,   171,   173,   199,   208,   179,   177,   174,   200,   209,
     178,   180,   182,   191,   190,   188,   181,   183,   192,   189,
     195,   197,   206,   205,   203,   196,   198,   207,   204,     0,
       0,    15,   290,    31,    32,   382,   373,   382,   374,   371,
     375,   521,    10,     0,     0,   292,     0,    86,    87,    88,
      57,    58,     0,     0,     0,     0,     0,    89,     0,     0,
      33,    35,    34,     0,    36,    37,     0,    38,    39,     0,
       0,    60,     0,     0,    62,    40,    46,   246,   247,   248,
     249,   242,   243,   244,   245,   406,     0,     0,     0,   238,
     239,   240,   241,   267,     0,     0,   288,    12,   287,    30,
     539,   287,   266,     0,   355,   520,   287,   341,   266,   287,
       0,   277,     0,   335,   336,     0,     0,     0,     0,   363,
       0,   366,     0,   369,   522,   538,     0,   295,    55,    56,
       0,     0,     0,     0,    50,    47,     0,   467,     0,     0,
      49,     0,   275,     0,    51,     0,    53,     0,     0,    79,
      77,    75,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   108,   109,     0,     0,    42,     0,
     408,   276,     0,     0,   463,     0,   456,   457,     0,    48,
     354,     0,     0,   126,   565,   352,   266,   267,     0,     0,
     469,     0,   469,   117,     0,   286,     0,     0,    14,   287,
      22,     0,   287,   287,   339,    13,    26,     0,   287,   389,
     384,   238,   239,   240,   241,   234,   235,   236,   237,   125,
     125,   381,     0,   382,   287,   382,   403,   404,   378,   401,
       0,   540,   302,   303,   300,     0,   293,   296,   301,     0,
       0,     0,     0,     0,     0,     0,    93,    92,     0,    41,
       0,     0,    85,    84,     0,     0,     0,     0,    73,    74,
      72,    71,    70,    68,    69,    63,    64,    65,    66,    67,
     107,   106,     0,    43,    44,     0,   266,   287,   407,   409,
     414,   413,   415,   423,    95,   571,     0,   466,   438,   465,
     469,   469,   469,   469,     0,   447,     0,     0,   433,   442,
     458,    94,   353,   280,   519,     0,     0,     0,     0,   425,
       0,   453,    28,   119,   118,   115,   230,   231,   226,   227,
     232,   233,   228,   229,   125,   125,   284,   340,     0,     0,
     469,   283,   338,   469,   365,   386,     0,   383,   390,     0,
     368,     0,     0,   379,     0,   378,   518,   295,     0,    42,
       0,   102,     0,   104,     0,   100,    98,    90,    61,    52,
      54,     0,     0,    78,    76,    96,     0,   105,   417,   541,
     422,   287,   421,   459,     0,   439,   434,   443,   440,   435,
     444,     0,   436,   445,   441,   437,   446,   448,   464,    86,
     274,   454,   454,   454,   454,   454,     0,     0,     0,     0,
       0,     0,   528,   511,   462,   469,     0,   124,   125,   125,
       0,   455,   512,   499,   500,   501,   502,   503,   513,   473,
     474,   508,     0,     0,   569,   549,   125,   125,   567,     0,
     550,   552,   566,     0,     0,     0,   426,   424,     0,   122,
       0,   123,     0,     0,   337,   278,   519,    20,   281,    24,
       0,   287,   385,   391,     0,   287,   387,   393,   287,   287,
     405,   402,   287,     0,   294,   540,    86,     0,     0,     0,
       0,     0,     0,    80,    83,    45,   416,   418,     0,     0,
     541,   420,   572,   469,   469,   469,     0,     0,     0,   516,
     504,   505,   506,     0,     0,     0,   529,   539,     0,   498,
       0,     0,   131,   468,   132,   547,   564,   410,   410,   543,
     544,     0,     0,   568,   427,   428,     0,    29,   460,     0,
       0,   309,   307,   306,   285,     0,     0,     0,   287,     0,
     395,   287,   287,     0,   398,   287,   364,   367,   372,   287,
     291,     0,   297,   299,    97,     0,   103,   110,     0,   322,
       0,     0,   319,     0,   321,     0,   376,   312,   318,     0,
     323,     0,     0,   419,   542,     0,     0,   483,   489,     0,
       0,   514,   507,     0,   509,     0,   287,     0,   129,   329,
       0,   130,   332,   346,   266,   287,   287,   342,   343,   287,
     561,   411,   414,   266,   287,   287,   563,   287,   551,   218,
     219,   220,   221,   210,   211,   212,   213,   222,   223,   224,
     225,   214,   215,   216,   217,   125,   125,   553,   570,   461,
     120,   121,     0,    21,   282,    25,   397,   287,     0,   400,
     287,     0,   370,     0,     0,     0,     0,    99,   325,     0,
       0,   316,    91,     0,   311,     0,   324,   326,   315,    81,
     469,   469,   484,   490,   492,     0,   469,     0,     0,   510,
       0,   517,   127,     0,   128,     0,   417,   541,   559,   287,
     345,   287,   348,   560,   412,   417,   541,   562,   545,   410,
     410,     0,   396,   392,   399,   394,   298,   101,   111,     0,
       0,   328,     0,     0,   313,   314,     0,     0,     0,   454,
     491,   469,   496,   515,     0,   524,   469,   469,   349,   350,
       0,   344,   347,     0,   287,   287,   556,   287,   558,   308,
     112,     0,   320,   317,   475,   454,   483,   470,     0,   489,
       0,   483,   540,   530,   330,   333,   351,   546,   554,   555,
     557,   327,   470,   478,   481,   482,   484,   469,   486,   493,
     489,   454,     0,     0,   525,   531,   532,   540,     0,     0,
     469,   454,   454,   454,   472,   471,   487,   494,     0,   497,
     523,     0,   530,     0,     0,   331,   334,   477,   476,   470,
     479,   480,   485,     0,   483,     0,   526,   533,     0,   469,
     469,   484,   540,     0,     0,     0,   454,     0,   527,   536,
     539,     0,   495,     0,     0,   534,   488,     0,   537,   539,
     535,     0,     0,     0
};

static const short yydefgoto[] =
{
     931,     1,     4,     5,    20,     2,    21,    22,   321,   645,
     327,   647,   223,   558,   673,   189,   258,   392,   393,   191,
     192,   193,    23,   194,   195,   377,   376,   374,   682,   375,
     196,   592,   591,   668,   310,   311,   312,   435,   408,    24,
     302,   527,   197,   198,   199,   200,   201,   202,   203,   204,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
     546,   547,   339,   213,   205,    43,   214,    44,    45,    46,
      47,    48,   230,    74,   231,   646,    75,   563,   303,   216,
      50,   355,   356,   357,    51,   644,   742,   675,   676,   677,
     813,   678,   760,   679,   680,   681,   698,   783,   878,   701,
     785,   879,   566,   233,   706,   707,   708,   234,    52,    53,
      54,    55,   343,   345,   350,   242,    56,   764,   464,   237,
     238,   341,   572,   576,   573,   577,   348,   349,   206,   290,
     398,   710,   711,   400,   401,   402,   224,   409,   410,   411,
     412,   413,   414,   313,   847,   295,   296,   297,   637,   531,
     298,   416,   207,   638,   314,   867,   863,   884,   885,   817,
     864,   865,   533,   772,   819,   534,   535,   886,   903,   775,
     776,   850,   888,   536,   537,   851,   538,   539,   540,   225,
     226,    58,   541,   824,   617,   874,   875,   876,   918,   877,
      67,   163,   490,   599,   548,   717,   833,   549,   550,   737,
     551,   627,   305,   425,   552,   553,   406,   208
};

static const short yypact[] =
{
     113,   155,-32768,-32768,  1362,  1362,   281,-32768,-32768,-32768,
  -32768,-32768,   104,   104,   104,    96,-32768,   100,-32768,-32768,
  -32768,-32768,-32768,    54,   245,  1127,  1094,  1132,  1602,   202,
    1193,   398,  1210,  1306,  1658,  1582,  1686,   554,  1260,   972,
    1367,-32768,-32768,   117,-32768,-32768,-32768,-32768,-32768,   104,
  -32768,-32768,    74,    76,    84,-32768,-32768,   134,-32768,  1362,
  -32768,-32768,-32768,   104,   104,   104,-32768,   157,-32768,   173,
    2564,-32768,   271,   104,    41,-32768,  1468,-32768,-32768,-32768,
     104,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,   104,
  -32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,   104,-32768,
  -32768,-32768,-32768,-32768,-32768,-32768,-32768,   104,-32768,-32768,
  -32768,-32768,-32768,-32768,-32768,-32768,   104,-32768,-32768,-32768,
  -32768,-32768,-32768,-32768,-32768,   104,-32768,-32768,-32768,-32768,
  -32768,-32768,-32768,-32768,   104,-32768,-32768,-32768,-32768,-32768,
  -32768,-32768,-32768,   104,-32768,-32768,-32768,-32768,-32768,   284,
     245,-32768,-32768,-32768,-32768,-32768,   164,-32768,   171,-32768,
     189,-32768,-32768,   207,   301,-32768,   255,-32768,-32768,-32768,
  -32768,-32768,  2646,  2646,   260,   310,   334,-32768,   342,   434,
  -32768,-32768,-32768,  2646,-32768,-32768,  1030,-32768,-32768,  2646,
     376,-32768,  2687,  2728,-32768,  3160,  1359,  2431,   607,  3041,
    3153,   540,   582,   709,   759,-32768,   318,  1764,  2646,   278,
     329,   285,   408,-32768,   245,   245,   104,-32768,   104,-32768,
  -32768,   104,   369,   379,-32768,-32768,   104,-32768,   271,   104,
      47,-32768,  1241,   470,   491,   180,  2092,   330,   994,-32768,
     336,-32768,   378,-32768,-32768,-32768,   361,   645,-32768,-32768,
    2646,  2439,  1716,  1808,-32768,-32768,   386,-32768,   473,   403,
  -32768,  2646,-32768,  1030,-32768,  1030,-32768,  2646,  2646,   399,
  -32768,-32768,  2646,  2646,  2646,  2646,  2646,  2646,  2646,  2646,
    2646,  2646,  2646,  2646,-32768,-32768,   434,   434,  2646,  2646,
     383,-32768,   405,   434,-32768,  1877,   431,-32768,   426,-32768,
     491,   206,   245,-32768,-32768,-32768,   271,   469,  2107,   433,
  -32768,   577,    57,-32768,  1691,   483,   284,   284,-32768,   104,
  -32768,   379,   104,   104,-32768,-32768,-32768,   379,   104,-32768,
  -32768,  2431,   607,  3041,  3153,   540,   582,   709,   759,-32768,
     507,   441,  1159,-32768,   104,-32768,-32768,   487,   443,-32768,
     378,-32768,-32768,-32768,-32768,   537,-32768,   474,-32768,  2916,
     458,  2935,   460,   452,   475,   480,-32768,-32768,  2375,  3160,
     482,   485,  3160,  3160,  2646,   534,  2646,  2646,  2532,   911,
    1247,   959,  1058,   698,   698,   567,   567,-32768,-32768,-32768,
  -32768,-32768,   495,   511,  3160,   308,   271,   104,-32768,-32768,
  -32768,-32768,   597,-32768,-32768,-32768,   222,   433,-32768,-32768,
      88,    90,    93,    94,   605,-32768,   516,  2259,-32768,-32768,
  -32768,-32768,-32768,-32768,   225,   914,  2646,  2646,  2167,-32768,
    2790,-32768,-32768,-32768,-32768,-32768,  2097,  3105,  1472,  1402,
    2205,  3122,  1728,  1512,   523,   531,-32768,   470,   298,   284,
  -32768,   587,-32768,-32768,-32768,   388,   114,-32768,-32768,   544,
  -32768,   546,  2646,   434,   548,   443,-32768,   645,   552,  2769,
    3054,-32768,  2646,-32768,  3054,-32768,-32768,-32768,-32768,   545,
     545,    82,  2646,  3189,  3123,-32768,  2646,-32768,   383,   383,
  -32768,   104,-32768,-32768,   434,-32768,-32768,-32768,-32768,-32768,
  -32768,  2334,-32768,-32768,-32768,-32768,-32768,-32768,-32768,   612,
     614,-32768,-32768,-32768,-32768,-32768,  2646,   616,   578,   580,
    2605,   144,   661,-32768,-32768,-32768,   240,-32768,-32768,-32768,
     584,    69,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,
  -32768,-32768,  2503,   586,-32768,-32768,-32768,-32768,-32768,   599,
     258,-32768,-32768,   592,  2818,  2839,-32768,-32768,    63,-32768,
     284,-32768,   245,  1985,-32768,-32768,   647,-32768,-32768,-32768,
    2646,   199,   598,-32768,  2646,   391,   620,-32768,   104,   104,
    3160,-32768,   104,   606,-32768,-32768,   609,   604,   613,  2962,
     622,   434,  1401,-32768,  3176,  3160,-32768,-32768,   626,   944,
  -32768,-32768,-32768,-32768,-32768,-32768,   632,   655,  2985,-32768,
  -32768,-32768,-32768,   320,  2646,   639,-32768,-32768,   684,-32768,
     284,   245,-32768,-32768,-32768,-32768,-32768,   136,   209,-32768,
  -32768,  1998,   730,-32768,-32768,-32768,   642,-32768,-32768,   365,
     368,-32768,-32768,  3160,-32768,    63,  1985,    63,  3045,  2646,
  -32768,   104,  3045,  2646,-32768,   104,-32768,-32768,-32768,   104,
  -32768,  2646,-32768,-32768,-32768,  2646,-32768,-32768,   178,-32768,
     434,  2646,-32768,   693,  3160,   654,   667,-32768,-32768,   186,
  -32768,  1918,  2646,-32768,-32768,   702,   704,-32768,  2503,  2646,
    2646,-32768,-32768,   373,-32768,   706,   104,   392,-32768,    83,
     430,-32768,   448,-32768,   271,   104,   104,   636,   641,   229,
  -32768,-32768,   104,   271,   104,   229,-32768,   104,-32768,  2097,
    3105,  2297,  3135,  1472,  1402,  2236,  1747,  2205,  3122,  2516,
    3140,  1728,  1512,  3100,  2325,-32768,-32768,-32768,-32768,-32768,
  -32768,-32768,  1401,-32768,-32768,-32768,-32768,  3045,   388,-32768,
    3045,   114,-32768,   640,  2888,   434,  2646,-32768,-32768,  1817,
    1401,-32768,-32768,  1620,-32768,  2026,-32768,-32768,-32768,  3176,
  -32768,-32768,-32768,   694,-32768,   700,-32768,   653,  3142,-32768,
     301,-32768,-32768,   379,-32768,   379,   136,   233,-32768,   104,
  -32768,   104,-32768,-32768,   104,   209,   209,-32768,-32768,   136,
     209,   701,-32768,-32768,-32768,-32768,-32768,-32768,-32768,   352,
    2646,-32768,   703,  2026,-32768,-32768,  2646,   708,   718,-32768,
  -32768,-32768,-32768,-32768,   720,   770,-32768,-32768,   636,   641,
     305,-32768,-32768,   944,   104,   229,-32768,   229,-32768,-32768,
  -32768,  2867,-32768,-32768,   694,-32768,-32768,-32768,   797,  2646,
     737,-32768,-32768,   120,-32768,-32768,-32768,-32768,-32768,-32768,
  -32768,-32768,-32768,-32768,   808,   813,-32768,-32768,-32768,-32768,
    2646,-32768,   745,   434,   791,   747,-32768,-32768,   642,   642,
      48,-32768,-32768,-32768,-32768,-32768,-32768,-32768,   751,-32768,
  -32768,   750,   120,   120,   785,-32768,-32768,-32768,-32768,-32768,
  -32768,-32768,-32768,   786,-32768,   301,   814,-32768,  2646,   773,
  -32768,-32768,-32768,   301,   685,   774,-32768,   799,   776,-32768,
  -32768,   783,-32768,  2646,   301,-32768,-32768,   687,-32768,-32768,
  -32768,   872,   873,-32768
};

static const short yypgoto[] =
{
  -32768,-32768,-32768,-32768,   191,   891,-32768,-32768,-32768,-32768,
  -32768,-32768,-32768,-32768,   -28,-32768,   -69,   425,   235,   603,
  -32768,-32768,-32768,  -127,  1076,-32768,-32768,-32768,-32768,-32768,
  -32768,-32768,-32768,-32768,  -278,   588,-32768,-32768,   -50,   236,
    -296,  -502,     0,     2,    24,    46,     4,     9,    31,   105,
    -302,  -295,   267,   274,  -292,  -284,   275,   276,  -350,  -348,
     594,   602,-32768,  -167,-32768,  -363,  -181,   794,   822,   471,
     903,-32768,  -462,  -140,   453,-32768,   621,-32768,    34,   457,
     -32,-32768,   444,-32768,   850,   279,-32768,  -636,-32768,   161,
  -32768,  -628,-32768,-32768,   257,   259,-32768,-32768,-32768,-32768,
  -32768,-32768,  -131,   481,   138,   153,    16,    18,-32768,-32768,
  -32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,   488,  -118,
  -32768,   615,-32768,-32768,   198,   196,   623,   492,  -100,-32768,
  -32768,  -594,  -263,  -467,  -428,-32768,   601,-32768,-32768,-32768,
  -32768,-32768,-32768,  -219,  -369,-32768,-32768,   658,  -434,-32768,
     450,-32768,-32768,  -380,   451,  -777,  -767,  -222,  -213,  -727,
  -32768,   -41,   102,  -732,  -631,-32768,-32768,-32768,-32768,  -741,
  -32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768,    95,
    -212,-32768,-32768,-32768,-32768,    91,-32768,    92,-32768,  -153,
     -17,   -66,   484,-32768,  -596,-32768,-32768,-32768,-32768,-32768,
  -32768,   437,  -285,-32768,-32768,-32768,-32768,    11
};


#define	YYLAST		3248


static const short yytable[] =
{
      69,   190,   165,   684,    25,    25,    26,    26,    29,    29,
     235,   246,   440,    30,    30,    59,    59,   152,   232,   441,
     322,   596,   442,   449,   156,   158,   160,   399,    27,    27,
     443,   152,   152,   152,   716,    31,    31,   532,   452,   240,
     624,   308,    76,   450,   818,   248,   249,   316,   152,   453,
      28,    28,   848,   768,   530,   307,   255,   152,  -114,    25,
     597,    26,   260,    29,   636,   511,   152,   528,    30,   529,
      59,   340,   209,   418,   210,   152,   415,   153,   154,   153,
     154,   299,   419,    27,   152,   880,   259,   153,   154,  -449,
      31,  -450,   433,   152,  -451,  -452,   211,    66,   639,    57,
      57,    68,   152,   293,   889,    28,   801,   215,   869,    32,
      32,   152,   220,    -1,   866,  -519,   902,    71,   212,   871,
      70,   532,   909,   440,   812,   426,  -519,   593,   217,   887,
     441,   245,   218,   442,   318,   897,    17,   815,   319,    71,
     703,   443,   603,   604,   605,   606,   607,   153,   154,   922,
    -114,   254,   363,   365,    57,    -2,   524,   294,   697,   574,
     712,   712,  -539,   370,    32,   371,  -539,   155,   236,   157,
    -519,    72,   911,   261,  -519,   340,  -429,   159,  -430,   530,
      73,  -431,  -432,   915,   152,   843,   774,   873,   496,   499,
     502,   505,   528,   704,   529,   507,    60,   497,   500,   503,
     506,   614,   705,   222,   151,   836,   838,     8,     9,    10,
      95,   743,    71,   745,   347,   488,    12,    13,    14,   354,
     395,   161,   209,   164,   210,   459,   598,   461,   209,   765,
     210,    17,   300,   301,    17,   883,    71,   857,   331,   166,
     332,   478,   335,   755,   649,   756,   211,   336,    71,   342,
     162,   670,   211,   671,   220,   304,   739,   239,   390,   391,
     315,    17,   333,   317,   241,   405,   713,   325,   212,   337,
     757,   218,   221,   222,   212,   714,   222,   149,   150,    11,
     916,   735,   243,   736,   334,   466,    77,    71,   227,  -254,
     704,   221,   222,    86,   244,   221,   222,   440,   422,   705,
     222,   255,    72,    17,   441,   481,   209,   442,   210,   493,
      17,    73,   245,   494,   436,   443,   437,    17,   438,   596,
     424,   247,   347,   439,   571,   530,   250,   619,   596,   727,
     211,   261,   712,   712,   300,   301,   728,    82,   528,   731,
     529,   228,   331,   338,   332,   630,   335,   732,   526,   631,
     229,   336,   212,   342,   322,   748,   451,   304,   597,   751,
     495,   498,   454,   504,   323,   222,   333,   597,    61,    62,
     588,   791,   222,   337,   590,   306,   251,    11,   460,   346,
     309,   153,   154,  -469,  -469,  -469,  -469,  -469,   334,   525,
     564,    71,   227,  -469,  -469,  -469,   209,   856,   210,   261,
     252,    17,   487,     8,     9,    10,   104,   692,   253,  -469,
     291,   261,    12,    13,    14,   299,    91,    25,   328,    26,
     211,    29,   640,    17,   344,   436,    30,   437,   542,   438,
      17,   489,   526,   570,   439,   347,   653,   153,   154,   354,
     396,    27,   212,   261,   -82,   228,   840,   338,    31,   397,
     222,   613,   740,   351,   229,   741,   319,   221,   222,   218,
     779,    49,    49,    28,   261,   293,   602,   261,   262,    63,
      64,    65,  -113,   525,   575,   427,   862,   220,   366,   782,
    -519,   700,    80,   319,    89,  -256,    98,   322,   107,   699,
     116,  -519,   125,   615,   134,   368,   143,   404,   895,   896,
      96,   101,   105,   110,   831,   826,   832,   827,   132,   137,
     141,   146,   899,   899,   221,   222,    49,   784,   421,   660,
     431,   218,    32,   786,   598,   600,   446,    49,   457,    49,
     462,   440,   795,   598,   463,  -519,   323,   222,   441,  -519,
     469,   442,    25,   474,    26,   693,    29,    10,    95,   443,
     471,    30,   473,   542,    12,    13,    14,   221,   222,     8,
       9,    10,   131,   667,   261,   367,    27,   475,    12,    13,
      14,   476,    17,    31,   479,   455,   456,   480,   309,   482,
     424,  -116,  -116,  -116,  -116,  -116,    17,   485,    28,    10,
     100,  -116,  -116,  -116,  -388,  -388,    12,    13,    14,   436,
     695,   437,   486,   438,   508,   650,   309,  -116,   439,   654,
     559,     7,   656,   657,    10,    82,   658,   571,   561,   773,
     777,    12,    13,    14,   281,   282,   283,   825,   467,   468,
     568,   719,   578,   720,   579,   723,   582,    16,   477,   702,
     724,  -262,   758,    49,   585,   709,   715,    32,   153,   154,
       8,     9,   352,   353,    80,   721,    89,   -31,    98,   -32,
     107,   609,   725,   491,   222,   610,    80,   611,    89,   616,
    -116,   622,    96,   101,   105,   110,   220,   722,   625,    49,
     560,   562,   746,   632,   633,    49,   749,   809,    25,   651,
      26,   629,    29,   752,   659,    49,   663,    30,   688,   542,
     661,   662,   789,   222,   209,   664,   210,   791,   222,    49,
      49,   655,    27,   209,   666,   210,    10,   104,   683,    31,
      49,   689,    49,    12,    13,    14,   694,   808,   211,   696,
     781,   486,   806,   738,    28,   524,   726,   211,   761,   787,
     788,    17,   762,   793,   261,   822,   417,   844,   796,   797,
     212,   798,   912,   279,   280,   281,   282,   283,   763,   212,
     919,   432,   152,    49,   620,   621,    10,   109,   770,   575,
     771,   928,   780,    12,    13,    14,   261,   920,   261,   929,
     773,   802,   626,   626,   804,   261,   872,   820,    80,   839,
      89,   842,    98,    32,   107,   264,   266,   447,   448,    49,
     845,   773,   829,   830,   300,   301,    96,   101,   105,   110,
     846,   894,   852,   300,   301,   853,   868,   835,   837,    78,
      83,    87,    92,   304,   870,   304,   881,   114,   119,   123,
     128,   882,   890,   436,   324,   437,   892,   438,   893,   914,
     900,   901,   439,   904,   905,   891,   917,    79,    84,    88,
      93,   908,   910,    49,   927,   115,   120,   124,   129,   913,
     897,   417,   417,   501,   417,   923,   921,   924,   858,   859,
     926,   860,   932,   933,    49,    81,    85,    90,    94,    99,
     103,   108,   112,   117,   121,   126,   130,   135,   139,   144,
     148,   403,     3,    80,   587,    98,   753,   116,   729,   134,
     434,   567,   565,   925,   569,   730,   733,   734,   444,    96,
     101,   584,   930,   132,   137,   543,   445,   544,     7,     8,
       9,    10,    11,   423,   814,   744,   545,    49,    12,    13,
      14,    49,    97,   102,   106,   111,   766,   834,   767,   828,
     133,   138,   142,   147,    16,   543,   803,   805,     7,     8,
       9,    10,    11,   583,   420,   581,   545,   458,    12,    13,
      14,   274,   275,   276,   277,   278,   279,   280,   281,   282,
     283,   799,   800,   465,    16,   601,   618,     8,     9,    10,
     140,   623,   898,   906,   628,   907,    12,    13,    14,     0,
       0,    78,    83,    87,    92,   329,     0,     0,     7,    49,
       0,    10,    11,   492,    17,     0,  -548,     0,    12,    13,
      14,   276,   277,   278,   279,   280,   281,   282,   283,    79,
      84,    88,    93,     0,    16,     0,    17,    18,     0,     0,
       0,   256,     0,   167,     7,     0,  -548,    10,    11,     0,
     168,   169,     0,   170,    12,    13,    14,     0,   324,   324,
       0,     0,     0,     0,   685,   686,   687,     0,     0,  -264,
      16,   171,    17,    18,     0,   172,   173,   174,   175,   176,
     177,   178,     0,     0,     0,     0,     0,   179,     0,     0,
     180,   330,  -380,     0,     0,   181,   182,   183,    49,   403,
     403,   184,   185,     0,     0,     0,   186,   358,     7,     8,
       9,    10,    82,     0,    97,   102,   106,   111,    12,    13,
      14,   277,   278,   279,   280,   281,   282,   283,     0,   187,
     188,     0,     0,   257,    16,    78,    83,    87,    92,     0,
       0,     7,     8,     9,    10,    77,     7,     8,     9,    10,
      86,    12,    13,    14,     0,    49,    12,    13,    14,     0,
       0,     0,     0,    79,    84,    88,    93,    16,     0,    17,
     329,    49,    16,     7,    17,     0,    10,    11,     0,   794,
      49,     0,     0,    12,    13,    14,    80,     0,    89,     0,
      98,  -251,   107,     0,   116,     0,   125,     0,   134,    16,
     143,    17,    18,     0,    96,   101,   105,   110,     8,     9,
      10,   100,   132,   137,   141,   146,     0,    12,    13,    14,
       0,     0,     0,     0,  -250,     8,     9,    10,   109,  -252,
       0,   816,   816,     0,    12,    13,    14,   821,   403,   403,
      78,    83,     0,     0,   114,   119,     0,     0,    97,   102,
     106,   111,   320,     0,     0,   -19,   -19,   -19,   -19,   -19,
       0,     0,     0,     0,     0,   -19,   -19,   -19,    79,    84,
       0,     0,   115,   120,     0,     8,     9,    10,   136,     0,
     220,   -19,   849,  -519,    12,    13,    14,   854,   855,     0,
    -255,     0,     0,     0,  -519,     0,    81,    85,    99,   103,
     117,   121,   135,   139,     0,     0,     0,  -257,   275,   276,
     277,   278,   279,   280,   281,   282,   283,     0,   790,   792,
       7,     8,     9,    10,   113,     0,     0,   358,   501,     0,
      12,    13,    14,     0,     0,     0,   359,   361,  -519,     0,
       0,   501,  -519,     0,   -19,     0,    16,   369,    17,     0,
       0,    97,   102,   372,   373,   133,   138,  -263,   378,   379,
     380,   381,   382,   383,   384,   385,   386,   387,   388,   389,
     501,   816,     0,     6,   394,  -125,     7,     8,     9,    10,
      11,     0,     8,     9,    10,   145,    12,    13,    14,     0,
       0,    12,    13,    14,   430,     0,     0,   403,   403,     0,
       0,    15,    16,  -258,    17,    18,   403,   403,     0,     0,
     403,   403,   669,     0,   509,   154,     0,     8,     9,    10,
     100,   168,   169,     0,   170,     0,    12,    13,    14,  -125,
     284,   285,     0,   286,   287,   288,   289,     0,  -125,   790,
     792,   792,   171,     0,    18,     0,   172,   173,   174,   175,
     176,   177,   178,     0,     0,     0,     0,     0,   179,    19,
       0,   180,   483,   484,  -265,     0,   181,   182,   183,     0,
       0,     0,   184,   185,     0,     0,   670,   186,   671,   219,
       0,     0,   -27,   -27,   -27,   -27,   -27,     8,     9,    10,
      95,     0,   -27,   -27,   -27,     0,    12,    13,    14,  -310,
     187,   188,     0,     0,   672,     0,     0,   220,   -27,     0,
    -519,     0,   554,   555,    17,     0,     0,     0,     0,     0,
       0,  -519,     0,    78,    83,    87,    92,     8,     9,    10,
     136,   114,   119,   123,   128,     0,    12,    13,    14,     0,
       0,     0,     0,     0,   221,   222,     0,     0,   580,     0,
       0,    79,    84,    88,    93,   394,     0,     0,   589,   115,
     120,   124,   129,     0,     0,  -519,     0,     0,   594,  -519,
       0,   -27,   595,     0,     0,     0,     0,     0,     0,    81,
      85,    90,    94,    99,   103,   108,   112,   117,   121,   126,
     130,   135,   139,   144,   148,     0,     7,     8,     9,    10,
     122,     0,   608,     0,     0,     0,    12,    13,    14,     0,
       0,     0,     0,     0,     0,     0,     7,     8,     9,    10,
      91,     0,    16,     0,    17,     0,    12,    13,    14,     0,
       0,   669,     0,   509,   154,     0,    97,   102,   106,   111,
     168,   169,    16,   170,   133,   138,   142,   147,     0,   643,
       0,     0,     0,     0,     0,     0,   648,     0,     0,     0,
     652,   171,     0,    18,     0,   172,   173,   174,   175,   176,
     177,   178,     7,     8,     9,    10,   118,   179,   674,  -260,
     180,     0,    12,    13,    14,   181,   182,   183,     0,     0,
       0,   184,   185,     0,     0,   670,   186,   671,    16,  -253,
       7,     8,     9,    10,   127,     7,     8,     9,    10,    11,
      12,    13,    14,     0,     0,    12,    13,    14,  -377,   187,
     188,     0,     0,   672,     0,     0,    16,   362,     0,     0,
       7,    16,   643,    10,    11,   747,     0,     0,     0,   750,
      12,    13,    14,     8,     9,    10,   131,   394,     0,     0,
       0,   754,    12,    13,    14,  -259,    16,   759,    17,     0,
       0,     0,     8,     9,    10,   109,     0,   674,   769,     0,
      17,    12,    13,    14,     0,   292,   778,  -455,  -455,  -455,
    -455,  -455,  -455,  -261,  -455,  -455,     0,  -455,  -455,  -455,
    -455,  -455,     0,  -455,  -455,  -455,  -455,  -455,  -455,  -455,
    -455,  -455,  -455,  -455,  -455,  -455,  -455,  -455,   293,  -455,
    -455,  -455,  -455,  -455,  -455,  -455,     0,     0,     0,   364,
       0,  -455,     7,     0,  -455,    10,    11,     0,   674,  -455,
    -455,  -455,    12,    13,    14,  -455,  -455,     0,     0,   810,
    -455,     0,     0,     0,     0,     0,   674,     0,    16,   674,
      17,   674,     0,     0,     0,     0,     0,     0,     0,     0,
       0,  -455,   294,  -455,  -455,     0,     0,  -455,     0,   267,
     268,   269,     0,   270,   271,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   281,   282,   283,     0,   407,     0,
    -469,  -469,  -469,  -469,  -469,  -469,   841,  -469,  -469,   674,
    -469,  -469,  -469,  -469,  -469,     0,  -469,  -469,  -469,  -469,
    -469,  -469,  -469,  -469,  -469,  -469,  -469,  -469,  -469,  -469,
    -469,   811,  -469,  -469,  -469,  -469,  -469,  -469,  -469,   669,
       0,   167,     0,     0,  -469,     0,     0,  -469,   168,   169,
       0,   170,  -469,  -469,  -469,     0,     0,     0,  -469,  -469,
       0,     0,     0,  -469,     0,     0,     0,     0,     0,   171,
       0,    18,     0,   172,   173,   174,   175,   176,   177,   178,
       0,  -326,     0,     0,  -469,   179,  -469,  -469,   180,     0,
    -469,     0,     0,   181,   182,   183,     0,     0,     0,   184,
     185,     0,     0,  -326,   186,  -326,   641,     0,   167,     0,
       0,     0,     0,     0,     0,   168,   169,     0,   170,     0,
       0,     0,     7,     8,     9,    10,    11,   187,   188,     0,
     718,   672,    12,    13,    14,     0,   171,     0,    18,     0,
     172,   173,   174,   175,   176,   177,   178,   669,    16,   167,
      17,     0,   179,     0,     0,   180,   168,   169,     0,   170,
     181,   182,   183,     0,     0,     0,   184,   185,     0,     0,
       0,   186,     0,     0,     0,     0,     0,   171,     0,    18,
       0,   172,   173,   174,   175,   176,   177,   178,     0,     0,
       0,     0,     0,   179,   187,   188,   180,     0,   642,     0,
       0,   181,   182,   183,     0,     0,     0,   184,   185,     0,
       0,     0,   186,   326,     0,     0,   -23,   -23,   -23,   -23,
     -23,     7,     8,     9,    10,    77,   -23,   -23,   -23,     0,
     167,    12,    13,    14,     0,   187,   188,   168,   169,   672,
     170,   220,   -23,     0,  -519,     0,     0,    16,     0,    17,
       0,     0,     0,     0,     0,  -519,     0,     0,   171,     0,
      18,     0,   172,   173,   174,   175,   176,   177,   178,     0,
       0,     0,     0,     0,   179,     0,     0,   180,   221,   222,
       0,     0,   181,   182,   428,     0,     0,     0,   184,   185,
     167,     0,     0,   186,     0,     0,     0,   168,   169,  -519,
     170,     0,     0,  -519,     0,   -23,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   187,   188,   171,     0,
      18,   429,   172,   173,   174,   175,   176,   177,   178,     7,
       8,     9,    10,   113,   179,     0,     0,   180,     0,    12,
      13,    14,   181,   182,   183,     0,     0,     0,   184,   185,
       0,     0,     0,   186,     0,    16,     0,    17,     0,     0,
       0,     8,     9,    10,   104,     0,     0,     0,     0,     0,
      12,    13,    14,     0,     0,     0,   187,   188,     0,     0,
       0,   556,   509,   510,     8,     9,    10,    11,    17,   168,
     169,     0,   170,    12,    13,    14,   511,     0,   512,   513,
     514,   515,   516,   517,   518,   519,   520,   521,   522,    16,
     171,    17,    18,     0,   172,   173,   174,   175,   176,   177,
     178,     7,     8,     9,    10,    86,   179,     0,     0,   180,
       0,    12,    13,    14,   181,   182,   183,     0,     0,     0,
     184,   185,     0,     0,     0,   186,     0,    16,     0,    17,
       8,     9,    10,   145,     0,     0,     0,   509,   154,    12,
      13,    14,     0,     0,   168,   169,   523,   170,   187,   188,
       0,   511,   524,   512,   513,   514,   515,   516,   517,   518,
     519,   520,   521,   522,     0,   171,     0,    18,     0,   172,
     173,   174,   175,   176,   177,   178,     0,     0,   167,     0,
       0,   179,     0,     0,   180,   168,   169,     0,   170,   181,
     182,   183,     0,     0,     0,   184,   185,     0,     0,     0,
     186,     0,     0,     0,     0,     0,   171,     0,    18,     0,
     172,   173,   174,   175,   176,   177,   178,     0,     0,     0,
       0,   523,   179,   187,   188,   180,     0,   524,     0,     0,
     181,   182,   183,     0,     0,     7,   184,   185,    10,    77,
     360,   186,   167,     0,     0,    12,    13,    14,     0,   168,
     169,     0,   170,     0,     0,     0,     0,     0,     0,     0,
       0,    16,     0,    17,   187,   188,     0,     0,   477,     0,
     171,     0,    18,     0,   172,   173,   174,   175,   176,   177,
     178,     0,     0,     0,     0,     0,   179,     0,     0,   180,
       0,     0,     0,     0,   181,   182,   183,     0,     0,     0,
     184,   185,     0,     0,     0,   186,   167,     7,     8,     9,
      10,    11,     0,   168,   169,     0,   170,    12,    13,    14,
       7,     8,     9,    10,   122,     0,     0,     0,   187,   188,
      12,    13,    14,    16,   171,    17,    18,     0,   172,   173,
     174,   175,   176,   177,   178,     0,    16,     0,    17,     0,
     179,     0,     0,   180,     0,     0,     0,     0,   181,   182,
     183,     0,     0,     0,   184,   185,     0,   167,     7,   186,
       0,    10,    11,     0,   168,   169,     0,   170,    12,    13,
      14,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   187,   188,    16,   171,    17,    18,     0,   172,
     173,   174,   175,   176,   177,   178,     0,     0,   167,     0,
       0,   179,     0,     0,   180,   168,   169,     0,   170,   181,
     182,   183,     0,     0,     0,   184,   185,     0,     0,     0,
     186,     0,     0,     0,     0,     0,   171,     0,    18,     0,
     172,   173,   174,   175,   176,   177,   178,     0,     0,   167,
       0,     0,   179,   187,   188,   180,   168,   169,     0,   170,
     181,   182,   183,     0,     0,     0,   184,   185,     0,     0,
       0,   186,     0,     0,     0,     0,     0,   171,     0,    18,
       0,   172,   173,   174,   175,   176,   177,   178,     0,     0,
     167,     0,   612,   179,   187,   188,   180,   168,   169,     0,
     170,   181,   182,   183,     0,     0,     0,   184,   185,     0,
       0,     0,   186,     0,     0,     0,     0,     0,   171,     0,
      18,     0,   172,   173,   174,   175,   176,   177,   178,     0,
       0,   167,     0,     0,   179,   187,   188,   180,   168,   169,
       0,   170,   181,   182,   183,     0,     0,     0,   184,   185,
       0,     0,     0,   263,     0,     0,     0,     0,     0,   171,
       0,    18,     0,   172,   173,   174,   175,   176,   177,   178,
       0,     0,   586,     0,     0,   179,   187,   188,   180,   168,
     169,     0,   170,   181,   182,   183,     0,     0,     0,   184,
     185,     0,     0,     0,   265,     0,     0,     0,     0,     0,
     171,     0,    18,     0,   172,   173,   174,   175,   176,   177,
     178,     0,     0,     0,     0,     0,   179,   187,   188,   180,
       0,     0,     0,     0,   181,   182,   183,     0,     0,     0,
     184,   185,   267,   268,   269,   186,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
       0,     0,     0,     0,     0,     0,     0,     0,   187,   188,
     267,   268,   269,     0,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,     0,     0,
       0,   267,   268,   269,   557,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   267,
     268,   269,   634,   270,   271,   272,   273,   274,   275,   276,
     277,   278,   279,   280,   281,   282,   283,     0,     0,     0,
     267,   268,   269,   635,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   282,   283,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   267,   268,
     269,   861,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,     0,   267,   268,   269,
     807,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,     0,     0,   690,     0,     0,
       0,     0,     0,     0,   267,   268,   269,   470,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,     0,     0,     0,     0,   472,   267,   268,   269,
     691,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,     7,     0,     0,    10,    86,
       0,     0,     0,   665,     0,    12,    13,    14,     7,     0,
       0,    10,    11,     0,     0,     0,     0,     0,    12,    13,
      14,    16,     0,    17,     0,     0,     0,    17,     0,     0,
       0,     0,     0,     0,    16,     0,    17,   267,   268,   269,
       0,   270,   271,   272,   273,   274,   275,   276,   277,   278,
     279,   280,   281,   282,   283,     8,     9,    10,   140,     7,
       8,     9,    10,    82,    12,    13,    14,     0,     0,    12,
      13,    14,     0,     0,     0,     0,     7,     8,     9,    10,
     118,     0,    17,     0,     0,    16,    12,    13,    14,     7,
       8,     9,    10,    91,     7,     8,     9,    10,   127,    12,
      13,    14,    16,     0,    12,    13,    14,     7,     0,     0,
      10,    91,     0,     0,     0,    16,     0,    12,    13,    14,
      16,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,    16,   267,   268,   269,   823,   270,   271,
     272,   273,   274,   275,   276,   277,   278,   279,   280,   281,
     282,   283,   267,   268,   269,     0,   270,   271,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     269,     0,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   282,   283,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283
};

static const short yycheck[] =
{
      17,    70,    68,   599,     4,     5,     4,     5,     4,     5,
     150,   164,   314,     4,     5,     4,     5,    49,   149,   314,
     232,   488,   314,   319,    52,    53,    54,   290,     4,     5,
     314,    63,    64,    65,   628,     4,     5,   417,   323,   157,
     542,   222,    24,   321,   771,   172,   173,   228,    80,   327,
       4,     5,   819,   681,   417,   222,   183,    89,     1,    59,
     488,    59,   189,    59,     1,    17,    98,   417,    59,   417,
      59,   238,    72,   295,    72,   107,   295,     3,     4,     3,
       4,   208,   295,    59,   116,   862,   186,     3,     4,     1,
      59,     1,   311,   125,     1,     1,    72,     1,   560,     4,
       5,     1,   134,    34,   871,    59,   742,    73,   849,     4,
       5,   143,    29,     0,   846,    32,   883,     3,    72,   851,
      66,   501,   899,   425,   760,   306,    43,    45,    87,   870,
     425,    11,    91,   425,    87,    87,    32,   765,    91,     3,
       4,   425,   511,   512,   513,   514,   515,     3,     4,   916,
      93,   179,   252,   253,    59,     0,    93,    88,   620,    45,
     627,   628,    66,   263,    59,   265,    66,    93,   150,    93,
      87,    57,   904,    91,    91,   342,    88,    93,    88,   542,
      66,    88,    88,   910,   216,   813,   688,    67,   410,   411,
     412,   413,   542,    57,   542,   414,     5,   410,   411,   412,
     413,    57,    66,    67,    87,   799,   800,     5,     6,     7,
       8,   645,     3,   647,   242,   396,    14,    15,    16,   247,
     289,    87,   222,    66,   222,   343,   489,   345,   228,    43,
     228,    32,   214,   215,    32,   866,     3,   833,   238,    66,
     238,   368,   238,    65,    45,    67,   222,   238,     3,   238,
      59,    65,   228,    67,    29,   221,   636,    93,   286,   287,
     226,    32,   238,   229,    93,   293,    57,    87,   222,   238,
      92,    91,    66,    67,   228,    66,    67,    41,    42,     8,
     911,   631,    93,   631,   238,   351,     8,     3,     4,    87,
      57,    66,    67,     8,    87,    66,    67,   599,    92,    66,
      67,   428,    57,    32,   599,   374,   306,   599,   306,    87,
      32,    66,    11,    91,   314,   599,   314,    32,   314,   786,
     302,    66,   350,   314,   455,   688,    66,    87,   795,   631,
     306,    91,   799,   800,   316,   317,   631,     8,   688,   631,
     688,    57,   342,   238,   342,    87,   342,   631,   417,    91,
      66,   342,   306,   342,   566,   651,   322,   323,   786,   655,
     410,   411,   328,   413,    66,    67,   342,   795,    87,    88,
     470,    66,    67,   342,   474,     6,    66,     8,   344,     1,
       1,     3,     4,     4,     5,     6,     7,     8,   342,   417,
      92,     3,     4,    14,    15,    16,   396,    92,   396,    91,
      66,    32,    94,     5,     6,     7,     8,    87,    66,    30,
      92,    91,    14,    15,    16,   542,     8,   417,    88,   417,
     396,   417,   562,    32,    88,   425,   417,   425,   417,   425,
      32,   397,   501,    45,   425,   463,    45,     3,     4,   467,
      57,   417,   396,    91,    45,    57,    94,   342,   417,    66,
      67,   520,    87,    92,    66,    87,    91,    66,    67,    91,
      87,     4,     5,   417,    91,    34,   494,    91,    92,    12,
      13,    14,    93,   501,   456,     6,   845,    29,    92,    87,
      32,   621,    25,    91,    27,    87,    29,   699,    31,   620,
      33,    43,    35,   521,    37,    92,    39,    92,   878,   879,
      29,    30,    31,    32,   789,   783,   791,   785,    37,    38,
      39,    40,   881,   882,    66,    67,    59,    87,    92,   585,
      87,    91,   417,   704,   787,   491,    43,    70,    87,    72,
      43,   833,   713,   796,    91,    87,    66,    67,   833,    91,
      66,   833,   542,    91,   542,   614,   542,     7,     8,   833,
      92,   542,    92,   542,    14,    15,    16,    66,    67,     5,
       6,     7,     8,   591,    91,    92,   542,    92,    14,    15,
      16,    91,    32,   542,    92,   339,   340,    92,     1,    45,
     562,     4,     5,     6,     7,     8,    32,    92,   542,     7,
       8,    14,    15,    16,    87,    88,    14,    15,    16,   599,
     617,   599,    91,   599,    88,   571,     1,    30,   599,   575,
      87,     4,   578,   579,     7,     8,   582,   748,    87,   688,
     689,    14,    15,    16,    57,    58,    59,   780,    91,    92,
      43,   631,    88,   631,    88,   631,    88,    30,    93,   621,
     631,    87,   670,   186,    92,   627,   628,   542,     3,     4,
       5,     6,     7,     8,   197,   631,   199,    45,   201,    45,
     203,    45,   631,    66,    67,    87,   209,    87,   211,     8,
      93,    87,   201,   202,   203,   204,    29,   631,    92,   222,
     444,   445,   648,    91,    92,   228,   652,   756,   688,    91,
     688,    92,   688,   659,    88,   238,    92,   688,    66,   688,
      91,    92,    66,    67,   704,    92,   704,    66,    67,   252,
     253,    91,   688,   713,    92,   713,     7,     8,    92,   688,
     263,    66,   265,    14,    15,    16,    87,   755,   704,    45,
     696,    91,    92,     3,   688,    93,   631,   713,    45,   705,
     706,    32,    88,   709,    91,    92,   295,   816,   714,   715,
     704,   717,   905,    55,    56,    57,    58,    59,    91,   713,
     913,   310,   794,   306,   528,   529,     7,     8,    66,   751,
      66,   924,    66,    14,    15,    16,    91,    92,    91,    92,
     849,   747,   546,   547,   750,    91,   852,    87,   331,    88,
     333,    88,   335,   688,   337,   192,   193,   316,   317,   342,
      92,   870,   786,   787,   786,   787,   335,   336,   337,   338,
      92,   877,    92,   795,   796,    45,    19,   799,   800,    25,
      26,    27,    28,   789,    87,   791,    18,    33,    34,    35,
      36,    18,    87,   833,   233,   833,    45,   833,    91,   908,
     881,   882,   833,    92,    94,   873,   912,    25,    26,    27,
      28,    66,    66,   396,   923,    33,    34,    35,    36,    45,
      87,   410,   411,   412,   413,    66,    92,    91,   834,   835,
      87,   837,     0,     0,   417,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,   290,     1,   436,   469,   438,   661,   440,   631,   442,
     312,   450,   449,   920,   453,   631,   631,   631,   314,   438,
     439,   467,   929,   442,   443,     1,   314,     3,     4,     5,
       6,     7,     8,   302,   763,   646,    12,   470,    14,    15,
      16,   474,    29,    30,    31,    32,   679,   799,   679,   786,
      37,    38,    39,    40,    30,     1,   748,   751,     4,     5,
       6,     7,     8,   465,   296,   463,    12,   342,    14,    15,
      16,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,   735,   736,   350,    30,   491,   525,     5,     6,     7,
       8,   531,   880,   892,   547,   893,    14,    15,    16,    -1,
      -1,   197,   198,   199,   200,     1,    -1,    -1,     4,   542,
      -1,     7,     8,   402,    32,    -1,    92,    -1,    14,    15,
      16,    52,    53,    54,    55,    56,    57,    58,    59,   197,
     198,   199,   200,    -1,    30,    -1,    32,    33,    -1,    -1,
      -1,     1,    -1,     3,     4,    -1,    92,     7,     8,    -1,
      10,    11,    -1,    13,    14,    15,    16,    -1,   447,   448,
      -1,    -1,    -1,    -1,   603,   604,   605,    -1,    -1,    87,
      30,    31,    32,    33,    -1,    35,    36,    37,    38,    39,
      40,    41,    -1,    -1,    -1,    -1,    -1,    47,    -1,    -1,
      50,    87,    88,    -1,    -1,    55,    56,    57,   631,   488,
     489,    61,    62,    -1,    -1,    -1,    66,   247,     4,     5,
       6,     7,     8,    -1,   201,   202,   203,   204,    14,    15,
      16,    53,    54,    55,    56,    57,    58,    59,    -1,    89,
      90,    -1,    -1,    93,    30,   331,   332,   333,   334,    -1,
      -1,     4,     5,     6,     7,     8,     4,     5,     6,     7,
       8,    14,    15,    16,    -1,   688,    14,    15,    16,    -1,
      -1,    -1,    -1,   331,   332,   333,   334,    30,    -1,    32,
       1,   704,    30,     4,    32,    -1,     7,     8,    -1,   712,
     713,    -1,    -1,    14,    15,    16,   719,    -1,   721,    -1,
     723,    87,   725,    -1,   727,    -1,   729,    -1,   731,    30,
     733,    32,    33,    -1,   723,   724,   725,   726,     5,     6,
       7,     8,   731,   732,   733,   734,    -1,    14,    15,    16,
      -1,    -1,    -1,    -1,    87,     5,     6,     7,     8,    87,
      -1,   770,   771,    -1,    14,    15,    16,   776,   627,   628,
     436,   437,    -1,    -1,   440,   441,    -1,    -1,   335,   336,
     337,   338,     1,    -1,    -1,     4,     5,     6,     7,     8,
      -1,    -1,    -1,    -1,    -1,    14,    15,    16,   436,   437,
      -1,    -1,   440,   441,    -1,     5,     6,     7,     8,    -1,
      29,    30,   821,    32,    14,    15,    16,   826,   827,    -1,
      87,    -1,    -1,    -1,    43,    -1,   436,   437,   438,   439,
     440,   441,   442,   443,    -1,    -1,    -1,    87,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    -1,   707,   708,
       4,     5,     6,     7,     8,    -1,    -1,   467,   867,    -1,
      14,    15,    16,    -1,    -1,    -1,   250,   251,    87,    -1,
      -1,   880,    91,    -1,    93,    -1,    30,   261,    32,    -1,
      -1,   438,   439,   267,   268,   442,   443,    87,   272,   273,
     274,   275,   276,   277,   278,   279,   280,   281,   282,   283,
     909,   910,    -1,     1,   288,     3,     4,     5,     6,     7,
       8,    -1,     5,     6,     7,     8,    14,    15,    16,    -1,
      -1,    14,    15,    16,   308,    -1,    -1,   786,   787,    -1,
      -1,    29,    30,    87,    32,    33,   795,   796,    -1,    -1,
     799,   800,     1,    -1,     3,     4,    -1,     5,     6,     7,
       8,    10,    11,    -1,    13,    -1,    14,    15,    16,    57,
      61,    62,    -1,    64,    65,    66,    67,    -1,    66,   828,
     829,   830,    31,    -1,    33,    -1,    35,    36,    37,    38,
      39,    40,    41,    -1,    -1,    -1,    -1,    -1,    47,    87,
      -1,    50,   376,   377,    87,    -1,    55,    56,    57,    -1,
      -1,    -1,    61,    62,    -1,    -1,    65,    66,    67,     1,
      -1,    -1,     4,     5,     6,     7,     8,     5,     6,     7,
       8,    -1,    14,    15,    16,    -1,    14,    15,    16,    88,
      89,    90,    -1,    -1,    93,    -1,    -1,    29,    30,    -1,
      32,    -1,   426,   427,    32,    -1,    -1,    -1,    -1,    -1,
      -1,    43,    -1,   719,   720,   721,   722,     5,     6,     7,
       8,   727,   728,   729,   730,    -1,    14,    15,    16,    -1,
      -1,    -1,    -1,    -1,    66,    67,    -1,    -1,   462,    -1,
      -1,   719,   720,   721,   722,   469,    -1,    -1,   472,   727,
     728,   729,   730,    -1,    -1,    87,    -1,    -1,   482,    91,
      -1,    93,   486,    -1,    -1,    -1,    -1,    -1,    -1,   719,
     720,   721,   722,   723,   724,   725,   726,   727,   728,   729,
     730,   731,   732,   733,   734,    -1,     4,     5,     6,     7,
       8,    -1,   516,    -1,    -1,    -1,    14,    15,    16,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     4,     5,     6,     7,
       8,    -1,    30,    -1,    32,    -1,    14,    15,    16,    -1,
      -1,     1,    -1,     3,     4,    -1,   723,   724,   725,   726,
      10,    11,    30,    13,   731,   732,   733,   734,    -1,   563,
      -1,    -1,    -1,    -1,    -1,    -1,   570,    -1,    -1,    -1,
     574,    31,    -1,    33,    -1,    35,    36,    37,    38,    39,
      40,    41,     4,     5,     6,     7,     8,    47,   592,    87,
      50,    -1,    14,    15,    16,    55,    56,    57,    -1,    -1,
      -1,    61,    62,    -1,    -1,    65,    66,    67,    30,    87,
       4,     5,     6,     7,     8,     4,     5,     6,     7,     8,
      14,    15,    16,    -1,    -1,    14,    15,    16,    88,    89,
      90,    -1,    -1,    93,    -1,    -1,    30,     1,    -1,    -1,
       4,    30,   646,     7,     8,   649,    -1,    -1,    -1,   653,
      14,    15,    16,     5,     6,     7,     8,   661,    -1,    -1,
      -1,   665,    14,    15,    16,    87,    30,   671,    32,    -1,
      -1,    -1,     5,     6,     7,     8,    -1,   681,   682,    -1,
      32,    14,    15,    16,    -1,     1,   690,     3,     4,     5,
       6,     7,     8,    87,    10,    11,    -1,    13,    14,    15,
      16,    17,    -1,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    -1,    -1,    -1,     1,
      -1,    47,     4,    -1,    50,     7,     8,    -1,   742,    55,
      56,    57,    14,    15,    16,    61,    62,    -1,    -1,    12,
      66,    -1,    -1,    -1,    -1,    -1,   760,    -1,    30,   763,
      32,   765,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    87,    88,    89,    90,    -1,    -1,    93,    -1,    42,
      43,    44,    -1,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    -1,     1,    -1,
       3,     4,     5,     6,     7,     8,   810,    10,    11,   813,
      13,    14,    15,    16,    17,    -1,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    94,    35,    36,    37,    38,    39,    40,    41,     1,
      -1,     3,    -1,    -1,    47,    -1,    -1,    50,    10,    11,
      -1,    13,    55,    56,    57,    -1,    -1,    -1,    61,    62,
      -1,    -1,    -1,    66,    -1,    -1,    -1,    -1,    -1,    31,
      -1,    33,    -1,    35,    36,    37,    38,    39,    40,    41,
      -1,    43,    -1,    -1,    87,    47,    89,    90,    50,    -1,
      93,    -1,    -1,    55,    56,    57,    -1,    -1,    -1,    61,
      62,    -1,    -1,    65,    66,    67,     1,    -1,     3,    -1,
      -1,    -1,    -1,    -1,    -1,    10,    11,    -1,    13,    -1,
      -1,    -1,     4,     5,     6,     7,     8,    89,    90,    -1,
      12,    93,    14,    15,    16,    -1,    31,    -1,    33,    -1,
      35,    36,    37,    38,    39,    40,    41,     1,    30,     3,
      32,    -1,    47,    -1,    -1,    50,    10,    11,    -1,    13,
      55,    56,    57,    -1,    -1,    -1,    61,    62,    -1,    -1,
      -1,    66,    -1,    -1,    -1,    -1,    -1,    31,    -1,    33,
      -1,    35,    36,    37,    38,    39,    40,    41,    -1,    -1,
      -1,    -1,    -1,    47,    89,    90,    50,    -1,    93,    -1,
      -1,    55,    56,    57,    -1,    -1,    -1,    61,    62,    -1,
      -1,    -1,    66,     1,    -1,    -1,     4,     5,     6,     7,
       8,     4,     5,     6,     7,     8,    14,    15,    16,    -1,
       3,    14,    15,    16,    -1,    89,    90,    10,    11,    93,
      13,    29,    30,    -1,    32,    -1,    -1,    30,    -1,    32,
      -1,    -1,    -1,    -1,    -1,    43,    -1,    -1,    31,    -1,
      33,    -1,    35,    36,    37,    38,    39,    40,    41,    -1,
      -1,    -1,    -1,    -1,    47,    -1,    -1,    50,    66,    67,
      -1,    -1,    55,    56,    57,    -1,    -1,    -1,    61,    62,
       3,    -1,    -1,    66,    -1,    -1,    -1,    10,    11,    87,
      13,    -1,    -1,    91,    -1,    93,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    89,    90,    31,    -1,
      33,    94,    35,    36,    37,    38,    39,    40,    41,     4,
       5,     6,     7,     8,    47,    -1,    -1,    50,    -1,    14,
      15,    16,    55,    56,    57,    -1,    -1,    -1,    61,    62,
      -1,    -1,    -1,    66,    -1,    30,    -1,    32,    -1,    -1,
      -1,     5,     6,     7,     8,    -1,    -1,    -1,    -1,    -1,
      14,    15,    16,    -1,    -1,    -1,    89,    90,    -1,    -1,
      -1,    94,     3,     4,     5,     6,     7,     8,    32,    10,
      11,    -1,    13,    14,    15,    16,    17,    -1,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    -1,    35,    36,    37,    38,    39,    40,
      41,     4,     5,     6,     7,     8,    47,    -1,    -1,    50,
      -1,    14,    15,    16,    55,    56,    57,    -1,    -1,    -1,
      61,    62,    -1,    -1,    -1,    66,    -1,    30,    -1,    32,
       5,     6,     7,     8,    -1,    -1,    -1,     3,     4,    14,
      15,    16,    -1,    -1,    10,    11,    87,    13,    89,    90,
      -1,    17,    93,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    -1,    31,    -1,    33,    -1,    35,
      36,    37,    38,    39,    40,    41,    -1,    -1,     3,    -1,
      -1,    47,    -1,    -1,    50,    10,    11,    -1,    13,    55,
      56,    57,    -1,    -1,    -1,    61,    62,    -1,    -1,    -1,
      66,    -1,    -1,    -1,    -1,    -1,    31,    -1,    33,    -1,
      35,    36,    37,    38,    39,    40,    41,    -1,    -1,    -1,
      -1,    87,    47,    89,    90,    50,    -1,    93,    -1,    -1,
      55,    56,    57,    -1,    -1,     4,    61,    62,     7,     8,
       1,    66,     3,    -1,    -1,    14,    15,    16,    -1,    10,
      11,    -1,    13,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    30,    -1,    32,    89,    90,    -1,    -1,    93,    -1,
      31,    -1,    33,    -1,    35,    36,    37,    38,    39,    40,
      41,    -1,    -1,    -1,    -1,    -1,    47,    -1,    -1,    50,
      -1,    -1,    -1,    -1,    55,    56,    57,    -1,    -1,    -1,
      61,    62,    -1,    -1,    -1,    66,     3,     4,     5,     6,
       7,     8,    -1,    10,    11,    -1,    13,    14,    15,    16,
       4,     5,     6,     7,     8,    -1,    -1,    -1,    89,    90,
      14,    15,    16,    30,    31,    32,    33,    -1,    35,    36,
      37,    38,    39,    40,    41,    -1,    30,    -1,    32,    -1,
      47,    -1,    -1,    50,    -1,    -1,    -1,    -1,    55,    56,
      57,    -1,    -1,    -1,    61,    62,    -1,     3,     4,    66,
      -1,     7,     8,    -1,    10,    11,    -1,    13,    14,    15,
      16,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    89,    90,    30,    31,    32,    33,    -1,    35,
      36,    37,    38,    39,    40,    41,    -1,    -1,     3,    -1,
      -1,    47,    -1,    -1,    50,    10,    11,    -1,    13,    55,
      56,    57,    -1,    -1,    -1,    61,    62,    -1,    -1,    -1,
      66,    -1,    -1,    -1,    -1,    -1,    31,    -1,    33,    -1,
      35,    36,    37,    38,    39,    40,    41,    -1,    -1,     3,
      -1,    -1,    47,    89,    90,    50,    10,    11,    -1,    13,
      55,    56,    57,    -1,    -1,    -1,    61,    62,    -1,    -1,
      -1,    66,    -1,    -1,    -1,    -1,    -1,    31,    -1,    33,
      -1,    35,    36,    37,    38,    39,    40,    41,    -1,    -1,
       3,    -1,    87,    47,    89,    90,    50,    10,    11,    -1,
      13,    55,    56,    57,    -1,    -1,    -1,    61,    62,    -1,
      -1,    -1,    66,    -1,    -1,    -1,    -1,    -1,    31,    -1,
      33,    -1,    35,    36,    37,    38,    39,    40,    41,    -1,
      -1,     3,    -1,    -1,    47,    89,    90,    50,    10,    11,
      -1,    13,    55,    56,    57,    -1,    -1,    -1,    61,    62,
      -1,    -1,    -1,    66,    -1,    -1,    -1,    -1,    -1,    31,
      -1,    33,    -1,    35,    36,    37,    38,    39,    40,    41,
      -1,    -1,     3,    -1,    -1,    47,    89,    90,    50,    10,
      11,    -1,    13,    55,    56,    57,    -1,    -1,    -1,    61,
      62,    -1,    -1,    -1,    66,    -1,    -1,    -1,    -1,    -1,
      31,    -1,    33,    -1,    35,    36,    37,    38,    39,    40,
      41,    -1,    -1,    -1,    -1,    -1,    47,    89,    90,    50,
      -1,    -1,    -1,    -1,    55,    56,    57,    -1,    -1,    -1,
      61,    62,    42,    43,    44,    66,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    89,    90,
      42,    43,    44,    -1,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    -1,    -1,
      -1,    42,    43,    44,    94,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    42,
      43,    44,    94,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    -1,    -1,    -1,
      42,    43,    44,    94,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    42,    43,
      44,    94,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    -1,    42,    43,    44,
      92,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    -1,    -1,    12,    -1,    -1,
      -1,    -1,    -1,    -1,    42,    43,    44,    91,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    -1,    -1,    -1,    -1,    91,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,     4,    -1,    -1,     7,     8,
      -1,    -1,    -1,    91,    -1,    14,    15,    16,     4,    -1,
      -1,     7,     8,    -1,    -1,    -1,    -1,    -1,    14,    15,
      16,    30,    -1,    32,    -1,    -1,    -1,    32,    -1,    -1,
      -1,    -1,    -1,    -1,    30,    -1,    32,    42,    43,    44,
      -1,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,     5,     6,     7,     8,     4,
       5,     6,     7,     8,    14,    15,    16,    -1,    -1,    14,
      15,    16,    -1,    -1,    -1,    -1,     4,     5,     6,     7,
       8,    -1,    32,    -1,    -1,    30,    14,    15,    16,     4,
       5,     6,     7,     8,     4,     5,     6,     7,     8,    14,
      15,    16,    30,    -1,    14,    15,    16,     4,    -1,    -1,
       7,     8,    -1,    -1,    -1,    30,    -1,    14,    15,    16,
      30,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    30,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    42,    43,    44,    -1,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      44,    -1,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59
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

case 1:
#line 342 "c-parse.y"
{ if (pedantic)
		    pedwarn ("ISO C forbids an empty source file");
		;
    break;}
case 3:
#line 353 "c-parse.y"
{ yyval.dsptype = NULL; ;
    break;}
case 4:
#line 354 "c-parse.y"
{ obstack_free (&parser_obstack, yyvsp[-2].otype); ;
    break;}
case 5:
#line 356 "c-parse.y"
{ yyval.dsptype = NULL; ggc_collect (); ;
    break;}
case 6:
#line 357 "c-parse.y"
{ obstack_free (&parser_obstack, yyvsp[-2].otype); ;
    break;}
case 10:
#line 365 "c-parse.y"
{ RESTORE_EXT_FLAGS (yyvsp[-1].itype); ;
    break;}
case 11:
#line 371 "c-parse.y"
{ yyval.otype = obstack_alloc (&parser_obstack, 0); ;
    break;}
case 12:
#line 376 "c-parse.y"
{ pedwarn ("data definition has no type or storage class");
		  POP_DECLSPEC_STACK; ;
    break;}
case 13:
#line 379 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 14:
#line 381 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 15:
#line 383 "c-parse.y"
{ shadow_tag (finish_declspecs (yyvsp[-1].dsptype)); ;
    break;}
case 18:
#line 387 "c-parse.y"
{ if (pedantic)
		    pedwarn ("ISO C does not allow extra %<;%> outside of a function"); ;
    break;}
case 19:
#line 393 "c-parse.y"
{ if (!start_function (current_declspecs, yyvsp[0].dtrtype,
				       all_prefix_attributes))
		    YYERROR1;
		;
    break;}
case 20:
#line 398 "c-parse.y"
{ DECL_SOURCE_LOCATION (current_function_decl) = yyvsp[0].location;
		  store_parm_decls (); ;
    break;}
case 21:
#line 401 "c-parse.y"
{ finish_function ();
		  POP_DECLSPEC_STACK; ;
    break;}
case 22:
#line 404 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 23:
#line 406 "c-parse.y"
{ if (!start_function (current_declspecs, yyvsp[0].dtrtype,
				       all_prefix_attributes))
		    YYERROR1;
		;
    break;}
case 24:
#line 411 "c-parse.y"
{ DECL_SOURCE_LOCATION (current_function_decl) = yyvsp[0].location;
		  store_parm_decls (); ;
    break;}
case 25:
#line 414 "c-parse.y"
{ finish_function ();
		  POP_DECLSPEC_STACK; ;
    break;}
case 26:
#line 417 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 27:
#line 419 "c-parse.y"
{ if (!start_function (current_declspecs, yyvsp[0].dtrtype,
				       all_prefix_attributes))
		    YYERROR1;
		;
    break;}
case 28:
#line 424 "c-parse.y"
{ DECL_SOURCE_LOCATION (current_function_decl) = yyvsp[0].location;
		  store_parm_decls (); ;
    break;}
case 29:
#line 427 "c-parse.y"
{ finish_function ();
		  POP_DECLSPEC_STACK; ;
    break;}
case 30:
#line 430 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 33:
#line 439 "c-parse.y"
{ yyval.code = ADDR_EXPR; ;
    break;}
case 34:
#line 441 "c-parse.y"
{ yyval.code = NEGATE_EXPR; ;
    break;}
case 35:
#line 443 "c-parse.y"
{ yyval.code = CONVERT_EXPR;
  if (warn_traditional && !in_system_header)
    warning ("traditional C rejects the unary plus operator");
		;
    break;}
case 36:
#line 448 "c-parse.y"
{ yyval.code = PREINCREMENT_EXPR; ;
    break;}
case 37:
#line 450 "c-parse.y"
{ yyval.code = PREDECREMENT_EXPR; ;
    break;}
case 38:
#line 452 "c-parse.y"
{ yyval.code = BIT_NOT_EXPR; ;
    break;}
case 39:
#line 454 "c-parse.y"
{ yyval.code = TRUTH_NOT_EXPR; ;
    break;}
case 41:
#line 459 "c-parse.y"
{ yyval.exprtype.value = build_compound_expr (yyvsp[-2].exprtype.value, yyvsp[0].exprtype.value);
		  yyval.exprtype.original_code = COMPOUND_EXPR; ;
    break;}
case 42:
#line 465 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 44:
#line 471 "c-parse.y"
{ yyval.ttype = build_tree_list (NULL_TREE, yyvsp[0].exprtype.value); ;
    break;}
case 45:
#line 473 "c-parse.y"
{ chainon (yyvsp[-2].ttype, build_tree_list (NULL_TREE, yyvsp[0].exprtype.value)); ;
    break;}
case 47:
#line 479 "c-parse.y"
{ yyval.exprtype.value = build_indirect_ref (yyvsp[0].exprtype.value, "unary *");
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 48:
#line 483 "c-parse.y"
{ yyval.exprtype = yyvsp[0].exprtype;
		  RESTORE_EXT_FLAGS (yyvsp[-1].itype); ;
    break;}
case 49:
#line 486 "c-parse.y"
{ yyval.exprtype.value = build_unary_op (yyvsp[-1].code, yyvsp[0].exprtype.value, 0);
		  overflow_warning (yyval.exprtype.value);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 50:
#line 491 "c-parse.y"
{ yyval.exprtype.value = finish_label_address_expr (yyvsp[0].ttype);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 51:
#line 494 "c-parse.y"
{ skip_evaluation--;
		  in_sizeof--;
		  if (TREE_CODE (yyvsp[0].exprtype.value) == COMPONENT_REF
		      && DECL_C_BIT_FIELD (TREE_OPERAND (yyvsp[0].exprtype.value, 1)))
		    error ("%<sizeof%> applied to a bit-field");
		  yyval.exprtype = c_expr_sizeof_expr (yyvsp[0].exprtype); ;
    break;}
case 52:
#line 501 "c-parse.y"
{ skip_evaluation--;
		  in_sizeof--;
		  yyval.exprtype = c_expr_sizeof_type (yyvsp[-1].typenametype); ;
    break;}
case 53:
#line 505 "c-parse.y"
{ skip_evaluation--;
		  in_alignof--;
		  yyval.exprtype.value = c_alignof_expr (yyvsp[0].exprtype.value);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 54:
#line 510 "c-parse.y"
{ skip_evaluation--;
		  in_alignof--;
		  yyval.exprtype.value = c_alignof (groktypename (yyvsp[-1].typenametype));
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 55:
#line 515 "c-parse.y"
{ yyval.exprtype.value = build_unary_op (REALPART_EXPR, yyvsp[0].exprtype.value, 0);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 56:
#line 518 "c-parse.y"
{ yyval.exprtype.value = build_unary_op (IMAGPART_EXPR, yyvsp[0].exprtype.value, 0);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 57:
#line 523 "c-parse.y"
{ skip_evaluation++; in_sizeof++; ;
    break;}
case 58:
#line 527 "c-parse.y"
{ skip_evaluation++; in_alignof++; ;
    break;}
case 59:
#line 531 "c-parse.y"
{ skip_evaluation++; in_typeof++; ;
    break;}
case 61:
#line 537 "c-parse.y"
{ yyval.exprtype.value = c_cast_expr (yyvsp[-2].typenametype, yyvsp[0].exprtype.value);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 63:
#line 544 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 64:
#line 546 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 65:
#line 548 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 66:
#line 550 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 67:
#line 552 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 68:
#line 554 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 69:
#line 556 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 70:
#line 558 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 71:
#line 560 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 72:
#line 562 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 73:
#line 564 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 74:
#line 566 "c-parse.y"
{ yyval.exprtype = parser_build_binary_op (yyvsp[-1].code, yyvsp[-2].exprtype, yyvsp[0].exprtype); ;
    break;}
case 75:
#line 568 "c-parse.y"
{ yyvsp[-1].exprtype.value = lang_hooks.truthvalue_conversion
		    (default_conversion (yyvsp[-1].exprtype.value));
		  skip_evaluation += yyvsp[-1].exprtype.value == truthvalue_false_node; ;
    break;}
case 76:
#line 572 "c-parse.y"
{ skip_evaluation -= yyvsp[-3].exprtype.value == truthvalue_false_node;
		  yyval.exprtype = parser_build_binary_op (TRUTH_ANDIF_EXPR, yyvsp[-3].exprtype, yyvsp[0].exprtype); ;
    break;}
case 77:
#line 575 "c-parse.y"
{ yyvsp[-1].exprtype.value = lang_hooks.truthvalue_conversion
		    (default_conversion (yyvsp[-1].exprtype.value));
		  skip_evaluation += yyvsp[-1].exprtype.value == truthvalue_true_node; ;
    break;}
case 78:
#line 579 "c-parse.y"
{ skip_evaluation -= yyvsp[-3].exprtype.value == truthvalue_true_node;
		  yyval.exprtype = parser_build_binary_op (TRUTH_ORIF_EXPR, yyvsp[-3].exprtype, yyvsp[0].exprtype); ;
    break;}
case 79:
#line 582 "c-parse.y"
{ yyvsp[-1].exprtype.value = lang_hooks.truthvalue_conversion
		    (default_conversion (yyvsp[-1].exprtype.value));
		  skip_evaluation += yyvsp[-1].exprtype.value == truthvalue_false_node; ;
    break;}
case 80:
#line 586 "c-parse.y"
{ skip_evaluation += ((yyvsp[-4].exprtype.value == truthvalue_true_node)
				      - (yyvsp[-4].exprtype.value == truthvalue_false_node)); ;
    break;}
case 81:
#line 589 "c-parse.y"
{ skip_evaluation -= yyvsp[-6].exprtype.value == truthvalue_true_node;
		  yyval.exprtype.value = build_conditional_expr (yyvsp[-6].exprtype.value, yyvsp[-3].exprtype.value,
						     yyvsp[0].exprtype.value);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 82:
#line 594 "c-parse.y"
{ if (pedantic)
		    pedwarn ("ISO C forbids omitting the middle term of a ?: expression");
		  /* Make sure first operand is calculated only once.  */
		  yyvsp[0].ttype = save_expr (default_conversion (yyvsp[-1].exprtype.value));
		  yyvsp[-1].exprtype.value = lang_hooks.truthvalue_conversion (yyvsp[0].ttype);
		  skip_evaluation += yyvsp[-1].exprtype.value == truthvalue_true_node; ;
    break;}
case 83:
#line 601 "c-parse.y"
{ skip_evaluation -= yyvsp[-4].exprtype.value == truthvalue_true_node;
		  yyval.exprtype.value = build_conditional_expr (yyvsp[-4].exprtype.value, yyvsp[-3].ttype,
						     yyvsp[0].exprtype.value);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 84:
#line 606 "c-parse.y"
{ yyval.exprtype.value = build_modify_expr (yyvsp[-2].exprtype.value, NOP_EXPR, yyvsp[0].exprtype.value);
		  yyval.exprtype.original_code = MODIFY_EXPR;
		;
    break;}
case 85:
#line 610 "c-parse.y"
{ yyval.exprtype.value = build_modify_expr (yyvsp[-2].exprtype.value, yyvsp[-1].code, yyvsp[0].exprtype.value);
		  TREE_NO_WARNING (yyval.exprtype.value) = 1;
		  yyval.exprtype.original_code = ERROR_MARK;
		;
    break;}
case 86:
#line 618 "c-parse.y"
{
		  if (yychar == YYEMPTY)
		    yychar = YYLEX;
		  yyval.exprtype.value = build_external_ref (yyvsp[0].ttype, yychar == '(');
		  yyval.exprtype.original_code = ERROR_MARK;
		;
    break;}
case 87:
#line 625 "c-parse.y"
{ yyval.exprtype.value = yyvsp[0].ttype; yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 88:
#line 627 "c-parse.y"
{ yyval.exprtype.value = yyvsp[0].ttype; yyval.exprtype.original_code = STRING_CST; ;
    break;}
case 89:
#line 629 "c-parse.y"
{ yyval.exprtype.value = fname_decl (C_RID_CODE (yyvsp[0].ttype), yyvsp[0].ttype);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 90:
#line 632 "c-parse.y"
{ start_init (NULL_TREE, NULL, 0);
		  yyval.ttype = groktypename (yyvsp[-2].typenametype);
		  if (C_TYPE_VARIABLE_SIZE (yyval.ttype))
		    {
		      error ("compound literal has variable size");
		      yyval.ttype = error_mark_node;
		    }
		  really_start_incremental_init (yyval.ttype); ;
    break;}
case 91:
#line 641 "c-parse.y"
{ struct c_expr init = pop_init_level (0);
		  tree constructor = init.value;
		  tree type = yyvsp[-2].ttype;
		  finish_init ();
		  maybe_warn_string_init (type, init);

		  if (pedantic && !flag_isoc99)
		    pedwarn ("ISO C90 forbids compound literals");
		  yyval.exprtype.value = build_compound_literal (type, constructor);
		  yyval.exprtype.original_code = ERROR_MARK;
		;
    break;}
case 92:
#line 653 "c-parse.y"
{ yyval.exprtype.value = yyvsp[-1].exprtype.value;
		  if (TREE_CODE (yyval.exprtype.value) == MODIFY_EXPR)
		    TREE_NO_WARNING (yyval.exprtype.value) = 1;
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 93:
#line 658 "c-parse.y"
{ yyval.exprtype.value = error_mark_node; yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 94:
#line 660 "c-parse.y"
{ if (pedantic)
		    pedwarn ("ISO C forbids braced-groups within expressions");
		  yyval.exprtype.value = c_finish_stmt_expr (yyvsp[-2].ttype);
		  yyval.exprtype.original_code = ERROR_MARK;
		;
    break;}
case 95:
#line 666 "c-parse.y"
{ c_finish_stmt_expr (yyvsp[-2].ttype);
		  yyval.exprtype.value = error_mark_node;
		  yyval.exprtype.original_code = ERROR_MARK;
		;
    break;}
case 96:
#line 671 "c-parse.y"
{ yyval.exprtype.value = build_function_call (yyvsp[-3].exprtype.value, yyvsp[-1].ttype);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 97:
#line 674 "c-parse.y"
{ yyval.exprtype.value = build_va_arg (yyvsp[-3].exprtype.value, groktypename (yyvsp[-1].typenametype));
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 98:
#line 678 "c-parse.y"
{ tree type = groktypename (yyvsp[-1].typenametype);
		  if (type == error_mark_node)
		    offsetof_base = error_mark_node;
		  else
		    offsetof_base = build1 (INDIRECT_REF, type, NULL);
		;
    break;}
case 99:
#line 685 "c-parse.y"
{ yyval.exprtype.value = fold_offsetof (yyvsp[-1].ttype);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 100:
#line 688 "c-parse.y"
{ yyval.exprtype.value = error_mark_node; yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 101:
#line 691 "c-parse.y"
{
                  tree c;

                  c = fold (yyvsp[-5].exprtype.value);
                  STRIP_NOPS (c);
                  if (TREE_CODE (c) != INTEGER_CST)
                    error ("first argument to %<__builtin_choose_expr%> not"
			   " a constant");
                  yyval.exprtype = integer_zerop (c) ? yyvsp[-1].exprtype : yyvsp[-3].exprtype;
		;
    break;}
case 102:
#line 702 "c-parse.y"
{ yyval.exprtype.value = error_mark_node; yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 103:
#line 704 "c-parse.y"
{
		  tree e1, e2;

		  e1 = TYPE_MAIN_VARIANT (groktypename (yyvsp[-3].typenametype));
		  e2 = TYPE_MAIN_VARIANT (groktypename (yyvsp[-1].typenametype));

		  yyval.exprtype.value = comptypes (e1, e2)
		    ? build_int_cst (NULL_TREE, 1)
		    : build_int_cst (NULL_TREE, 0);
		  yyval.exprtype.original_code = ERROR_MARK;
		;
    break;}
case 104:
#line 716 "c-parse.y"
{ yyval.exprtype.value = error_mark_node; yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 105:
#line 718 "c-parse.y"
{ yyval.exprtype.value = build_array_ref (yyvsp[-3].exprtype.value, yyvsp[-1].exprtype.value);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 106:
#line 721 "c-parse.y"
{ yyval.exprtype.value = build_component_ref (yyvsp[-2].exprtype.value, yyvsp[0].ttype);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 107:
#line 724 "c-parse.y"
{
                  tree expr = build_indirect_ref (yyvsp[-2].exprtype.value, "->");
		  yyval.exprtype.value = build_component_ref (expr, yyvsp[0].ttype);
		  yyval.exprtype.original_code = ERROR_MARK;
		;
    break;}
case 108:
#line 730 "c-parse.y"
{ yyval.exprtype.value = build_unary_op (POSTINCREMENT_EXPR, yyvsp[-1].exprtype.value, 0);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 109:
#line 733 "c-parse.y"
{ yyval.exprtype.value = build_unary_op (POSTDECREMENT_EXPR, yyvsp[-1].exprtype.value, 0);
		  yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 110:
#line 743 "c-parse.y"
{ yyval.ttype = build_component_ref (offsetof_base, yyvsp[0].ttype); ;
    break;}
case 111:
#line 745 "c-parse.y"
{ yyval.ttype = build_component_ref (yyvsp[-2].ttype, yyvsp[0].ttype); ;
    break;}
case 112:
#line 747 "c-parse.y"
{ yyval.ttype = build_array_ref (yyvsp[-3].ttype, yyvsp[-1].exprtype.value); ;
    break;}
case 115:
#line 760 "c-parse.y"
{ ;
    break;}
case 120:
#line 776 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 121:
#line 778 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 122:
#line 780 "c-parse.y"
{ shadow_tag_warned (finish_declspecs (yyvsp[-1].dsptype), 1);
		  pedwarn ("empty declaration"); ;
    break;}
case 123:
#line 783 "c-parse.y"
{ pedwarn ("empty declaration"); ;
    break;}
case 124:
#line 792 "c-parse.y"
{ ;
    break;}
case 125:
#line 800 "c-parse.y"
{ pending_xref_error ();
		  PUSH_DECLSPEC_STACK;
		  if (yyvsp[0].dsptype)
		    {
		      prefix_attributes = yyvsp[0].dsptype->attrs;
		      yyvsp[0].dsptype->attrs = NULL_TREE;
		      current_declspecs = yyvsp[0].dsptype;
		    }
		  else
		    {
		      prefix_attributes = NULL_TREE;
		      current_declspecs = build_null_declspecs ();
		    }
		  current_declspecs = finish_declspecs (current_declspecs);
		  all_prefix_attributes = prefix_attributes; ;
    break;}
case 126:
#line 821 "c-parse.y"
{ all_prefix_attributes = chainon (yyvsp[0].ttype, prefix_attributes); ;
    break;}
case 127:
#line 826 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 128:
#line 828 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 129:
#line 830 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 130:
#line 832 "c-parse.y"
{ POP_DECLSPEC_STACK; ;
    break;}
case 131:
#line 834 "c-parse.y"
{ shadow_tag (finish_declspecs (yyvsp[-1].dsptype)); ;
    break;}
case 132:
#line 836 "c-parse.y"
{ RESTORE_EXT_FLAGS (yyvsp[-1].itype); ;
    break;}
case 133:
#line 882 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (build_null_declspecs (), yyvsp[0].ttype); ;
    break;}
case 134:
#line 884 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 135:
#line 886 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 136:
#line 891 "c-parse.y"
{ yyval.dsptype = declspecs_add_attrs (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 137:
#line 896 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 138:
#line 898 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 139:
#line 903 "c-parse.y"
{ yyval.dsptype = declspecs_add_attrs (build_null_declspecs (), yyvsp[0].ttype); ;
    break;}
case 140:
#line 905 "c-parse.y"
{ yyval.dsptype = declspecs_add_attrs (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 141:
#line 910 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (build_null_declspecs (), yyvsp[0].tstype); ;
    break;}
case 142:
#line 912 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 143:
#line 914 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 144:
#line 916 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 145:
#line 918 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 146:
#line 920 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 147:
#line 922 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 148:
#line 927 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (build_null_declspecs (), yyvsp[0].tstype); ;
    break;}
case 149:
#line 929 "c-parse.y"
{ yyval.dsptype = declspecs_add_attrs (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 150:
#line 931 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 151:
#line 933 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 152:
#line 935 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 153:
#line 937 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 154:
#line 942 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 155:
#line 944 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 156:
#line 946 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 157:
#line 948 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 158:
#line 950 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 159:
#line 952 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 160:
#line 957 "c-parse.y"
{ yyval.dsptype = declspecs_add_attrs (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 161:
#line 959 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 162:
#line 961 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 163:
#line 963 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 164:
#line 965 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 165:
#line 970 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (build_null_declspecs (), yyvsp[0].ttype); ;
    break;}
case 166:
#line 972 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 167:
#line 974 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 168:
#line 976 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 169:
#line 978 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 170:
#line 980 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 171:
#line 982 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 172:
#line 987 "c-parse.y"
{ yyval.dsptype = declspecs_add_attrs (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 173:
#line 992 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 174:
#line 994 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 175:
#line 996 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 176:
#line 998 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 177:
#line 1000 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 178:
#line 1002 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 179:
#line 1007 "c-parse.y"
{ yyval.dsptype = declspecs_add_attrs (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 180:
#line 1012 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 181:
#line 1014 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 182:
#line 1016 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 183:
#line 1018 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 184:
#line 1020 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 185:
#line 1022 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 186:
#line 1024 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 187:
#line 1026 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 188:
#line 1028 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 189:
#line 1030 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 190:
#line 1035 "c-parse.y"
{ yyval.dsptype = declspecs_add_attrs (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 191:
#line 1037 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 192:
#line 1039 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 193:
#line 1041 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 194:
#line 1043 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 195:
#line 1048 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 196:
#line 1050 "c-parse.y"
{ yyval.dsptype = declspecs_add_qual (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 197:
#line 1052 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 198:
#line 1054 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 199:
#line 1056 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 200:
#line 1058 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 201:
#line 1060 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 202:
#line 1062 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 203:
#line 1064 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 204:
#line 1066 "c-parse.y"
{ yyval.dsptype = declspecs_add_scspec (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 205:
#line 1071 "c-parse.y"
{ yyval.dsptype = declspecs_add_attrs (yyvsp[-1].dsptype, yyvsp[0].ttype); ;
    break;}
case 206:
#line 1073 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 207:
#line 1075 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 208:
#line 1077 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 209:
#line 1079 "c-parse.y"
{ yyval.dsptype = declspecs_add_type (yyvsp[-1].dsptype, yyvsp[0].tstype); ;
    break;}
case 266:
#line 1166 "c-parse.y"
{ yyval.dsptype = NULL; ;
    break;}
case 267:
#line 1168 "c-parse.y"
{ yyval.dsptype = yyvsp[0].dsptype; ;
    break;}
case 271:
#line 1203 "c-parse.y"
{ OBJC_NEED_RAW_IDENTIFIER (1);
		  yyval.tstype.kind = ctsk_resword;
		  yyval.tstype.spec = yyvsp[0].ttype; ;
    break;}
case 274:
#line 1215 "c-parse.y"
{ /* For a typedef name, record the meaning, not the name.
		     In case of `foo foo, bar;'.  */
		  yyval.tstype.kind = ctsk_typedef;
		  yyval.tstype.spec = lookup_name (yyvsp[0].ttype); ;
    break;}
case 275:
#line 1220 "c-parse.y"
{ skip_evaluation--;
		  in_typeof--;
		  if (TREE_CODE (yyvsp[-1].exprtype.value) == COMPONENT_REF
		      && DECL_C_BIT_FIELD (TREE_OPERAND (yyvsp[-1].exprtype.value, 1)))
		    error ("%<typeof%> applied to a bit-field");
		  yyval.tstype.kind = ctsk_typeof;
		  yyval.tstype.spec = TREE_TYPE (yyvsp[-1].exprtype.value);
		  pop_maybe_used (variably_modified_type_p (yyval.tstype.spec,
							    NULL_TREE)); ;
    break;}
case 276:
#line 1230 "c-parse.y"
{ skip_evaluation--;
		  in_typeof--;
		  yyval.tstype.kind = ctsk_typeof;
		  yyval.tstype.spec = groktypename (yyvsp[-1].typenametype);
		  pop_maybe_used (variably_modified_type_p (yyval.tstype.spec,
							    NULL_TREE)); ;
    break;}
case 281:
#line 1252 "c-parse.y"
{ yyval.ttype = start_decl (yyvsp[-3].dtrtype, current_declspecs, true,
					  chainon (yyvsp[-1].ttype, all_prefix_attributes));
		  if (!yyval.ttype)
		    yyval.ttype = error_mark_node;
		  start_init (yyval.ttype, yyvsp[-2].ttype, global_bindings_p ()); ;
    break;}
case 282:
#line 1259 "c-parse.y"
{ finish_init ();
		  if (yyvsp[-1].ttype != error_mark_node)
		    {
		      maybe_warn_string_init (TREE_TYPE (yyvsp[-1].ttype), yyvsp[0].exprtype);
		      finish_decl (yyvsp[-1].ttype, yyvsp[0].exprtype.value, yyvsp[-4].ttype);
		    }
		;
    break;}
case 283:
#line 1267 "c-parse.y"
{ tree d = start_decl (yyvsp[-2].dtrtype, current_declspecs, false,
				       chainon (yyvsp[0].ttype, all_prefix_attributes));
		  if (d)
		    finish_decl (d, NULL_TREE, yyvsp[-1].ttype);
                ;
    break;}
case 284:
#line 1276 "c-parse.y"
{ yyval.ttype = start_decl (yyvsp[-3].dtrtype, current_declspecs, true,
					  chainon (yyvsp[-1].ttype, all_prefix_attributes));
		  if (!yyval.ttype)
		    yyval.ttype = error_mark_node;
		  start_init (yyval.ttype, yyvsp[-2].ttype, global_bindings_p ()); ;
    break;}
case 285:
#line 1283 "c-parse.y"
{ finish_init ();
		  if (yyvsp[-1].ttype != error_mark_node)
		    {
		      maybe_warn_string_init (TREE_TYPE (yyvsp[-1].ttype), yyvsp[0].exprtype);
		      finish_decl (yyvsp[-1].ttype, yyvsp[0].exprtype.value, yyvsp[-4].ttype);
		    }
		;
    break;}
case 286:
#line 1291 "c-parse.y"
{ tree d = start_decl (yyvsp[-2].dtrtype, current_declspecs, false,
				       chainon (yyvsp[0].ttype, all_prefix_attributes));
		  if (d)
                    finish_decl (d, NULL_TREE, yyvsp[-1].ttype); ;
    break;}
case 287:
#line 1300 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 288:
#line 1302 "c-parse.y"
{ yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 289:
#line 1307 "c-parse.y"
{ yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 290:
#line 1309 "c-parse.y"
{ yyval.ttype = chainon (yyvsp[-1].ttype, yyvsp[0].ttype); ;
    break;}
case 291:
#line 1315 "c-parse.y"
{ yyval.ttype = yyvsp[-3].ttype; ;
    break;}
case 292:
#line 1317 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 293:
#line 1322 "c-parse.y"
{ yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 294:
#line 1324 "c-parse.y"
{ yyval.ttype = chainon (yyvsp[-2].ttype, yyvsp[0].ttype); ;
    break;}
case 295:
#line 1329 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 296:
#line 1331 "c-parse.y"
{ yyval.ttype = build_tree_list (yyvsp[0].ttype, NULL_TREE); ;
    break;}
case 297:
#line 1333 "c-parse.y"
{ yyval.ttype = build_tree_list (yyvsp[-3].ttype, build_tree_list (NULL_TREE, yyvsp[-1].ttype)); ;
    break;}
case 298:
#line 1335 "c-parse.y"
{ yyval.ttype = build_tree_list (yyvsp[-5].ttype, tree_cons (NULL_TREE, yyvsp[-3].ttype, yyvsp[-1].ttype)); ;
    break;}
case 299:
#line 1337 "c-parse.y"
{ yyval.ttype = build_tree_list (yyvsp[-3].ttype, yyvsp[-1].ttype); ;
    break;}
case 306:
#line 1359 "c-parse.y"
{ yyval.exprtype = yyvsp[0].exprtype; ;
    break;}
case 307:
#line 1361 "c-parse.y"
{ really_start_incremental_init (NULL_TREE); ;
    break;}
case 308:
#line 1363 "c-parse.y"
{ yyval.exprtype = pop_init_level (0); ;
    break;}
case 309:
#line 1365 "c-parse.y"
{ yyval.exprtype.value = error_mark_node; yyval.exprtype.original_code = ERROR_MARK; ;
    break;}
case 310:
#line 1371 "c-parse.y"
{ if (pedantic)
		    pedwarn ("ISO C forbids empty initializer braces"); ;
    break;}
case 314:
#line 1385 "c-parse.y"
{ if (pedantic && !flag_isoc99)
		    pedwarn ("ISO C90 forbids specifying subobject to initialize"); ;
    break;}
case 315:
#line 1388 "c-parse.y"
{ if (pedantic)
		    pedwarn ("obsolete use of designated initializer without %<=%>"); ;
    break;}
case 316:
#line 1391 "c-parse.y"
{ set_init_label (yyvsp[-1].ttype);
		  if (pedantic)
		    pedwarn ("obsolete use of designated initializer with %<:%>"); ;
    break;}
case 317:
#line 1395 "c-parse.y"
{;
    break;}
case 319:
#line 1401 "c-parse.y"
{ push_init_level (0); ;
    break;}
case 320:
#line 1403 "c-parse.y"
{ process_init_element (pop_init_level (0)); ;
    break;}
case 321:
#line 1405 "c-parse.y"
{ process_init_element (yyvsp[0].exprtype); ;
    break;}
case 325:
#line 1416 "c-parse.y"
{ set_init_label (yyvsp[0].ttype); ;
    break;}
case 327:
#line 1422 "c-parse.y"
{ set_init_index (yyvsp[-3].exprtype.value, yyvsp[-1].exprtype.value);
		  if (pedantic)
		    pedwarn ("ISO C forbids specifying range of elements to initialize"); ;
    break;}
case 328:
#line 1426 "c-parse.y"
{ set_init_index (yyvsp[-1].exprtype.value, NULL_TREE); ;
    break;}
case 329:
#line 1431 "c-parse.y"
{ if (pedantic)
		    pedwarn ("ISO C forbids nested functions");

		  push_function_context ();
		  if (!start_function (current_declspecs, yyvsp[0].dtrtype,
				       all_prefix_attributes))
		    {
		      pop_function_context ();
		      YYERROR1;
		    }
		;
    break;}
case 330:
#line 1443 "c-parse.y"
{ tree decl = current_function_decl;
		  DECL_SOURCE_LOCATION (decl) = yyvsp[0].location;
		  store_parm_decls (); ;
    break;}
case 331:
#line 1452 "c-parse.y"
{ tree decl = current_function_decl;
		  add_stmt (yyvsp[0].ttype);
		  finish_function ();
		  pop_function_context ();
		  add_stmt (build_stmt (DECL_EXPR, decl)); ;
    break;}
case 332:
#line 1461 "c-parse.y"
{ if (pedantic)
		    pedwarn ("ISO C forbids nested functions");

		  push_function_context ();
		  if (!start_function (current_declspecs, yyvsp[0].dtrtype,
				       all_prefix_attributes))
		    {
		      pop_function_context ();
		      YYERROR1;
		    }
		;
    break;}
case 333:
#line 1473 "c-parse.y"
{ tree decl = current_function_decl;
		  DECL_SOURCE_LOCATION (decl) = yyvsp[0].location;
		  store_parm_decls (); ;
    break;}
case 334:
#line 1482 "c-parse.y"
{ tree decl = current_function_decl;
		  add_stmt (yyvsp[0].ttype);
		  finish_function ();
		  pop_function_context ();
		  add_stmt (build_stmt (DECL_EXPR, decl)); ;
    break;}
case 337:
#line 1501 "c-parse.y"
{ yyval.dtrtype = yyvsp[-2].ttype ? build_attrs_declarator (yyvsp[-2].ttype, yyvsp[-1].dtrtype) : yyvsp[-1].dtrtype; ;
    break;}
case 338:
#line 1503 "c-parse.y"
{ yyval.dtrtype = build_function_declarator (yyvsp[0].arginfotype, yyvsp[-2].dtrtype); ;
    break;}
case 339:
#line 1505 "c-parse.y"
{ yyval.dtrtype = set_array_declarator_inner (yyvsp[0].dtrtype, yyvsp[-1].dtrtype, false); ;
    break;}
case 340:
#line 1507 "c-parse.y"
{ yyval.dtrtype = make_pointer_declarator (yyvsp[-1].dsptype, yyvsp[0].dtrtype); ;
    break;}
case 341:
#line 1509 "c-parse.y"
{ yyval.dtrtype = build_id_declarator (yyvsp[0].ttype); ;
    break;}
case 344:
#line 1523 "c-parse.y"
{ yyval.dtrtype = build_function_declarator (yyvsp[0].arginfotype, yyvsp[-2].dtrtype); ;
    break;}
case 345:
#line 1525 "c-parse.y"
{ yyval.dtrtype = set_array_declarator_inner (yyvsp[0].dtrtype, yyvsp[-1].dtrtype, false); ;
    break;}
case 346:
#line 1527 "c-parse.y"
{ yyval.dtrtype = build_id_declarator (yyvsp[0].ttype); ;
    break;}
case 347:
#line 1532 "c-parse.y"
{ yyval.dtrtype = build_function_declarator (yyvsp[0].arginfotype, yyvsp[-2].dtrtype); ;
    break;}
case 348:
#line 1534 "c-parse.y"
{ yyval.dtrtype = set_array_declarator_inner (yyvsp[0].dtrtype, yyvsp[-1].dtrtype, false); ;
    break;}
case 349:
#line 1536 "c-parse.y"
{ yyval.dtrtype = make_pointer_declarator (yyvsp[-1].dsptype, yyvsp[0].dtrtype); ;
    break;}
case 350:
#line 1538 "c-parse.y"
{ yyval.dtrtype = make_pointer_declarator (yyvsp[-1].dsptype, yyvsp[0].dtrtype); ;
    break;}
case 351:
#line 1540 "c-parse.y"
{ yyval.dtrtype = yyvsp[-2].ttype ? build_attrs_declarator (yyvsp[-2].ttype, yyvsp[-1].dtrtype) : yyvsp[-1].dtrtype; ;
    break;}
case 352:
#line 1548 "c-parse.y"
{ yyval.dtrtype = build_function_declarator (yyvsp[0].arginfotype, yyvsp[-2].dtrtype); ;
    break;}
case 353:
#line 1550 "c-parse.y"
{ yyval.dtrtype = yyvsp[-2].ttype ? build_attrs_declarator (yyvsp[-2].ttype, yyvsp[-1].dtrtype) : yyvsp[-1].dtrtype; ;
    break;}
case 354:
#line 1552 "c-parse.y"
{ yyval.dtrtype = make_pointer_declarator (yyvsp[-1].dsptype, yyvsp[0].dtrtype); ;
    break;}
case 355:
#line 1554 "c-parse.y"
{ yyval.dtrtype = set_array_declarator_inner (yyvsp[0].dtrtype, yyvsp[-1].dtrtype, false); ;
    break;}
case 356:
#line 1556 "c-parse.y"
{ yyval.dtrtype = build_id_declarator (yyvsp[0].ttype); ;
    break;}
case 357:
#line 1561 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 358:
#line 1563 "c-parse.y"
{ yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 359:
#line 1568 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 360:
#line 1570 "c-parse.y"
{ yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 361:
#line 1575 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 362:
#line 1577 "c-parse.y"
{ yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 363:
#line 1588 "c-parse.y"
{ yyval.ttype = start_struct (RECORD_TYPE, yyvsp[-1].ttype);
		  /* Start scope of tag before parsing components.  */
		;
    break;}
case 364:
#line 1592 "c-parse.y"
{ yyval.tstype.spec = finish_struct (yyvsp[-3].ttype, nreverse (yyvsp[-2].ttype),
					   chainon (yyvsp[-6].ttype, yyvsp[0].ttype));
		  yyval.tstype.kind = ctsk_tagdef; ;
    break;}
case 365:
#line 1596 "c-parse.y"
{ yyval.tstype.spec = finish_struct (start_struct (RECORD_TYPE,
							 NULL_TREE),
					   nreverse (yyvsp[-2].ttype), chainon (yyvsp[-4].ttype, yyvsp[0].ttype));
		  yyval.tstype.kind = ctsk_tagdef;
		;
    break;}
case 366:
#line 1602 "c-parse.y"
{ yyval.ttype = start_struct (UNION_TYPE, yyvsp[-1].ttype); ;
    break;}
case 367:
#line 1604 "c-parse.y"
{ yyval.tstype.spec = finish_struct (yyvsp[-3].ttype, nreverse (yyvsp[-2].ttype),
					   chainon (yyvsp[-6].ttype, yyvsp[0].ttype));
		  yyval.tstype.kind = ctsk_tagdef; ;
    break;}
case 368:
#line 1608 "c-parse.y"
{ yyval.tstype.spec = finish_struct (start_struct (UNION_TYPE,
							 NULL_TREE),
					   nreverse (yyvsp[-2].ttype), chainon (yyvsp[-4].ttype, yyvsp[0].ttype));
		  yyval.tstype.kind = ctsk_tagdef;
		;
    break;}
case 369:
#line 1614 "c-parse.y"
{ yyval.ttype = start_enum (yyvsp[-1].ttype); ;
    break;}
case 370:
#line 1616 "c-parse.y"
{ yyval.tstype.spec = finish_enum (yyvsp[-4].ttype, nreverse (yyvsp[-3].ttype),
					 chainon (yyvsp[-7].ttype, yyvsp[0].ttype));
		  yyval.tstype.kind = ctsk_tagdef; ;
    break;}
case 371:
#line 1620 "c-parse.y"
{ yyval.ttype = start_enum (NULL_TREE); ;
    break;}
case 372:
#line 1622 "c-parse.y"
{ yyval.tstype.spec = finish_enum (yyvsp[-4].ttype, nreverse (yyvsp[-3].ttype),
					 chainon (yyvsp[-6].ttype, yyvsp[0].ttype));
		  yyval.tstype.kind = ctsk_tagdef; ;
    break;}
case 373:
#line 1629 "c-parse.y"
{ yyval.tstype = parser_xref_tag (RECORD_TYPE, yyvsp[0].ttype); ;
    break;}
case 374:
#line 1631 "c-parse.y"
{ yyval.tstype = parser_xref_tag (UNION_TYPE, yyvsp[0].ttype); ;
    break;}
case 375:
#line 1633 "c-parse.y"
{ yyval.tstype = parser_xref_tag (ENUMERAL_TYPE, yyvsp[0].ttype);
		  /* In ISO C, enumerated types can be referred to
		     only if already defined.  */
		  if (pedantic && !COMPLETE_TYPE_P (yyval.tstype.spec))
		    pedwarn ("ISO C forbids forward references to %<enum%> types"); ;
    break;}
case 379:
#line 1648 "c-parse.y"
{ if (pedantic && !flag_isoc99)
		    pedwarn ("comma at end of enumerator list"); ;
    break;}
case 380:
#line 1666 "c-parse.y"
{ yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 381:
#line 1668 "c-parse.y"
{ yyval.ttype = chainon (yyvsp[0].ttype, yyvsp[-1].ttype);
		  pedwarn ("no semicolon at end of struct or union"); ;
    break;}
case 382:
#line 1673 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 383:
#line 1675 "c-parse.y"
{ yyval.ttype = chainon (yyvsp[-1].ttype, yyvsp[-2].ttype); ;
    break;}
case 384:
#line 1677 "c-parse.y"
{ if (pedantic)
		    pedwarn ("extra semicolon in struct or union specified"); ;
    break;}
case 385:
#line 1683 "c-parse.y"
{ yyval.ttype = yyvsp[0].ttype;
		  POP_DECLSPEC_STACK; ;
    break;}
case 386:
#line 1686 "c-parse.y"
{
		  /* Support for unnamed structs or unions as members of
		     structs or unions (which is [a] useful and [b] supports
		     MS P-SDK).  */
		  yyval.ttype = grokfield (build_id_declarator (NULL_TREE),
				  current_declspecs, NULL_TREE);
		  POP_DECLSPEC_STACK; ;
    break;}
case 387:
#line 1694 "c-parse.y"
{ yyval.ttype = yyvsp[0].ttype;
		  POP_DECLSPEC_STACK; ;
    break;}
case 388:
#line 1697 "c-parse.y"
{ if (pedantic)
		    pedwarn ("ISO C forbids member declarations with no members");
		  shadow_tag_warned (finish_declspecs (yyvsp[0].dsptype), pedantic);
		  yyval.ttype = NULL_TREE; ;
    break;}
case 389:
#line 1702 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 390:
#line 1704 "c-parse.y"
{ yyval.ttype = yyvsp[0].ttype;
		  RESTORE_EXT_FLAGS (yyvsp[-1].itype); ;
    break;}
case 392:
#line 1711 "c-parse.y"
{ TREE_CHAIN (yyvsp[0].ttype) = yyvsp[-3].ttype; yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 394:
#line 1717 "c-parse.y"
{ TREE_CHAIN (yyvsp[0].ttype) = yyvsp[-3].ttype; yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 395:
#line 1722 "c-parse.y"
{ yyval.ttype = grokfield (yyvsp[-1].dtrtype, current_declspecs, NULL_TREE);
		  decl_attributes (&yyval.ttype,
				   chainon (yyvsp[0].ttype, all_prefix_attributes), 0); ;
    break;}
case 396:
#line 1726 "c-parse.y"
{ yyval.ttype = grokfield (yyvsp[-3].dtrtype, current_declspecs, yyvsp[-1].exprtype.value);
		  decl_attributes (&yyval.ttype,
				   chainon (yyvsp[0].ttype, all_prefix_attributes), 0); ;
    break;}
case 397:
#line 1730 "c-parse.y"
{ yyval.ttype = grokfield (build_id_declarator (NULL_TREE),
				  current_declspecs, yyvsp[-1].exprtype.value);
		  decl_attributes (&yyval.ttype,
				   chainon (yyvsp[0].ttype, all_prefix_attributes), 0); ;
    break;}
case 398:
#line 1738 "c-parse.y"
{ yyval.ttype = grokfield (yyvsp[-1].dtrtype, current_declspecs, NULL_TREE);
		  decl_attributes (&yyval.ttype,
				   chainon (yyvsp[0].ttype, all_prefix_attributes), 0); ;
    break;}
case 399:
#line 1742 "c-parse.y"
{ yyval.ttype = grokfield (yyvsp[-3].dtrtype, current_declspecs, yyvsp[-1].exprtype.value);
		  decl_attributes (&yyval.ttype,
				   chainon (yyvsp[0].ttype, all_prefix_attributes), 0); ;
    break;}
case 400:
#line 1746 "c-parse.y"
{ yyval.ttype = grokfield (build_id_declarator (NULL_TREE),
				  current_declspecs, yyvsp[-1].exprtype.value);
		  decl_attributes (&yyval.ttype,
				   chainon (yyvsp[0].ttype, all_prefix_attributes), 0); ;
    break;}
case 402:
#line 1758 "c-parse.y"
{ if (yyvsp[-2].ttype == error_mark_node)
		    yyval.ttype = yyvsp[-2].ttype;
		  else
		    TREE_CHAIN (yyvsp[0].ttype) = yyvsp[-2].ttype, yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 403:
#line 1763 "c-parse.y"
{ yyval.ttype = error_mark_node; ;
    break;}
case 404:
#line 1769 "c-parse.y"
{ yyval.ttype = build_enumerator (yyvsp[0].ttype, NULL_TREE); ;
    break;}
case 405:
#line 1771 "c-parse.y"
{ yyval.ttype = build_enumerator (yyvsp[-2].ttype, yyvsp[0].exprtype.value); ;
    break;}
case 406:
#line 1776 "c-parse.y"
{ pending_xref_error ();
		  yyval.dsptype = finish_declspecs (yyvsp[0].dsptype); ;
    break;}
case 407:
#line 1779 "c-parse.y"
{ yyval.typenametype = XOBNEW (&parser_obstack, struct c_type_name);
		  yyval.typenametype->specs = yyvsp[-1].dsptype;
		  yyval.typenametype->declarator = yyvsp[0].dtrtype; ;
    break;}
case 408:
#line 1786 "c-parse.y"
{ yyval.dtrtype = build_id_declarator (NULL_TREE); ;
    break;}
case 410:
#line 1792 "c-parse.y"
{ yyval.parmtype = build_c_parm (current_declspecs, all_prefix_attributes,
				     build_id_declarator (NULL_TREE)); ;
    break;}
case 411:
#line 1795 "c-parse.y"
{ yyval.parmtype = build_c_parm (current_declspecs, all_prefix_attributes,
				     yyvsp[0].dtrtype); ;
    break;}
case 412:
#line 1798 "c-parse.y"
{ yyval.parmtype = build_c_parm (current_declspecs,
				     chainon (yyvsp[0].ttype, all_prefix_attributes),
				     yyvsp[-1].dtrtype); ;
    break;}
case 416:
#line 1811 "c-parse.y"
{ yyval.dtrtype = make_pointer_declarator (yyvsp[-1].dsptype, yyvsp[0].dtrtype); ;
    break;}
case 417:
#line 1816 "c-parse.y"
{ yyval.dtrtype = make_pointer_declarator
		    (yyvsp[0].dsptype, build_id_declarator (NULL_TREE)); ;
    break;}
case 418:
#line 1819 "c-parse.y"
{ yyval.dtrtype = make_pointer_declarator (yyvsp[-1].dsptype, yyvsp[0].dtrtype); ;
    break;}
case 419:
#line 1824 "c-parse.y"
{ yyval.dtrtype = yyvsp[-2].ttype ? build_attrs_declarator (yyvsp[-2].ttype, yyvsp[-1].dtrtype) : yyvsp[-1].dtrtype; ;
    break;}
case 420:
#line 1826 "c-parse.y"
{ yyval.dtrtype = build_function_declarator (yyvsp[0].arginfotype, yyvsp[-2].dtrtype); ;
    break;}
case 421:
#line 1828 "c-parse.y"
{ yyval.dtrtype = set_array_declarator_inner (yyvsp[0].dtrtype, yyvsp[-1].dtrtype, true); ;
    break;}
case 422:
#line 1830 "c-parse.y"
{ yyval.dtrtype = build_function_declarator
		    (yyvsp[0].arginfotype, build_id_declarator (NULL_TREE)); ;
    break;}
case 423:
#line 1833 "c-parse.y"
{ yyval.dtrtype = set_array_declarator_inner
		    (yyvsp[0].dtrtype, build_id_declarator (NULL_TREE), true); ;
    break;}
case 424:
#line 1841 "c-parse.y"
{ yyval.dtrtype = build_array_declarator (yyvsp[-1].exprtype.value, yyvsp[-2].dsptype, false, false); ;
    break;}
case 425:
#line 1843 "c-parse.y"
{ yyval.dtrtype = build_array_declarator (NULL_TREE, yyvsp[-1].dsptype, false, false); ;
    break;}
case 426:
#line 1845 "c-parse.y"
{ yyval.dtrtype = build_array_declarator (NULL_TREE, yyvsp[-2].dsptype, false, true); ;
    break;}
case 427:
#line 1847 "c-parse.y"
{ yyval.dtrtype = build_array_declarator (yyvsp[-1].exprtype.value, yyvsp[-2].dsptype, true, false); ;
    break;}
case 428:
#line 1850 "c-parse.y"
{ yyval.dtrtype = build_array_declarator (yyvsp[-1].exprtype.value, yyvsp[-3].dsptype, true, false); ;
    break;}
case 431:
#line 1863 "c-parse.y"
{
		  error ("label at end of compound statement");
		;
    break;}
case 439:
#line 1880 "c-parse.y"
{
		  if ((pedantic && !flag_isoc99)
		      || warn_declaration_after_statement)
		    pedwarn_c90 ("ISO C90 forbids mixed declarations and code");
		;
    break;}
case 454:
#line 1914 "c-parse.y"
{ yyval.ttype = c_begin_compound_stmt (flag_isoc99); ;
    break;}
case 456:
#line 1922 "c-parse.y"
{ if (pedantic)
		    pedwarn ("ISO C forbids label declarations"); ;
    break;}
case 459:
#line 1933 "c-parse.y"
{ tree link;
		  for (link = yyvsp[-1].ttype; link; link = TREE_CHAIN (link))
		    {
		      tree label = declare_label (TREE_VALUE (link));
		      C_DECLARED_LABEL_FLAG (label) = 1;
		      add_stmt (build_stmt (DECL_EXPR, label));
		    }
		;
    break;}
case 460:
#line 1947 "c-parse.y"
{ add_stmt (yyvsp[0].ttype); ;
    break;}
case 462:
#line 1951 "c-parse.y"
{ yyval.ttype = c_begin_compound_stmt (true); ;
    break;}
case 467:
#line 1965 "c-parse.y"
{ if (cur_stmt_list == NULL)
		    {
		      error ("braced-group within expression allowed "
			     "only inside a function");
		      YYERROR;
		    }
		  yyval.ttype = c_begin_stmt_expr ();
		;
    break;}
case 468:
#line 1976 "c-parse.y"
{ yyval.ttype = c_end_compound_stmt (yyvsp[-1].ttype, true); ;
    break;}
case 469:
#line 1984 "c-parse.y"
{ if (yychar == YYEMPTY)
		    yychar = YYLEX;
		  yyval.location = input_location; ;
    break;}
case 472:
#line 1997 "c-parse.y"
{ yyval.ttype = c_end_compound_stmt (yyvsp[-2].ttype, flag_isoc99); ;
    break;}
case 473:
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
		  if (yyvsp[0].ttype && EXPR_P (yyvsp[0].ttype))
		    SET_EXPR_LOCATION (yyvsp[0].ttype, yyvsp[-1].location);
		;
    break;}
case 474:
#line 2020 "c-parse.y"
{ if (yyvsp[0].ttype) SET_EXPR_LOCATION (yyvsp[0].ttype, yyvsp[-1].location); ;
    break;}
case 475:
#line 2024 "c-parse.y"
{ yyval.ttype = lang_hooks.truthvalue_conversion (yyvsp[0].exprtype.value);
		  if (EXPR_P (yyval.ttype))
		    SET_EXPR_LOCATION (yyval.ttype, yyvsp[-1].location); ;
    break;}
case 476:
#line 2037 "c-parse.y"
{ yyval.ttype = c_end_compound_stmt (yyvsp[-2].ttype, flag_isoc99); ;
    break;}
case 477:
#line 2042 "c-parse.y"
{ if (extra_warnings)
		    add_stmt (build (NOP_EXPR, NULL_TREE, NULL_TREE));
		  yyval.ttype = c_end_compound_stmt (yyvsp[-2].ttype, flag_isoc99); ;
    break;}
case 479:
#line 2051 "c-parse.y"
{ c_finish_if_stmt (yyvsp[-6].location, yyvsp[-4].ttype, yyvsp[-2].ttype, yyvsp[0].ttype, true);
		  add_stmt (c_end_compound_stmt (yyvsp[-7].ttype, flag_isoc99)); ;
    break;}
case 480:
#line 2055 "c-parse.y"
{ c_finish_if_stmt (yyvsp[-6].location, yyvsp[-4].ttype, yyvsp[-2].ttype, yyvsp[0].ttype, false);
		  add_stmt (c_end_compound_stmt (yyvsp[-7].ttype, flag_isoc99)); ;
    break;}
case 481:
#line 2059 "c-parse.y"
{ c_finish_if_stmt (yyvsp[-4].location, yyvsp[-2].ttype, yyvsp[0].ttype, NULL, true);
		  add_stmt (c_end_compound_stmt (yyvsp[-5].ttype, flag_isoc99)); ;
    break;}
case 482:
#line 2063 "c-parse.y"
{ c_finish_if_stmt (yyvsp[-4].location, yyvsp[-2].ttype, yyvsp[0].ttype, NULL, false);
		  add_stmt (c_end_compound_stmt (yyvsp[-5].ttype, flag_isoc99)); ;
    break;}
case 483:
#line 2068 "c-parse.y"
{ yyval.ttype = c_break_label; c_break_label = NULL; ;
    break;}
case 484:
#line 2072 "c-parse.y"
{ yyval.ttype = c_cont_label; c_cont_label = NULL; ;
    break;}
case 485:
#line 2078 "c-parse.y"
{ c_finish_loop (yyvsp[-6].location, yyvsp[-4].ttype, NULL, yyvsp[0].ttype, c_break_label,
				 c_cont_label, true);
		  add_stmt (c_end_compound_stmt (yyvsp[-7].ttype, flag_isoc99));
		  c_break_label = yyvsp[-2].ttype; c_cont_label = yyvsp[-1].ttype; ;
    break;}
case 486:
#line 2087 "c-parse.y"
{ yyval.ttype = c_break_label; c_break_label = yyvsp[-3].ttype; ;
    break;}
case 487:
#line 2088 "c-parse.y"
{ yyval.ttype = c_cont_label; c_cont_label = yyvsp[-3].ttype; ;
    break;}
case 488:
#line 2090 "c-parse.y"
{ c_finish_loop (yyvsp[-10].location, yyvsp[-2].ttype, NULL, yyvsp[-7].ttype, yyvsp[-5].ttype,
				 yyvsp[-4].ttype, false);
		  add_stmt (c_end_compound_stmt (yyvsp[-11].ttype, flag_isoc99)); ;
    break;}
case 489:
#line 2097 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 490:
#line 2099 "c-parse.y"
{ yyval.ttype = yyvsp[0].exprtype.value; ;
    break;}
case 491:
#line 2104 "c-parse.y"
{ c_finish_expr_stmt (yyvsp[-1].ttype); ;
    break;}
case 492:
#line 2106 "c-parse.y"
{ check_for_loop_decls (); ;
    break;}
case 493:
#line 2110 "c-parse.y"
{ if (yyvsp[0].ttype)
		    {
		      yyval.ttype = lang_hooks.truthvalue_conversion (yyvsp[0].ttype);
		      if (EXPR_P (yyval.ttype))
			SET_EXPR_LOCATION (yyval.ttype, yyvsp[-1].location);
		    }
		  else
		    yyval.ttype = NULL;
		;
    break;}
case 494:
#line 2122 "c-parse.y"
{ yyval.ttype = c_process_expr_stmt (yyvsp[0].ttype); ;
    break;}
case 495:
#line 2129 "c-parse.y"
{ c_finish_loop (yyvsp[-7].location, yyvsp[-6].ttype, yyvsp[-4].ttype, yyvsp[0].ttype, c_break_label,
				 c_cont_label, true);
		  add_stmt (c_end_compound_stmt (yyvsp[-10].ttype, flag_isoc99));
		  c_break_label = yyvsp[-2].ttype; c_cont_label = yyvsp[-1].ttype; ;
    break;}
case 496:
#line 2137 "c-parse.y"
{ yyval.ttype = c_start_case (yyvsp[-1].exprtype.value); ;
    break;}
case 497:
#line 2139 "c-parse.y"
{ c_finish_case (yyvsp[0].ttype);
		  if (c_break_label)
		    add_stmt (build (LABEL_EXPR, void_type_node,
				     c_break_label));
		  c_break_label = yyvsp[-1].ttype;
		  add_stmt (c_end_compound_stmt (yyvsp[-6].ttype, flag_isoc99)); ;
    break;}
case 498:
#line 2150 "c-parse.y"
{ yyval.ttype = c_finish_expr_stmt (yyvsp[-1].exprtype.value); ;
    break;}
case 499:
#line 2152 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 500:
#line 2154 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 501:
#line 2156 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 502:
#line 2158 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 503:
#line 2160 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 504:
#line 2162 "c-parse.y"
{ yyval.ttype = c_finish_bc_stmt (&c_break_label, true); ;
    break;}
case 505:
#line 2164 "c-parse.y"
{ yyval.ttype = c_finish_bc_stmt (&c_cont_label, false); ;
    break;}
case 506:
#line 2166 "c-parse.y"
{ yyval.ttype = c_finish_return (NULL_TREE); ;
    break;}
case 507:
#line 2168 "c-parse.y"
{ yyval.ttype = c_finish_return (yyvsp[-1].exprtype.value); ;
    break;}
case 509:
#line 2171 "c-parse.y"
{ yyval.ttype = c_finish_goto_label (yyvsp[-1].ttype); ;
    break;}
case 510:
#line 2173 "c-parse.y"
{ yyval.ttype = c_finish_goto_ptr (yyvsp[-1].exprtype.value); ;
    break;}
case 511:
#line 2175 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 512:
#line 2181 "c-parse.y"
{ add_stmt (yyvsp[0].ttype); yyval.ttype = NULL_TREE; ;
    break;}
case 514:
#line 2190 "c-parse.y"
{ yyval.ttype = do_case (yyvsp[-1].exprtype.value, NULL_TREE); ;
    break;}
case 515:
#line 2192 "c-parse.y"
{ yyval.ttype = do_case (yyvsp[-3].exprtype.value, yyvsp[-1].exprtype.value); ;
    break;}
case 516:
#line 2194 "c-parse.y"
{ yyval.ttype = do_case (NULL_TREE, NULL_TREE); ;
    break;}
case 517:
#line 2196 "c-parse.y"
{ tree label = define_label (yyvsp[-2].location, yyvsp[-3].ttype);
		  if (label)
		    {
		      decl_attributes (&label, yyvsp[0].ttype, 0);
		      yyval.ttype = add_stmt (build_stmt (LABEL_EXPR, label));
		    }
		  else
		    yyval.ttype = NULL_TREE;
		;
    break;}
case 518:
#line 2214 "c-parse.y"
{ yyval.ttype = yyvsp[-2].ttype; ;
    break;}
case 519:
#line 2220 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 521:
#line 2227 "c-parse.y"
{ assemble_asm (yyvsp[-1].ttype); ;
    break;}
case 522:
#line 2229 "c-parse.y"
{;
    break;}
case 523:
#line 2237 "c-parse.y"
{ yyval.ttype = build_asm_stmt (yyvsp[-6].ttype, yyvsp[-3].ttype); ;
    break;}
case 524:
#line 2243 "c-parse.y"
{ yyval.ttype = build_asm_expr (yyvsp[0].ttype, 0, 0, 0, true); ;
    break;}
case 525:
#line 2246 "c-parse.y"
{ yyval.ttype = build_asm_expr (yyvsp[-2].ttype, yyvsp[0].ttype, 0, 0, false); ;
    break;}
case 526:
#line 2249 "c-parse.y"
{ yyval.ttype = build_asm_expr (yyvsp[-4].ttype, yyvsp[-2].ttype, yyvsp[0].ttype, 0, false); ;
    break;}
case 527:
#line 2252 "c-parse.y"
{ yyval.ttype = build_asm_expr (yyvsp[-6].ttype, yyvsp[-4].ttype, yyvsp[-2].ttype, yyvsp[0].ttype, false); ;
    break;}
case 528:
#line 2259 "c-parse.y"
{ yyval.ttype = 0; ;
    break;}
case 529:
#line 2261 "c-parse.y"
{ if (yyvsp[0].ttype != ridpointers[RID_VOLATILE])
		    {
		      warning ("%E qualifier ignored on asm", yyvsp[0].ttype);
		      yyval.ttype = 0;
		    }
		  else
		    yyval.ttype = yyvsp[0].ttype;
		;
    break;}
case 530:
#line 2274 "c-parse.y"
{ yyval.ttype = NULL_TREE; ;
    break;}
case 533:
#line 2281 "c-parse.y"
{ yyval.ttype = chainon (yyvsp[-2].ttype, yyvsp[0].ttype); ;
    break;}
case 534:
#line 2287 "c-parse.y"
{ yyval.ttype = build_tree_list (build_tree_list (NULL_TREE, yyvsp[-5].ttype),
					yyvsp[-2].exprtype.value); ;
    break;}
case 535:
#line 2291 "c-parse.y"
{ yyvsp[-7].ttype = build_string (IDENTIFIER_LENGTH (yyvsp[-7].ttype),
				     IDENTIFIER_POINTER (yyvsp[-7].ttype));
		  yyval.ttype = build_tree_list (build_tree_list (yyvsp[-7].ttype, yyvsp[-5].ttype), yyvsp[-2].exprtype.value); ;
    break;}
case 536:
#line 2298 "c-parse.y"
{ yyval.ttype = tree_cons (NULL_TREE, yyvsp[0].ttype, NULL_TREE); ;
    break;}
case 537:
#line 2300 "c-parse.y"
{ yyval.ttype = tree_cons (NULL_TREE, yyvsp[0].ttype, yyvsp[-2].ttype); ;
    break;}
case 538:
#line 2306 "c-parse.y"
{ if (TYPE_MAIN_VARIANT (TREE_TYPE (TREE_TYPE (yyvsp[0].ttype)))
		      != char_type_node)
		    {
		      error ("wide string literal in %<asm%>");
		      yyval.ttype = build_string (1, "");
		    }
		  else
		    yyval.ttype = yyvsp[0].ttype; ;
    break;}
case 539:
#line 2317 "c-parse.y"
{ c_lex_string_translate = 0; ;
    break;}
case 540:
#line 2321 "c-parse.y"
{ c_lex_string_translate = 1; ;
    break;}
case 541:
#line 2332 "c-parse.y"
{ push_scope ();
		  declare_parm_level (); ;
    break;}
case 542:
#line 2335 "c-parse.y"
{ yyval.arginfotype = yyvsp[0].arginfotype;
		  pop_scope (); ;
    break;}
case 544:
#line 2342 "c-parse.y"
{ mark_forward_parm_decls (); ;
    break;}
case 545:
#line 2344 "c-parse.y"
{ /* Dummy action so attributes are in known place
		     on parser stack.  */ ;
    break;}
case 546:
#line 2347 "c-parse.y"
{ yyval.arginfotype = yyvsp[0].arginfotype; ;
    break;}
case 547:
#line 2349 "c-parse.y"
{ yyval.arginfotype = XOBNEW (&parser_obstack, struct c_arg_info);
		  yyval.arginfotype->parms = 0;
		  yyval.arginfotype->tags = 0;
		  yyval.arginfotype->types = 0;
		  yyval.arginfotype->others = 0; ;
    break;}
case 548:
#line 2359 "c-parse.y"
{ yyval.arginfotype = XOBNEW (&parser_obstack, struct c_arg_info);
		  yyval.arginfotype->parms = 0;
		  yyval.arginfotype->tags = 0;
		  yyval.arginfotype->types = 0;
		  yyval.arginfotype->others = 0; ;
    break;}
case 549:
#line 2365 "c-parse.y"
{ yyval.arginfotype = XOBNEW (&parser_obstack, struct c_arg_info);
		  yyval.arginfotype->parms = 0;
		  yyval.arginfotype->tags = 0;
		  yyval.arginfotype->others = 0;
		  /* Suppress -Wold-style-definition for this case.  */
		  yyval.arginfotype->types = error_mark_node;
		  error ("ISO C requires a named argument before %<...%>");
		;
    break;}
case 550:
#line 2374 "c-parse.y"
{ yyval.arginfotype = get_parm_info (/*ellipsis=*/false); ;
    break;}
case 551:
#line 2376 "c-parse.y"
{ yyval.arginfotype = get_parm_info (/*ellipsis=*/true); ;
    break;}
case 552:
#line 2381 "c-parse.y"
{ push_parm_decl (yyvsp[0].parmtype); ;
    break;}
case 553:
#line 2383 "c-parse.y"
{ push_parm_decl (yyvsp[0].parmtype); ;
    break;}
case 554:
#line 2390 "c-parse.y"
{ yyval.parmtype = build_c_parm (current_declspecs,
				     chainon (yyvsp[0].ttype, all_prefix_attributes), yyvsp[-1].dtrtype);
		  POP_DECLSPEC_STACK; ;
    break;}
case 555:
#line 2394 "c-parse.y"
{ yyval.parmtype = build_c_parm (current_declspecs,
				     chainon (yyvsp[0].ttype, all_prefix_attributes), yyvsp[-1].dtrtype);
		  POP_DECLSPEC_STACK; ;
    break;}
case 556:
#line 2398 "c-parse.y"
{ yyval.parmtype = yyvsp[0].parmtype;
		  POP_DECLSPEC_STACK; ;
    break;}
case 557:
#line 2401 "c-parse.y"
{ yyval.parmtype = build_c_parm (current_declspecs,
				     chainon (yyvsp[0].ttype, all_prefix_attributes), yyvsp[-1].dtrtype);
		  POP_DECLSPEC_STACK; ;
    break;}
case 558:
#line 2406 "c-parse.y"
{ yyval.parmtype = yyvsp[0].parmtype;
		  POP_DECLSPEC_STACK; ;
    break;}
case 559:
#line 2414 "c-parse.y"
{ yyval.parmtype = build_c_parm (current_declspecs,
				     chainon (yyvsp[0].ttype, all_prefix_attributes), yyvsp[-1].dtrtype);
		  POP_DECLSPEC_STACK; ;
    break;}
case 560:
#line 2418 "c-parse.y"
{ yyval.parmtype = build_c_parm (current_declspecs,
				     chainon (yyvsp[0].ttype, all_prefix_attributes), yyvsp[-1].dtrtype);
		  POP_DECLSPEC_STACK; ;
    break;}
case 561:
#line 2422 "c-parse.y"
{ yyval.parmtype = yyvsp[0].parmtype;
		  POP_DECLSPEC_STACK; ;
    break;}
case 562:
#line 2425 "c-parse.y"
{ yyval.parmtype = build_c_parm (current_declspecs,
				     chainon (yyvsp[0].ttype, all_prefix_attributes), yyvsp[-1].dtrtype);
		  POP_DECLSPEC_STACK; ;
    break;}
case 563:
#line 2430 "c-parse.y"
{ yyval.parmtype = yyvsp[0].parmtype;
		  POP_DECLSPEC_STACK; ;
    break;}
case 564:
#line 2436 "c-parse.y"
{ prefix_attributes = chainon (prefix_attributes, yyvsp[-3].ttype);
		  all_prefix_attributes = prefix_attributes; ;
    break;}
case 565:
#line 2445 "c-parse.y"
{ push_scope ();
		  declare_parm_level (); ;
    break;}
case 566:
#line 2448 "c-parse.y"
{ yyval.arginfotype = yyvsp[0].arginfotype;
		  pop_scope (); ;
    break;}
case 568:
#line 2455 "c-parse.y"
{ yyval.arginfotype = XOBNEW (&parser_obstack, struct c_arg_info);
		  yyval.arginfotype->parms = 0;
		  yyval.arginfotype->tags = 0;
		  yyval.arginfotype->types = yyvsp[-1].ttype;
		  yyval.arginfotype->others = 0;

		  /* Make sure we have a parmlist after attributes.  */
		  if (yyvsp[-3].ttype != 0)
		    YYERROR1;
		;
    break;}
case 569:
#line 2470 "c-parse.y"
{ yyval.ttype = build_tree_list (NULL_TREE, yyvsp[0].ttype); ;
    break;}
case 570:
#line 2472 "c-parse.y"
{ yyval.ttype = chainon (yyvsp[-2].ttype, build_tree_list (NULL_TREE, yyvsp[0].ttype)); ;
    break;}
case 571:
#line 2478 "c-parse.y"
{ yyval.ttype = build_tree_list (NULL_TREE, yyvsp[0].ttype); ;
    break;}
case 572:
#line 2480 "c-parse.y"
{ yyval.ttype = chainon (yyvsp[-2].ttype, build_tree_list (NULL_TREE, yyvsp[0].ttype)); ;
    break;}
case 573:
#line 2485 "c-parse.y"
{ yyval.itype = SAVE_EXT_FLAGS ();
		  pedantic = 0;
		  warn_pointer_arith = 0;
		  warn_traditional = 0;
		  flag_iso = 0; ;
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
