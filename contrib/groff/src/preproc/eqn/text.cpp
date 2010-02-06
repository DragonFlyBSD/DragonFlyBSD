// -*- C++ -*-
/* Copyright (C) 1989, 1990, 1991, 1992, 2003, 2007, 2009
   Free Software Foundation, Inc.
     Written by James Clark (jjc@jclark.com)

This file is part of groff.

groff is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or
(at your option) any later version.

groff is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include <ctype.h>
#include "eqn.h"
#include "pbox.h"
#include "ptable.h"

struct map {
  const char *from;
  const char *to;
};

struct map entity_table[] = {
  // Classic troff special characters
  {"%", "&shy;"},	// ISOnum
  {"'", "&acute;"},	// ISOdia
  {"!=", "&ne;"},	// ISOtech
  {"**", "&lowast;"},	// ISOtech
  {"*a", "&alpha;"},	// ISOgrk3
  {"*A", "A"},
  {"*b", "&beta;"},	// ISOgrk3
  {"*B", "B"},
  {"*d", "&delta;"},	// ISOgrk3
  {"*D", "&Delta;"},	// ISOgrk3
  {"*e", "&epsilon;"},	// ISOgrk3
  {"*E", "E"},
  {"*f", "&phi;"},	// ISOgrk3
  {"*F", "&Phi;"},	// ISOgrk3
  {"*g", "&gamma;"},	// ISOgrk3
  {"*G", "&Gamma;"},	// ISOgrk3
  {"*h", "&theta;"},	// ISOgrk3
  {"*H", "&Theta;"},	// ISOgrk3
  {"*i", "&iota;"},	// ISOgrk3
  {"*I", "I"},
  {"*k", "&kappa;"},	// ISOgrk3
  {"*K", "K;"},
  {"*l", "&lamda;"},	// ISOgrk3
  {"*L", "&Lambda;"},	// ISOgrk3
  {"*m", "&mu;"},	// ISOgrk3
  {"*M", "M"},
  {"*n", "&nu;"},	// ISOgrk3
  {"*N", "N"},
  {"*o", "o"},
  {"*O", "O"},
  {"*p", "&pi;"},	// ISOgrk3
  {"*P", "&Pi;"},	// ISOgrk3
  {"*q", "&psi;"},	// ISOgrk3
  {"*Q", "&PSI;"},	// ISOgrk3
  {"*r", "&rho;"},	// ISOgrk3
  {"*R", "R"},
  {"*s", "&sigma;"},	// ISOgrk3
  {"*S", "&Sigma;"},	// ISOgrk3
  {"*t", "&tau;"},	// ISOgrk3
  {"*T", "&Tau;"},	// ISOgrk3
  {"*u", "&upsilon;"},	// ISOgrk3
  {"*U", "&Upsilon;"},	// ISOgrk3
  {"*w", "&omega;"},	// ISOgrk3
  {"*W", "&Omega;"},	// ISOgrk3
  {"*x", "&chi;"},	// ISOgrk3
  {"*X", "&Chi;"},	// ISOgrk3
  {"*y", "&eta;"},	// ISOgrk3
  {"*Y", "&Eta;"},	// ISOgrk3
  {"*z", "&zeta;"},	// ISOgrk3
  {"*Z", "&Zeta;"},	// ISOgrk3
  {"+-", "&plusmn;"},	// ISOnum
  {"->", "&rarr;"},	// ISOnum
  {"12", "&frac12;"},	// ISOnum
  {"14", "&frac14;"},	// ISOnum
  {"34", "&frac34;"},	// ISOnum
  {"<-", "&larr;"},	// ISOnum
  {"==", "&equiv;"},	// ISOtech
  {"Fi", "&ffilig;"},	// ISOpub
  {"Fl", "&ffllig;"},	// ISOpub
  {"aa", "&acute;"},	// ISOdia
  {"ap", "&sim;"},	// ISOtech
  {"bl", "&phonexb;"},	// ISOpub
  {"br", "&boxv;"},	// ISObox
  {"bs", "&phone;"},	// ISOpub (for the Bell logo)
  {"bu", "&bull;"},	// ISOpub
  {"bv", "&verbar;"},	// ISOnum
  {"ca", "&cap;"},	// ISOtech
  {"ci", "&cir;"},	// ISOpub
  {"co", "&copy;"},	// ISOnum
  {"ct", "&cent;"},	// ISOnum
  {"cu", "&cup;"},	// ISOtech
  {"da", "&darr;"},	// ISOnum
  {"de", "&deg;"},	// ISOnum
  {"dg", "&dagger;"},	// ISOpub
  {"dd", "&Dagger;"},	// ISOpub
  {"di", "&divide;"},	// ISOnum
  {"em", "&mdash;"},	// ISOpub
  {"eq", "&equals;"},	// ISOnum
  {"es", "&empty;"},	// ISOamso
  {"ff", "&fflig;"},	// ISOpub
  {"fi", "&filig;"},	// ISOpub
  {"fl", "&fllig;"},	// ISOpub
  {"fm", "&prime;"},	// ISOtech
  {"ge", "&ge;"},	// ISOtech
  {"gr", "&nabla;"},	// ISOtech
  {"hy", "&hyphen;"},	// ISOnum
  {"ib", "&sube;"},	// ISOtech
  {"if", "&infin;"},	// ISOtech
  {"ip", "&supe;"},	// ISOtech
  {"is", "&int;"},	// ISOtech
  {"le", "&le;"},	// ISOtech
  // Some pile characters go here
  {"mi", "&minus;"},	// ISOtech
  {"mo", "&isin;"},	// ISOtech
  {"mu", "&times;"},	// ISOnum
  {"no", "&not;"},	// ISOnum
  {"or", "&verbar;"},	// ISOnum
  {"pl", "&plus;"},	// ISOnum
  {"pt", "&prop;"},	// ISOtech
  {"rg", "&trade;"},	// ISOnum
  // More pile characters go here
  {"rn", "&macr;"},	// ISOdia
  {"ru", "&lowbar;"},	// ISOnum
  {"sb", "&sub;"},	// ISOtech
  {"sc", "&sect;"},	// ISOnum
  {"sl", "/"},
  {"sp", "&sup;"},	// ISOtech
  {"sq", "&squf;"},	// ISOpub
  {"sr", "&radic;"},	// ISOtech
  {"ts", "&sigmav;"},	// ISOgrk3
  {"ua", "&uarr;"},	// ISOnum
  {"ul", "_"},
  {"~=", "&cong;"},	// ISOtech
  // Extended specials supported by groff; see groff_char(7).
  // These are listed in the order they occur on that man page.
  {"-D", "&ETH;"},	// ISOlat: Icelandic uppercase eth
  {"Sd", "&eth;"},	// ISOlat1: Icelandic lowercase eth
  {"TP", "&THORN;"},	// ISOlat1: Icelandic uppercase thorn
  {"Tp", "&thorn;"},	// ISOlat1: Icelandic lowercase thorn
  {"ss", "&szlig;"},	// ISOlat1
  // Ligatures
  // ff, fi, fl, ffi, ffl from old troff go here
  {"AE", "&AElig;"},	// ISOlat1
  {"ae", "&aelig;"},	// ISOlat1
  {"OE", "&OElig;"},	// ISOlat2
  {"oe", "&oelig;"},	// ISOlat2
  {"IJ", "&ijlig;"}, 	// ISOlat2: Dutch IJ ligature
  {"ij", "&IJlig;"}, 	// ISOlat2: Dutch ij ligature
  {".i", "&inodot;"},	// ISOlat2,ISOamso
  {".j", "&jnodot;"},	// ISOamso (undocumented but in 1.19)
  // Accented characters
  {"'A", "&Aacute;"},	// ISOlat1
  {"'C", "&Cacute;"},	// ISOlat2
  {"'E", "&Eacute;"},	// ISOlat1
  {"'I", "&Iacute;"},	// ISOlat1
  {"'O", "&Oacute;"},	// ISOlat1
  {"'U", "&Uacute;"},	// ISOlat1
  {"'Y", "&Yacute;"},	// ISOlat1
  {"'a", "&aacute;"},	// ISOlat1
  {"'c", "&cacute;"},	// ISOlat2
  {"'e", "&eacute;"},	// ISOlat1
  {"'i", "&iacute;"},	// ISOlat1
  {"'o", "&oacute;"},	// ISOlat1
  {"'u", "&uacute;"},	// ISOlat1
  {"'y", "&yacute;"},	// ISOlat1
  {":A", "&Auml;"},	// ISOlat1
  {":E", "&Euml;"},	// ISOlat1
  {":I", "&Iuml;"},	// ISOlat1
  {":O", "&Ouml;"},	// ISOlat1
  {":U", "&Uuml;"},	// ISOlat1
  {":Y", "&Yuml;"},	// ISOlat2
  {":a", "&auml;"},	// ISOlat1
  {":e", "&euml;"},	// ISOlat1
  {":i", "&iuml;"},	// ISOlat1
  {":o", "&ouml;"},	// ISOlat1
  {":u", "&uuml;"},	// ISOlat1
  {":y", "&yuml;"},	// ISOlat1
  {"^A", "&Acirc;"},	// ISOlat1
  {"^E", "&Ecirc;"},	// ISOlat1
  {"^I", "&Icirc;"},	// ISOlat1
  {"^O", "&Ocirc;"},	// ISOlat1
  {"^U", "&Ucirc;"},	// ISOlat1
  {"^a", "&acirc;"},	// ISOlat1
  {"^e", "&ecirc;"},	// ISOlat1
  {"^i", "&icirc;"},	// ISOlat1
  {"^o", "&ocirc;"},	// ISOlat1
  {"^u", "&ucirc;"},	// ISOlat1
  {"`A", "&Agrave;"},	// ISOlat1
  {"`E", "&Egrave;"},	// ISOlat1
  {"`I", "&Igrave;"},	// ISOlat1
  {"`O", "&Ograve;"},	// ISOlat1
  {"`U", "&Ugrave;"},	// ISOlat1
  {"`a", "&agrave;"},	// ISOlat1
  {"`e", "&egrave;"},	// ISOlat1
  {"`i", "&igrave;"},	// ISOlat1
  {"`o", "&ograve;"},	// ISOlat1
  {"`u", "&ugrave;"},	// ISOlat1
  {"~A", "&Atilde;"},	// ISOlat1
  {"~N", "&Ntilde;"},	// ISOlat1
  {"~O", "&Otilde;"},	// ISOlat1
  {"~a", "&atilde;"},	// ISOlat1
  {"~n", "&ntilde;"},	// ISOlat1
  {"~o", "&otilde;"},	// ISOlat1
  {"vS", "&Scaron;"},	// ISOlat2
  {"vs", "&scaron;"},	// ISOlat2
  {"vZ", "&Zcaron;"},	// ISOlat2
  {"vz", "&zcaron;"},	// ISOlat2
  {",C", "&Ccedil;"},	// ISOlat1
  {",c", "&ccedil;"},	// ISOlat1
  {"/L", "&Lstrok;"},	// ISOlat2: Polish L with a slash
  {"/l", "&lstrok;"},	// ISOlat2: Polish l with a slash
  {"/O", "&Oslash;"},	// ISOlat1
  {"/o", "&oslash;"},	// ISOlat1
  {"oA", "&Aring;"},	// ISOlat1
  {"oa", "&aring;"},	// ISOlat1
  // Accents
  {"a\"","&dblac;"},	// ISOdia: double acute accent (Hungarian umlaut)
  {"a-", "&macr;"},	// ISOdia: macron or bar accent
  {"a.", "&dot;"},	// ISOdia: dot above
  {"a^", "&circ;"},	// ISOdia: circumflex accent
  {"aa", "&acute;"},	// ISOdia: acute accent
  {"ga", "&grave;"},	// ISOdia: grave accent
  {"ab", "&breve;"},	// ISOdia: breve accent
  {"ac", "&cedil;"},	// ISOdia: cedilla accent
  {"ad", "&uml;"},	// ISOdia: umlaut or dieresis
  {"ah", "&caron;"},	// ISOdia: caron (aka hacek accent)
  {"ao", "&ring;"},	// ISOdia: ring or circle accent
  {"a~", "&tilde;"},	// ISOdia: tilde accent
  {"ho", "&ogon;"},	// ISOdia: hook or ogonek accent
  {"ha", "^"},		// ASCII circumflex, hat, caret
  {"ti", "~"},		// ASCII tilde, large tilde
  // Quotes
  {"Bq", "&lsquor;"},	// ISOpub: low double comma quote
  {"bq", "&ldquor;"},	// ISOpub: low single comma quote
  {"lq", "&ldquo;"},	// ISOnum
  {"rq", "&rdquo;"},	// ISOpub
  {"oq", "&lsquo;"},	// ISOnum: single open quote
  {"cq", "&rsquo;"},	// ISOnum: single closing quote (ASCII 39)
  {"aq", "&zerosp;'"},	// apostrophe quote
  {"dq", "\""},		// double quote (ASCII 34)
  {"Fo", "&laquo;"},	// ISOnum
  {"Fc", "&raquo;"},	// ISOnum
  //{"fo", "&fo;"},
  //{"fc", "&fc;"},
  // Punctuation
  {"r!", "&iexcl;"},	// ISOnum
  {"r?", "&iquest;"},	// ISOnum
  // Old troff \(em goes here
  {"en", "&ndash;"},	// ISOpub: en dash
  // Old troff \(hy goes here 
  // Brackets
  {"lB", "&lsqb;"},	// ISOnum: left (square) bracket
  {"rB", "&rsqb;"},	// ISOnum: right (square) bracket
  {"lC", "&lcub;"},	// ISOnum: left (curly) brace
  {"rC", "&rcub;"},	// ISOnum: right (curly) brace
  {"la", "&lang;"},	// ISOtech: left angle bracket
  {"ra", "&rang;"},	// ISOtech: right angle bracket
  // Old troff \(bv goes here
  // Bracket-pile characters could go here.
  // Arrows
  // Old troff \(<- and \(-> go here
  {"<>", "&harr;"},	// ISOamsa
  {"da", "&darr;"},	// ISOnum
  {"ua", "&uarr;"},	// ISOnum
  {"lA", "&lArr;"},	// ISOtech
  {"rA", "&rArr;"},	// ISOtech
  {"hA", "&iff;"},	// ISOtech: horizontal double-headed arrow
  {"dA", "&dArr;"},	// ISOamsa
  {"uA", "&uArr;"},	// ISOamsa
  {"vA", "&vArr;"}, 	// ISOamsa: vertical double-headed double arrow
  //{"an", "&an;"},
  // Lines
  {"-h", "&planck;"},	// ISOamso: h-bar (Planck's constant)
  // Old troff \(or goes here
  {"ba", "&verbar;"},	// ISOnum
  // Old troff \(br, \{u, \(ul, \(bv go here
  {"bb", "&brvbar;"},	// ISOnum
  {"sl", "/"},
  {"rs", "&bsol;"},	// ISOnum
  // Text markers
  // Old troff \(ci, \(bu, \(dd, \(dg go here
  {"lz", "&loz;"},	// ISOpub
  // Old troff sq goes here
  {"ps", "&para;"},	// ISOnum: paragraph or pilcrow sign
  {"sc", "&sect;"},	// ISOnum (in old troff)
  // Old troff \(lh, \{h go here
  {"at", "&commat;"},	// ISOnum
  {"sh", "&num;"},	// ISOnum
  //{"CR", "&CR;"},
  {"OK", "&check;"},	// ISOpub
  // Legalize
  // Old troff \(co, \{g go here
  {"tm", "&trade;"},	// ISOnum
  // Currency symbols
  {"Do", "&dollar;"},	// ISOnum
  {"ct", "&cent;"},	// ISOnum
  {"eu", "&euro;"},
  {"Eu", "&euro;"},
  {"Ye", "&yen;"},	// ISOnum
  {"Po", "&pound;"},	// ISOnum
  {"Cs", "&curren;"},	// ISOnum: currency sign
  {"Fn", "&fnof"},	// ISOtech
  // Units
  // Old troff de goes here
  {"%0", "&permil;"},	// ISOtech: per thousand, per mille sign
  // Old troff \(fm goes here
  {"sd", "&Prime;"},	// ISOtech
  {"mc", "&micro;"},	// ISOnum
  {"Of", "&ordf;"},	// ISOnum
  {"Om", "&ordm;"},	// ISOnum
  // Logical symbols
  {"AN", "&and;"},	// ISOtech
  {"OR", "&or;"},	// ISOtech
  // Old troff \(no goes here
  {"te", "&exist;"}, 	// ISOtech: there exists, existential quantifier
  {"fa", "&forall;"}, 	// ISOtech: for all, universal quantifier
  {"st", "&bepsi"},	// ISOamsr: such that
  {"3d", "&there4;"},	// ISOtech
  {"tf", "&there4;"},	// ISOtech
  // Mathematical symbols
  // Old troff "12", "14", "34" goes here
  {"S1", "&sup1;"},	// ISOnum
  {"S2", "&sup2;"},	// ISOnum
  {"S3", "&sup3;"},	// ISOnum
  // Old troff \(pl", \-, \(+- go here
  {"t+-", "&plusmn;"},	// ISOnum
  {"-+", "&mnplus;"},	// ISOtech
  {"pc", "&middot;"},	// ISOnum
  {"md", "&middot;"},	// ISOnum
  // Old troff \(mu goes here
  {"tmu", "&times;"},	// ISOnum
  {"c*", "&otimes;"},	// ISOamsb: multiply sign in a circle
  {"c+", "&oplus;"},	// ISOamsb: plus sign in a circle
  // Old troff \(di goes here
  {"tdi", "&divide;"},	// ISOnum
  {"f/", "&horbar;"},	// ISOnum: horizintal bar for fractions
  // Old troff \(** goes here
  {"<=", "&le;"},	// ISOtech
  {">=", "&ge;"},	// ISOtech
  {"<<", "&Lt;"},	// ISOamsr
  {">>", "&Gt;"},	// ISOamsr
  {"!=", "&ne;"},	// ISOtech
  // Old troff \(eq and \(== go here
  {"=~", "&cong;"},	// ISOamsr
  // Old troff \(ap goes here
  {"~~", "&ap;"},	// ISOtech
  // This appears to be an error in the groff table.  
  // It clashes with the Bell Labs use of ~= for a congruence sign
  // {"~=", "&ap;"},	// ISOamsr
  // Old troff \(pt, \(es, \(mo go here
  {"nm", "&notin;"},	// ISOtech
  {"nb", "&nsub;"},	// ISOamsr
  {"nc", "&nsup;"},	// ISOamsn
  {"ne", "&nequiv;"},	// ISOamsn
  // Old troff \(sb, \(sp, \(ib, \(ip, \(ca, \(cu go here
  {"/_", "&ang;"},	// ISOamso
  {"pp", "&perp;"},	// ISOtech
  // Old troff \(is goes here
  {"sum", "&sum;"},	// ISOamsb
  {"product", "&prod;"},	// ISOamsb
  {"gr", "&nabla;"},	// ISOtech
  // Old troff \(sr. \{n, \(if go here
  {"Ah", "&aleph;"},	// ISOtech
  {"Im", "&image;"},	// ISOamso: Fraktur I, imaginary
  {"Re", "&real;"},	// ISOamso: Fraktur R, real
  {"wp", "&weierp;"},	// ISOamso
  {"pd", "&part;"},	// ISOtech: partial differentiation sign
  // Their table duplicates the Greek letters here.
  // We list only the variant forms here, mapping them into
  // the ISO Greek 4 variants (which may or may not be correct :-() 
  {"+f", "&b.phiv;"},	// ISOgrk4: variant phi
  {"+h", "&b.thetas;"},	// ISOgrk4: variant theta
  {"+p", "&b.omega;"},	// ISOgrk4: variant pi, looking like omega
  // Card symbols
  {"CL", "&clubs;"},	// ISOpub: club suit
  {"SP", "&spades;"},	// ISOpub: spade suit
  {"HE", "&hearts;"},	// ISOpub: heart suit
  {"DI", "&diams;"},	// ISOpub: diamond suit
};

const char *special_to_entity(const char *sp)
{
  struct map *mp;
  for (mp = entity_table; 
       mp < entity_table + sizeof(entity_table)/sizeof(entity_table[0]); 
       mp++) {
    if (strcmp(mp->from, sp) == 0)
      return mp->to;
  }
  return NULL;
}

class char_box : public simple_box {
  unsigned char c;
  char next_is_italic;
  char prev_is_italic;
public:
  char_box(unsigned char);
  void debug_print();
  void output();
  int is_char();
  int left_is_italic();
  int right_is_italic();
  void hint(unsigned);
  void handle_char_type(int, int);
};

class special_char_box : public simple_box {
  char *s;
public:
  special_char_box(const char *);
  ~special_char_box();
  void output();
  void debug_print();
  int is_char();
  void handle_char_type(int, int);
};

enum spacing_type {
  s_ordinary,
  s_operator,
  s_binary,
  s_relation,
  s_opening,
  s_closing,
  s_punctuation,
  s_inner,
  s_suppress
};

const char *spacing_type_table[] = {
  "ordinary",
  "operator",
  "binary",
  "relation",
  "opening",
  "closing",
  "punctuation",
  "inner",
  "suppress",
  0,
};

const int DIGIT_TYPE = 0;
const int LETTER_TYPE = 1;

const char *font_type_table[] = {
  "digit",
  "letter",
  0,
};

struct char_info {
  int spacing_type;
  int font_type;
  char_info();
};

char_info::char_info()
: spacing_type(ORDINARY_TYPE), font_type(DIGIT_TYPE)
{
}

static char_info char_table[256];

declare_ptable(char_info)
implement_ptable(char_info)

PTABLE(char_info) special_char_table;

static int get_special_char_spacing_type(const char *ch)
{
  char_info *p = special_char_table.lookup(ch);
  return p ? p->spacing_type : ORDINARY_TYPE;
}

static int get_special_char_font_type(const char *ch)
{
  char_info *p = special_char_table.lookup(ch);
  return p ? p->font_type : DIGIT_TYPE;
}

static void set_special_char_type(const char *ch, int st, int ft)
{
  char_info *p = special_char_table.lookup(ch);
  if (!p) {
    p = new char_info[1];
    special_char_table.define(ch, p);
  }
  if (st >= 0)
    p->spacing_type = st;
  if (ft >= 0)
    p->font_type = ft;
}

void init_char_table()
{
  set_special_char_type("pl", s_binary, -1);
  set_special_char_type("mi", s_binary, -1);
  set_special_char_type("eq", s_relation, -1);
  set_special_char_type("<=", s_relation, -1);
  set_special_char_type(">=", s_relation, -1);
  char_table['}'].spacing_type = s_closing;
  char_table[')'].spacing_type = s_closing;
  char_table[']'].spacing_type = s_closing;
  char_table['{'].spacing_type = s_opening;
  char_table['('].spacing_type = s_opening;
  char_table['['].spacing_type = s_opening;
  char_table[','].spacing_type = s_punctuation;
  char_table[';'].spacing_type = s_punctuation;
  char_table[':'].spacing_type = s_punctuation;
  char_table['.'].spacing_type = s_punctuation;
  char_table['>'].spacing_type = s_relation;
  char_table['<'].spacing_type = s_relation;
  char_table['*'].spacing_type = s_binary;
  for (int i = 0; i < 256; i++)
    if (csalpha(i))
      char_table[i].font_type = LETTER_TYPE;
}

static int lookup_spacing_type(const char *type)
{
  for (int i = 0; spacing_type_table[i] != 0; i++)
    if (strcmp(spacing_type_table[i], type) == 0)
      return i;
  return -1;
}

static int lookup_font_type(const char *type)
{
  for (int i = 0; font_type_table[i] != 0; i++)
    if (strcmp(font_type_table[i], type) == 0)
      return i;
  return -1;
}

void box::set_spacing_type(char *type)
{
  int t = lookup_spacing_type(type);
  if (t < 0)
    error("unrecognised type `%1'", type);
  else
    spacing_type = t;
  a_delete type;
}

char_box::char_box(unsigned char cc)
: c(cc), next_is_italic(0), prev_is_italic(0)
{
  spacing_type = char_table[c].spacing_type;
}

void char_box::hint(unsigned flags)
{
  if (flags & HINT_PREV_IS_ITALIC)
    prev_is_italic = 1;
  if (flags & HINT_NEXT_IS_ITALIC)
    next_is_italic = 1;
}

void char_box::output()
{
  if (output_format == troff) {
    int font_type = char_table[c].font_type;
    if (font_type != LETTER_TYPE)
      printf("\\f[%s]", current_roman_font);
    if (!prev_is_italic)
      fputs("\\,", stdout);
    if (c == '\\')
      fputs("\\e", stdout);
    else
      putchar(c);
    if (!next_is_italic)
      fputs("\\/", stdout);
    else
      fputs("\\&", stdout);		// suppress ligaturing and kerning
    if (font_type != LETTER_TYPE)
      fputs("\\fP", stdout);
  }
  else if (output_format == mathml) {
    if (isdigit(c))
      printf("<mn>");
    else if (char_table[c].spacing_type)
      printf("<mo>");
    else
      printf("<mi>");
    if (c == '<')
      printf("&lt;");
    else if (c == '>')
      printf("&gt;");
    else if (c == '&')
      printf("&amp;");
    else
      putchar(c);
    if (isdigit(c))
      printf("</mn>");
    else if (char_table[c].spacing_type)
      printf("</mo>");
    else
      printf("</mi>");
  }
}

int char_box::left_is_italic()
{
  int font_type = char_table[c].font_type;
  return font_type == LETTER_TYPE;
}

int char_box::right_is_italic()
{
  int font_type = char_table[c].font_type;
  return font_type == LETTER_TYPE;
}

int char_box::is_char()
{
  return 1;
}

void char_box::debug_print()
{
  if (c == '\\') {
    putc('\\', stderr);
    putc('\\', stderr);
  }
  else
    putc(c, stderr);
}

special_char_box::special_char_box(const char *t)
{
  s = strsave(t);
  spacing_type = get_special_char_spacing_type(s);
}

special_char_box::~special_char_box()
{
  a_delete s;
}

void special_char_box::output()
{
  if (output_format == troff) {
    int font_type = get_special_char_font_type(s);
    if (font_type != LETTER_TYPE)
      printf("\\f[%s]", current_roman_font);
    printf("\\,\\[%s]\\/", s);
    if (font_type != LETTER_TYPE)
      printf("\\fP");
  }
  else if (output_format == mathml) {
    const char *entity = special_to_entity(s);
    if (entity != NULL)
      printf("<mo>%s</mo>", entity);
    else
      printf("<merror>unknown eqn/troff special char %s</merror>", s);
  }
}

int special_char_box::is_char()
{
  return 1;
}

void special_char_box::debug_print()
{
  fprintf(stderr, "\\[%s]", s);
}


void char_box::handle_char_type(int st, int ft)
{
  if (st >= 0)
    char_table[c].spacing_type = st;
  if (ft >= 0)
    char_table[c].font_type = ft;
}

void special_char_box::handle_char_type(int st, int ft)
{
  set_special_char_type(s, st, ft);
}

void set_char_type(const char *type, char *ch)
{
  assert(ch != 0);
  int st = lookup_spacing_type(type);
  int ft = lookup_font_type(type);
  if (st < 0 && ft < 0) {
    error("bad character type `%1'", type);
    a_delete ch;
    return;
  }
  box *b = split_text(ch);
  b->handle_char_type(st, ft);
  delete b;
}

/* We give primes special treatment so that in ``x' sub 2'', the ``2''
will be tucked under the prime */

class prime_box : public pointer_box {
  box *pb;
public:
  prime_box(box *);
  ~prime_box();
  int compute_metrics(int style);
  void output();
  void compute_subscript_kern();
  void debug_print();
  void handle_char_type(int, int);
};

box *make_prime_box(box *pp)
{
  return new prime_box(pp);
}

prime_box::prime_box(box *pp) : pointer_box(pp)
{
  pb = new special_char_box("fm");
}

prime_box::~prime_box()
{
  delete pb;
}

int prime_box::compute_metrics(int style)
{
  int res = p->compute_metrics(style);
  pb->compute_metrics(style);
  printf(".nr " WIDTH_FORMAT " 0\\n[" WIDTH_FORMAT "]"
	 "+\\n[" WIDTH_FORMAT "]\n",
	 uid, p->uid, pb->uid);
  printf(".nr " HEIGHT_FORMAT " \\n[" HEIGHT_FORMAT "]"
	 ">?\\n[" HEIGHT_FORMAT "]\n",
	 uid, p->uid, pb->uid);
  printf(".nr " DEPTH_FORMAT " \\n[" DEPTH_FORMAT "]"
	 ">?\\n[" DEPTH_FORMAT "]\n",
	 uid, p->uid, pb->uid);
  return res;
}

void prime_box::compute_subscript_kern()
{
  p->compute_subscript_kern();
  printf(".nr " SUB_KERN_FORMAT " 0\\n[" WIDTH_FORMAT "]"
	 "+\\n[" SUB_KERN_FORMAT "]>?0\n",
	 uid, pb->uid, p->uid);
}

void prime_box::output()
{
  p->output();
  pb->output();
}

void prime_box::handle_char_type(int st, int ft)
{
  p->handle_char_type(st, ft);
  pb->handle_char_type(st, ft);
}

void prime_box::debug_print()
{
  p->debug_print();
  putc('\'', stderr);
}

box *split_text(char *text)
{
  list_box *lb = 0;
  box *fb = 0;
  char *s = text;
  while (*s != '\0') {
    char c = *s++;
    box *b = 0;
    switch (c) {
    case '+':
      b = new special_char_box("pl");
      break;
    case '-':
      b = new special_char_box("mi");
      break;
    case '=':
      b = new special_char_box("eq");
      break;
    case '\'':
      b = new special_char_box("fm");
      break;
    case '<':
      if (*s == '=') {
	b = new special_char_box("<=");
	s++;
	break;
      }
      goto normal_char;
    case '>':
      if (*s == '=') {
	b = new special_char_box(">=");
	s++;
	break;
      }
      goto normal_char;
    case '\\':
      if (*s == '\0') {
	lex_error("bad escape");
	break;
      }
      c = *s++;
      switch (c) {
      case '(':
	{
	  char buf[3];
	  if (*s != '\0') {
	    buf[0] = *s++;
	    if (*s != '\0') {
	      buf[1] = *s++;
	      buf[2] = '\0';
	      b = new special_char_box(buf);
	    }
	    else {
	      lex_error("bad escape");
	    }
	  }
	  else {
	    lex_error("bad escape");
	  }
	}
	break;
      case '[':
	{
	  char *ch = s;
	  while (*s != ']' && *s != '\0')
	    s++;
	  if (*s == '\0')
	    lex_error("bad escape");
	  else {
	    *s++ = '\0';
	    b = new special_char_box(ch);
	  }
	}
	break;
      case 'f':
      case 'g':
      case 'k':
      case 'n':
      case '*':
	{
	  char *escape_start = s - 2;
	  switch (*s) {
	  case '(':
	    if (*++s != '\0')
	      ++s;
	    break;
	  case '[':
	    for (++s; *s != '\0' && *s != ']'; s++)
	      ;
	    break;
	  }
	  if (*s == '\0')
	    lex_error("bad escape");
	  else {
	    ++s;
	    char *buf = new char[s - escape_start + 1];
	    memcpy(buf, escape_start, s - escape_start);
	    buf[s - escape_start] = '\0';
	    b = new quoted_text_box(buf);
	  }
	}
	break;
      case '-':
      case '_':
	{
	  char buf[2];
	  buf[0] = c;
	  buf[1] = '\0';
	  b = new special_char_box(buf);
	}
	break;
      case '`':
	b = new special_char_box("ga");
	break;
      case '\'':
	b = new special_char_box("aa");
	break;
      case 'e':
      case '\\':
	b = new char_box('\\');
	break;
      case '^':
      case '|':
      case '0':
	{
	  char buf[3];
	  buf[0] = '\\';
	  buf[1] = c;
	  buf[2] = '\0';
	  b = new quoted_text_box(strsave(buf));
	  break;
	}
      default:
	lex_error("unquoted escape");
	b = new quoted_text_box(strsave(s - 2));
	s = strchr(s, '\0');
	break;
      }
      break;
    default:
    normal_char:
      b = new char_box(c);
      break;
    }
    while (*s == '\'') {
      if (b == 0)
	b = new quoted_text_box(0);
      b = new prime_box(b);
      s++;
    }
    if (b != 0) {
      if (lb != 0)
	lb->append(b);
      else if (fb != 0) {
	lb = new list_box(fb);
	lb->append(b);
      }
      else
	fb = b;
    }
  }
  a_delete text;
  if (lb != 0)
    return lb;
  else if (fb != 0)
    return fb;
  else
    return new quoted_text_box(0);
}

