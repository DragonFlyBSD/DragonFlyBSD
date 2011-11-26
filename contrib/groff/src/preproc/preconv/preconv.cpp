// -*- C++ -*-
/* Copyright (C) 2005, 2006, 2008, 2009
   Free Software Foundation, Inc.
     Written by Werner Lemberg (wl@gnu.org)

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

#include "lib.h"

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include "errarg.h"
#include "error.h"
#include "localcharset.h"
#include "nonposix.h"
#include "stringclass.h"

#include <locale.h>

#if HAVE_ICONV
# include <iconv.h>
# ifdef WORDS_BIGENDIAN
#  define UNICODE "UTF-32BE"
# else
#  define UNICODE "UTF-32LE"
# endif
#endif

#define MAX_VAR_LEN 100

extern "C" const char *Version_string;

char default_encoding[MAX_VAR_LEN];
char user_encoding[MAX_VAR_LEN];
char encoding_string[MAX_VAR_LEN];
int debug_flag = 0;
int raw_flag = 0;

struct conversion {
  const char *from;
  const char *to;
};

// The official list of MIME tags can be found at
//
//   http://www.iana.org/assignments/character-sets
//
// For encodings which don't have a MIME tag we use GNU iconv's encoding
// names (which also work with the portable GNU libiconv package).  They
// are marked with `*'.
//
// Encodings specific to XEmacs and Emacs are marked as such; no mark means
// that they are used by both Emacs and XEmacs.
//
// Encodings marked with `--' are special to Emacs, XEmacs, or other
// applications and shouldn't be used for data exchange.
//
// `Not covered' means that the encoding can be handled neither by GNU iconv
// nor by libiconv, or just one of them has support for it.
//
// A special case is VIQR encoding: Despite of having a MIME tag it is
// missing in both libiconv 1.10 and iconv (coming with GNU libc 2.3.6).
//
// Finally, we add all aliases of GNU iconv for `ascii', `latin1', and
// `utf8' to catch those encoding names before iconv is called.
//
// Note that most entries are commented out -- only a small, (rather)
// reliable and stable subset of encodings is recognized (for coding tags)
// which are still in greater use today (January 2006).  Most notably, all
// Windows-specific encodings are not selected because they lack stability:
// Microsoft has changed the mappings instead of creating new versions.
//
// Please contact the groff list if you find the selection inadequate.

static const conversion
emacs_to_mime[] = {
  {"ascii",				"US-ASCII"},	// Emacs
  {"big5",				"Big5"},
  {"chinese-big5",			"Big5"},	// Emacs
  {"chinese-euc",			"GB2312"},	// XEmacs
  {"chinese-iso-8bit",			"GB2312"},	// Emacs
  {"cn-big5",				"Big5"},
  {"cn-gb",				"GB2312"},	// Emacs
  {"cn-gb-2312",			"GB2312"},
  {"cp878",				"KOI8-R"},	// Emacs
  {"cp1047",				"CP1047"},	// EBCDIC
  {"csascii",				"US-ASCII"},	// alias
  {"csisolatin1",			"ISO-8859-1"},	// alias
  {"cyrillic-iso-8bit",			"ISO-8859-5"},	// Emacs
  {"cyrillic-koi8",			"KOI8-R"},	// not KOI8!, Emacs
  {"euc-china",				"GB2312"},	// Emacs
  {"euc-cn",				"GB2312"},	// Emacs
  {"euc-japan",				"EUC-JP"},
  {"euc-japan-1990",			"EUC-JP"},	// Emacs
  {"euc-jp",				"EUC-JP"},
  {"euc-korea",				"EUC-KR"},
  {"euc-kr",				"EUC-KR"},
  {"gb2312",				"GB2312"},
  {"greek-iso-8bit",			"ISO-8859-7"},
  {"iso-10646/utf8",			"UTF-8"},	// alias
  {"iso-10646/utf-8",			"UTF-8"},	// alias
  {"iso-8859-1",			"ISO-8859-1"},
  {"iso-8859-13",			"ISO-8859-13"},	// Emacs
  {"iso-8859-15",			"ISO-8859-15"},
  {"iso-8859-2",			"ISO-8859-2"},
  {"iso-8859-5",			"ISO-8859-5"},
  {"iso-8859-7",			"ISO-8859-7"},
  {"iso-8859-9",			"ISO-8859-9"},
  {"iso-latin-1",			"ISO-8859-1"},
  {"iso-latin-2",			"ISO-8859-2"},	// Emacs
  {"iso-latin-5",			"ISO-8859-9"},	// Emacs
  {"iso-latin-7",			"ISO-8859-13"},	// Emacs
  {"iso-latin-9",			"ISO-8859-15"},	// Emacs
  {"japanese-iso-8bit",			"EUC-JP"},	// Emacs
  {"japanese-euc",			"EUC-JP"},	// XEmacs
  {"jis8",				"EUC-JP"},	// XEmacs
  {"koi8",				"KOI8-R"},	// not KOI8!, Emacs
  {"koi8-r",				"KOI8-R"},
  {"korean-euc",			"EUC-KR"},	// XEmacs
  {"korean-iso-8bit",			"EUC-KR"},	// Emacs
  {"latin1",				"ISO-8859-1"},  // alias
  {"latin-0",				"ISO-8859-15"},	// Emacs
  {"latin-1",				"ISO-8859-1"},	// Emacs
  {"latin-2",				"ISO-8859-2"},	// Emacs
  {"latin-5",				"ISO-8859-9"},	// Emacs
  {"latin-7",				"ISO-8859-13"},	// Emacs
  {"latin-9",				"ISO-8859-15"},	// Emacs
  {"mule-utf-16",			"UTF-16"},	// Emacs
  {"mule-utf-16be",			"UTF-16BE"},	// Emacs
  {"mule-utf-16-be",			"UTF-16BE"},	// Emacs
  {"mule-utf-16be-with-signature",	"UTF-16"},	// Emacs, not UTF-16BE
  {"mule-utf-16le",			"UTF-16LE"},	// Emacs
  {"mule-utf-16-le",			"UTF-16LE"},	// Emacs
  {"mule-utf-16le-with-signature",	"UTF-16"},	// Emacs, not UTF-16LE
  {"mule-utf-8",			"UTF-8"},	// Emacs
  {"us-ascii",				"US-ASCII"},	// Emacs
  {"utf8",				"UTF-8"},	// alias
  {"utf-16",				"UTF-16"},	// Emacs
  {"utf-16be",				"UTF-16BE"},	// Emacs
  {"utf-16-be",				"UTF-16BE"},	// Emacs
  {"utf-16be-with-signature",		"UTF-16"},	// Emacs, not UTF-16BE
  {"utf-16-be-with-signature",		"UTF-16"},	// Emacs, not UTF-16BE
  {"utf-16le",				"UTF-16LE"},	// Emacs
  {"utf-16-le",				"UTF-16LE"},	// Emacs
  {"utf-16le-with-signature",		"UTF-16"},	// Emacs, not UTF-16LE
  {"utf-16-le-with-signature",		"UTF-16"},	// Emacs, not UTF-16LE
  {"utf-8",				"UTF-8"},	// Emacs

//  {"alternativnyj",			""},		// ?
//  {"arabic-iso-8bit",			"ISO-8859-6"},	// Emacs
//  {"binary",				""},		// --
//  {"chinese-hz",			"HZ-GB-2312"},	// Emacs
//  {"chinese-iso-7bit",		"ISO-2022-CN"},	// Emacs
//  {"chinese-iso-8bit-with-esc",	""},		// --
//  {"compound-text",			""},		// --
//  {"compound-text-with-extension",	""},		// --
//  {"cp1125",				"cp1125"},	// *
//  {"cp1250",				"windows-1250"},// Emacs
//  {"cp1251",				"windows-1251"},// Emacs
//  {"cp1252",				"windows-1252"},// Emacs
//  {"cp1253",				"windows-1253"},// Emacs
//  {"cp1254",				"windows-1254"},// Emacs
//  {"cp1255",				"windows-1255"},// Emacs
//  {"cp1256",				"windows-1256"},// Emacs
//  {"cp1257",				"windows-1257"},// Emacs
//  {"cp1258",				"windows-1258"},// Emacs
//  {"cp437",				"cp437"},	// Emacs
//  {"cp720",				""},		// not covered
//  {"cp737",				"cp737"},	// *, Emacs
//  {"cp775",				"cp775"},	// Emacs
//  {"cp850",				"cp850"},	// Emacs
//  {"cp851",				"cp851"},	// Emacs
//  {"cp852",				"cp852"},	// Emacs
//  {"cp855",				"cp855"},	// Emacs
//  {"cp857",				"cp857"},	// Emacs
//  {"cp860",				"cp860"},	// Emacs
//  {"cp861",				"cp861"},	// Emacs
//  {"cp862",				"cp862"},	// Emacs
//  {"cp863",				"cp863"},	// Emacs
//  {"cp864",				"cp864"},	// Emacs
//  {"cp865",				"cp865"},	// Emacs
//  {"cp866",				"cp866"},	// Emacs
//  {"cp866u",				"cp1125"},	// *, Emacs
//  {"cp869",				"cp869"},	// Emacs
//  {"cp874",				"cp874"},	// *, Emacs
//  {"cp932",				"cp932"},	// *, Emacs
//  {"cp936",				"cp936"},	// Emacs
//  {"cp949",				"cp949"},	// *, Emacs
//  {"cp950",				"cp950"},	// *, Emacs
//  {"ctext",				""},		// --
//  {"ctext-no-compositions",		""},		// --
//  {"ctext-with-extensions",		""},		// --
//  {"cyrillic-alternativnyj",		""},		// ?, Emacs
//  {"cyrillic-iso-8bit-with-esc",	""},		// --
//  {"cyrillic-koi8-t",			"KOI8-T"},	// *, Emacs
//  {"devanagari",			""},		// not covered
//  {"dos",				""},		// --
//  {"emacs-mule",			""},		// --
//  {"euc-jisx0213",			"EUC-JISX0213"},// *, XEmacs?
//  {"euc-jisx0213-with-esc",		""},		// XEmacs?
//  {"euc-taiwan",			"EUC-TW"},	// *, Emacs
//  {"euc-tw",				"EUC-TW"},	// *, Emacs
//  {"georgian-ps",			"GEORGIAN-PS"},	// *, Emacs
//  {"greek-iso-8bit-with-esc",		""},		// --
//  {"hebrew-iso-8bit",			"ISO-8859-8"},	// Emacs
//  {"hebrew-iso-8bit-with-esc",	""},		// --
//  {"hz",				"HZ-GB-2312"},
//  {"hz-gb-2312",			"HZ-GB-2312"},
//  {"in-is13194",			""},		// not covered
//  {"in-is13194-devanagari",		""},		// not covered
//  {"in-is13194-with-esc",		""},		// --
//  {"iso-2022-7",			""},		// XEmacs?
//  {"iso-2022-7bit",			""},		// --
//  {"iso-2022-7bit-lock",		""},		// --
//  {"iso-2022-7bit-lock-ss2",		""},		// --
//  {"iso-2022-7bit-ss2",		""},		// --
//  {"iso-2022-8",			""},		// XEmacs?
//  {"iso-2022-8bit",			""},		// XEmacs?
//  {"iso-2022-8bit-lock",		""},		// XEmacs?
//  {"iso-2022-8bit-lock-ss2",		""},		// XEmacs?
//  {"iso-2022-8bit-ss2",		""},		// --
//  {"iso-2022-cjk",			""},		// --
//  {"iso-2022-cn",			"ISO-2022-CN"},	// Emacs
//  {"iso-2022-cn-ext",			"ISO-2022-CN-EXT"},// Emacs
//  {"iso-2022-int-1",			""},		// --
//  {"iso-2022-jp",			"ISO-2022-JP"},
//  {"iso-2022-jp-1978-irv",		"ISO-2022-JP"},
//  {"iso-2022-jp-2",			"ISO-2022-JP-2"},
//  {"iso-2022-jp-3",			"ISO-2022-JP-3"},// *, XEmacs?
//  {"iso-2022-jp-3-compatible",	""},		// XEmacs?
//  {"iso-2022-jp-3-strict",		"ISO-2022-JP-3"},// *, XEmacs?
//  {"iso-2022-kr",			"ISO-2022-KR"},
//  {"iso-2022-lock",			""},		// XEmacs?
//  {"iso-8859-10",			"ISO-8859-10"},	// Emacs
//  {"iso-8859-11",			"ISO-8859-11"},	// *, Emacs
//  {"iso-8859-14",			"ISO-8859-14"},	// Emacs
//  {"iso-8859-16",			"ISO-8859-16"},
//  {"iso-8859-3",			"ISO-8859-3"},
//  {"iso-8859-4",			"ISO-8859-4"},
//  {"iso-8859-6",			"ISO-8859-6"},
//  {"iso-8859-8",			"ISO-8859-8"},
//  {"iso-8859-8-e",			"ISO-8859-8"},
//  {"iso-8859-8-i",			"ISO-8859-8"},	// Emacs
//  {"iso-latin-10",			"ISO-8859-16"},	// Emacs
//  {"iso-latin-1-with-esc",		""},		// --
//  {"iso-latin-2-with-esc",		""},		// --
//  {"iso-latin-3",			"ISO-8859-3"},	// Emacs
//  {"iso-latin-3-with-esc",		""},		// --
//  {"iso-latin-4",			"ISO-8859-4"},	// Emacs
//  {"iso-latin-4-with-esc",		""},		// --
//  {"iso-latin-5-with-esc",		""},		// --
//  {"iso-latin-6",			"ISO-8859-10"},	// Emacs
//  {"iso-latin-8",			"ISO-8859-14"},	// Emacs
//  {"iso-safe",				""},		// --
//  {"japanese-iso-7bit-1978-irv",	"ISO-2022-JP"},	// Emacs
//  {"japanese-iso-8bit-with-esc",	""},		// --
//  {"japanese-shift-jis",		"Shift_JIS"},	// Emacs
//  {"japanese-shift-jisx0213",		""},		// XEmacs?
//  {"jis7",				"ISO-2022-JP"},	// Xemacs
//  {"junet",				"ISO-2022-JP"},
//  {"koi8-t",				"KOI8-T"},	// *, Emacs
//  {"koi8-u",				"KOI8-U"},	// Emacs
//  {"korean-iso-7bit-lock",		"ISO-2022-KR"},
//  {"korean-iso-8bit-with-esc",	""},		// --
//  {"lao",				""},		// not covered
//  {"lao-with-esc",			""},		// --
//  {"latin-10",			"ISO-8859-16"},	// Emacs
//  {"latin-3",				"ISO-8859-3"},	// Emacs
//  {"latin-4",				"ISO-8859-4"},	// Emacs
//  {"latin-6",				"ISO-8859-10"},	// Emacs
//  {"latin-8",				"ISO-8859-14"},	// Emacs
//  {"mac",				""},		// --
//  {"mac-roman",			"MACINTOSH"},	// Emacs
//  {"mik",				""},		// not covered
//  {"next",				"NEXTSTEP"},	// *, Emacs
//  {"no-conversion",			""},		// --
//  {"old-jis",				"ISO-2022-JP"},
//  {"pt154",				"PT154"},	// Emacs
//  {"raw-text",			""},		// --
//  {"ruscii",				"cp1125"},	// *, Emacs
//  {"shift-jis",			"Shift_JIS"},	// XEmacs
//  {"shift_jis",			"Shift_JIS"},
//  {"shift_jisx0213",			"Shift_JISX0213"},// *, XEmacs?
//  {"sjis",				"Shift_JIS"},	// Emacs
//  {"tcvn",				"TCVN"},	// *, Emacs
//  {"tcvn-5712",			"TCVN"},	// *, Emacs
//  {"thai-tis620",			"TIS-620"},
//  {"thai-tis620-with-esc",		""},		// --
//  {"th-tis620",			"TIS-620"},
//  {"tibetan",				""},		// not covered
//  {"tibetan-iso-8bit",		""},		// not covered
//  {"tibetan-iso-8bit-with-esc",	""},		// --
//  {"tis-620",				"TIS-620"},
//  {"tis620",				"TIS-620"},
//  {"undecided",			""},		// --
//  {"unix",				""},		// --
//  {"utf-7",				"UTF-7"},	// Emacs
//  {"utf-7-safe",			""},		// XEmacs?
//  {"utf-8-ws",			"UTF-8"},	// XEmacs?
//  {"vietnamese-tcvn",			"TCVN"},	// *, Emacs
//  {"vietnamese-viqr",			"VIQR"},	// not covered
//  {"vietnamese-viscii",		"VISCII"},
//  {"vietnamese-vscii",		""},		// not covered
//  {"viqr",				"VIQR"},	// not covered
//  {"viscii",				"VISCII"},
//  {"vscii",				""},		// not covered
//  {"windows-037",			""},		// not covered
//  {"windows-10000",			""},		// not covered
//  {"windows-10001",			""},		// not covered
//  {"windows-10006",			""},		// not covered
//  {"windows-10007",			""},		// not covered
//  {"windows-10029",			""},		// not covered
//  {"windows-10079",			""},		// not covered
//  {"windows-10081",			""},		// not covered
//  {"windows-1026",			""},		// not covered
//  {"windows-1200",			""},		// not covered
//  {"windows-1250",			"windows-1250"},
//  {"windows-1251",			"windows-1251"},
//  {"windows-1252",			"windows-1252"},
//  {"windows-1253",			"windows-1253"},
//  {"windows-1254",			"windows-1254"},
//  {"windows-1255",			"windows-1255"},
//  {"windows-1256",			"windows-1256"},
//  {"windows-1257",			"windows-1257"},
//  {"windows-1258",			"windows-1258"},
//  {"windows-1361",			"cp1361"},	// *, XEmacs
//  {"windows-437",			"cp437"},	// XEmacs
//  {"windows-500",			""},		// not covered
//  {"windows-708",			""},		// not covered
//  {"windows-709",			""},		// not covered
//  {"windows-710",			""},		// not covered
//  {"windows-720",			""},		// not covered
//  {"windows-737",			"cp737"},	// *, XEmacs
//  {"windows-775",			"cp775"},	// XEmacs
//  {"windows-850",			"cp850"},	// XEmacs
//  {"windows-852",			"cp852"},	// XEmacs
//  {"windows-855",			"cp855"},	// XEmacs
//  {"windows-857",			"cp857"},	// XEmacs
//  {"windows-860",			"cp860"},	// XEmacs
//  {"windows-861",			"cp861"},	// XEmacs
//  {"windows-862",			"cp862"},	// XEmacs
//  {"windows-863",			"cp863"},	// XEmacs
//  {"windows-864",			"cp864"},	// XEmacs
//  {"windows-865",			"cp865"},	// XEmacs
//  {"windows-866",			"cp866"},	// XEmacs
//  {"windows-869",			"cp869"},	// XEmacs
//  {"windows-874",			"cp874"},	// XEmacs
//  {"windows-875",			""},		// not covered
//  {"windows-932",			"cp932"},	// *, XEmacs
//  {"windows-936",			"cp936"},	// XEmacs
//  {"windows-949",			"cp949"},	// *, XEmacs
//  {"windows-950",			"cp950"},	// *, XEmacs
//  {"x-ctext",				""},		// --
//  {"x-ctext-with-extensions",		""},		// --

  {NULL,				NULL},
};

// ---------------------------------------------------------
// Convert encoding name from emacs to mime.
// ---------------------------------------------------------
char *
emacs2mime(char *emacs_enc)
{
  int emacs_enc_len = strlen(emacs_enc);
  if (emacs_enc_len > 4
      && !strcasecmp(emacs_enc + emacs_enc_len - 4, "-dos"))
    emacs_enc[emacs_enc_len - 4] = 0;
  if (emacs_enc_len > 4
      && !strcasecmp(emacs_enc + emacs_enc_len - 4, "-mac"))
    emacs_enc[emacs_enc_len - 4] = 0;
  if (emacs_enc_len > 5
      && !strcasecmp(emacs_enc + emacs_enc_len - 5, "-unix"))
    emacs_enc[emacs_enc_len - 5] = 0;
  for (const conversion *table = emacs_to_mime; table->from; table++)
    if (!strcasecmp(emacs_enc, table->from))
      return (char *)table->to;
  return emacs_enc;
}

// ---------------------------------------------------------
// Print out Unicode entity if value is greater than 0x7F.
// ---------------------------------------------------------
inline void
unicode_entity(int u)
{
  if (u < 0x80)
    putchar(u);
  else {
    // Handle soft hyphen specially -- it is an input character only,
    // not a glyph.
    if (u == 0xAD) {
      putchar('\\');
      putchar('%');
    }
    else
      printf("\\[u%04X]", u);
  }
}

// ---------------------------------------------------------
// Conversion functions.  All functions take `data', which
// normally holds the first two lines, and a file pointer.
// ---------------------------------------------------------

// Conversion from ISO-8859-1 (aka Latin-1) to Unicode.
void
conversion_latin1(FILE *fp, const string &data)
{
  int len = data.length();
  const unsigned char *ptr = (const unsigned char *)data.contents();
  for (int i = 0; i < len; i++)
    unicode_entity(ptr[i]);
  int c = -1;
  while ((c = getc(fp)) != EOF)
    unicode_entity(c);
}

// A future version of groff shall support UTF-8 natively.
// In this case, the UTF-8 stuff here in this file will be
// moved to the troff program.

struct utf8 {
  FILE *fp;
  unsigned char s[6];
  enum {
    FIRST = 0,
    SECOND,
    THIRD,
    FOURTH,
    FIFTH,
    SIXTH
  } byte;
  int expected_bytes;
  int invalid_warning;
  int incomplete_warning;
  utf8(FILE *);
  ~utf8();
  void add(unsigned char);
  void invalid();
  void incomplete();
};

utf8::utf8(FILE *f) : fp(f), byte(FIRST), expected_bytes(1),
		      invalid_warning(1), incomplete_warning(1)
{
  // empty
}

utf8::~utf8()
{
  if (byte != FIRST)
    incomplete();
}

inline void
utf8::add(unsigned char c)
{
  s[byte] = c;
  if (byte == FIRST) {
    if (c < 0x80)
      unicode_entity(c);
    else if (c < 0xC0)
      invalid();
    else if (c < 0xE0) {
      expected_bytes = 2;
      byte = SECOND;
    }
    else if (c < 0xF0) {
      expected_bytes = 3;
      byte = SECOND;
    }
    else if (c < 0xF8) {
      expected_bytes = 4;
      byte = SECOND;
    }
    else if (c < 0xFC) {
      expected_bytes = 5;
      byte = SECOND;
    }
    else if (c < 0xFE) {
      expected_bytes = 6;
      byte = SECOND;
    }
    else
      invalid();
    return;
  }
  if (c < 0x80 || c > 0xBF) {
    incomplete();
    add(c);
    return;
  }
  switch (byte) {
  case FIRST:
    // can't happen
    break;
  case SECOND:
    if (expected_bytes == 2) {
      if (s[0] < 0xC2)
	invalid();
      else
	unicode_entity(((s[0] & 0x1F) << 6)
		       | (s[1] ^ 0x80));
      byte = FIRST;
    }
    else
      byte = THIRD;
    break;
  case THIRD:
    if (expected_bytes == 3) {
      if (!(s[0] >= 0xE1 || s[1] >= 0xA0))
	invalid();
      else
	unicode_entity(((s[0] & 0x1F) << 12)
		       | ((s[1] ^ 0x80) << 6)
		       | (s[2] ^ 0x80));
      byte = FIRST;
    }
    else
      byte = FOURTH;
    break;
  case FOURTH:
    // We reject everything greater than 0x10FFFF.
    if (expected_bytes == 4) {
      if (!((s[0] >= 0xF1 || s[1] >= 0x90)
	    && (s[0] < 0xF4 || (s[0] == 0xF4 && s[1] < 0x90))))
	invalid();
      else
	unicode_entity(((s[0] & 0x07) << 18)
		       | ((s[1] ^ 0x80) << 12)
		       | ((s[2] ^ 0x80) << 6)
		       | (s[3] ^ 0x80));
      byte = FIRST;
    }
    else
      byte = FIFTH;
    break;
  case FIFTH:
    if (expected_bytes == 5) {
      invalid();
      byte = FIRST;
    }
    else
      byte = SIXTH;
    break;
  case SIXTH:
    invalid();
    byte = FIRST;
    break;
  }
}

void
utf8::invalid()
{
  if (debug_flag && invalid_warning) {
    fprintf(stderr, "  invalid byte(s) found in input stream --\n"
		    "  each such sequence replaced with 0xFFFD\n");
    invalid_warning = 0;
  }
  unicode_entity(0xFFFD);
  byte = FIRST;
}

void
utf8::incomplete()
{
  if (debug_flag && incomplete_warning) {
    fprintf(stderr, "  incomplete sequence(s) found in input stream --\n"
		    "  each such sequence replaced with 0xFFFD\n");
    incomplete_warning = 0;
  }
  unicode_entity(0xFFFD);
  byte = FIRST;
}

// Conversion from UTF-8 to Unicode.
void
conversion_utf8(FILE *fp, const string &data)
{
  utf8 u(fp);
  int len = data.length();
  const unsigned char *ptr = (const unsigned char *)data.contents();
  for (int i = 0; i < len; i++)
    u.add(ptr[i]);
  int c = -1;
  while ((c = getc(fp)) != EOF)
    u.add(c);
  return;
}

// Conversion from cp1047 (EBCDIC) to UTF-8.
void
conversion_cp1047(FILE *fp, const string &data)
{
  static unsigned char cp1047[] = {
    0x00, 0x01, 0x02, 0x03, 0x9C, 0x09, 0x86, 0x7F,	// 0x00
    0x97, 0x8D, 0x8E, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x9D, 0x85, 0x08, 0x87,	// 0x10
    0x18, 0x19, 0x92, 0x8F, 0x1C, 0x1D, 0x1E, 0x1F,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x0A, 0x17, 0x1B,	// 0x20
    0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x05, 0x06, 0x07,
    0x90, 0x91, 0x16, 0x93, 0x94, 0x95, 0x96, 0x04,	// 0x30
    0x98, 0x99, 0x9A, 0x9B, 0x14, 0x15, 0x9E, 0x1A,
    0x20, 0xA0, 0xE2, 0xE4, 0xE0, 0xE1, 0xE3, 0xE5,	// 0x40
    0xE7, 0xF1, 0xA2, 0x2E, 0x3C, 0x28, 0x2B, 0x7C,
    0x26, 0xE9, 0xEA, 0xEB, 0xE8, 0xED, 0xEE, 0xEF,	// 0x50
    0xEC, 0xDF, 0x21, 0x24, 0x2A, 0x29, 0x3B, 0x5E,
    0x2D, 0x2F, 0xC2, 0xC4, 0xC0, 0xC1, 0xC3, 0xC5,	// 0x60
    0xC7, 0xD1, 0xA6, 0x2C, 0x25, 0x5F, 0x3E, 0x3F,
    0xF8, 0xC9, 0xCA, 0xCB, 0xC8, 0xCD, 0xCE, 0xCF,	// 0x70
    0xCC, 0x60, 0x3A, 0x23, 0x40, 0x27, 0x3D, 0x22,
    0xD8, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,	// 0x80
    0x68, 0x69, 0xAB, 0xBB, 0xF0, 0xFD, 0xFE, 0xB1,
    0xB0, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,	// 0x90
    0x71, 0x72, 0xAA, 0xBA, 0xE6, 0xB8, 0xC6, 0xA4,
    0xB5, 0x7E, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,	// 0xA0
    0x79, 0x7A, 0xA1, 0xBF, 0xD0, 0x5B, 0xDE, 0xAE,
    0xAC, 0xA3, 0xA5, 0xB7, 0xA9, 0xA7, 0xB6, 0xBC,	// 0xB0
    0xBD, 0xBE, 0xDD, 0xA8, 0xAF, 0x5D, 0xB4, 0xD7,
    0x7B, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,	// 0xC0
    0x48, 0x49, 0xAD, 0xF4, 0xF6, 0xF2, 0xF3, 0xF5,
    0x7D, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,	// 0xD0
    0x51, 0x52, 0xB9, 0xFB, 0xFC, 0xF9, 0xFA, 0xFF,
    0x5C, 0xF7, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,	// 0xE0
    0x59, 0x5A, 0xB2, 0xD4, 0xD6, 0xD2, 0xD3, 0xD5,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,	// 0xF0
    0x38, 0x39, 0xB3, 0xDB, 0xDC, 0xD9, 0xDA, 0x9F,
  };
  int len = data.length();
  const unsigned char *ptr = (const unsigned char *)data.contents();
  for (int i = 0; i < len; i++)
    unicode_entity(cp1047[ptr[i]]);
  int c = -1;
  while ((c = getc(fp)) != EOF)
    unicode_entity(cp1047[c]);
}

// Locale-sensible conversion.
#if HAVE_ICONV
void
conversion_iconv(FILE *fp, const string &data, char *enc)
{
  iconv_t handle = iconv_open(UNICODE, enc);
  if (handle == (iconv_t)-1) {
    if (errno == EINVAL) {
      error("encoding system `%1' not supported by iconv()", enc);
      return;
    }
    fatal("iconv_open failed");
  }
  char inbuf[BUFSIZ];
  int outbuf[BUFSIZ];
  char *outptr = (char *)outbuf;
  size_t outbytes_left = BUFSIZ * sizeof (int);
  // Handle `data'.
  char *inptr = (char *)data.contents();
  size_t inbytes_left = data.length();
  char *limit;
  while (inbytes_left > 0) {
    size_t status = iconv(handle,
			  (ICONV_CONST char **)&inptr, &inbytes_left,
			  &outptr, &outbytes_left);
    if (status == (size_t)-1) {
      if (errno == EILSEQ) {
	// Invalid byte sequence.  XXX
	inptr++;
	inbytes_left--;
      }
      else if (errno == E2BIG) {
	// Output buffer is full.
	limit = (char *)outbuf + BUFSIZ * sizeof (int) - outbytes_left;
	for (int *ptr = outbuf; (char *)ptr < limit; ptr++)
	  unicode_entity(*ptr);
	memmove(outbuf, outptr, outbytes_left);
	outptr = (char *)outbuf + outbytes_left;
	outbytes_left = BUFSIZ * sizeof (int) - outbytes_left;
      }
      else if (errno == EINVAL) {
	// `data' ends with partial input sequence.
	memcpy(inbuf, inptr, inbytes_left);
	break;
      }
    }
  }
  // Handle `fp' and switch to `inbuf'.
  size_t read_bytes;
  char *read_start = inbuf + inbytes_left;
  while ((read_bytes = fread(read_start, 1, BUFSIZ - inbytes_left, fp)) > 0) {
    inptr = inbuf;
    inbytes_left += read_bytes;
    while (inbytes_left > 0) {
      size_t status = iconv(handle,
			    (ICONV_CONST char **)&inptr, &inbytes_left,
			    &outptr, &outbytes_left);
      if (status == (size_t)-1) {
	if (errno == EILSEQ) {
	  // Invalid byte sequence.  XXX
	  inptr++;
	  inbytes_left--;
	}
	else if (errno == E2BIG) {
	  // Output buffer is full.
	  limit = (char *)outbuf + BUFSIZ * sizeof (int) - outbytes_left;
	  for (int *ptr = outbuf; (char *)ptr < limit; ptr++)
	    unicode_entity(*ptr);
	  memmove(outbuf, outptr, outbytes_left);
	  outptr = (char *)outbuf + outbytes_left;
	  outbytes_left = BUFSIZ * sizeof (int) - outbytes_left;
	}
	else if (errno == EINVAL) {
	  // `inbuf' ends with partial input sequence.
	  memmove(inbuf, inptr, inbytes_left);
	  break;
	}
      }
    }
    read_start = inbuf + inbytes_left;
  }
  iconv_close(handle);
  // XXX use ferror?
  limit = (char *)outbuf + BUFSIZ * sizeof (int) - outbytes_left;
  for (int *ptr = outbuf; (char *)ptr < limit; ptr++)
    unicode_entity(*ptr);
}
#endif /* HAVE_ICONV */

// ---------------------------------------------------------
// Handle Byte Order Mark.
//
// Since we have a chicken-and-egg problem it's necessary
// to handle the BOM manually if it is in the data stream.
// As documented in the Unicode book it is very unlikely
// that any normal text file (regardless of the encoding)
// starts with the bytes which represent a BOM.
//
// Return the BOM in string `BOM'; `data' then starts with
// the byte after the BOM.  This function reads (at most)
// four bytes from the data stream.
//
// Return encoding if a BOM is found, NULL otherwise.
// ---------------------------------------------------------
const char *
get_BOM(FILE *fp, string &BOM, string &data)
{
  // The BOM is U+FEFF.  We have thus the following possible
  // representations.
  //
  //   UTF-8: 0xEFBBBF
  //   UTF-16: 0xFEFF or 0xFFFE
  //   UTF-32: 0x0000FEFF or 0xFFFE0000
  static struct {
    int len;
    const char *str;
    const char *name;
  } BOM_table[] = {
    {4, "\x00\x00\xFE\xFF", "UTF-32"},
    {4, "\xFF\xFE\x00\x00", "UTF-32"},
    {3, "\xEF\xBB\xBF", "UTF-8"},
    {2, "\xFE\xFF", "UTF-16"},
    {2, "\xFF\xFE", "UTF-16"},
  };
  const int BOM_table_len = sizeof (BOM_table) / sizeof (BOM_table[0]);
  char BOM_string[4];
  const char *retval = NULL;
  int len;
  for (len = 0; len < 4; len++) {
    int c = getc(fp);
    if (c == EOF)
      break;
    BOM_string[len] = char(c);
  }
  int i;
  for (i = 0; i < BOM_table_len; i++) {
    if (BOM_table[i].len <= len
	&& memcmp(BOM_string, BOM_table[i].str, BOM_table[i].len) == 0)
      break;
  }
  int j = 0;
  if (i < BOM_table_len) {
    for (; j < BOM_table[i].len; j++)
      BOM += BOM_string[j];
    retval = BOM_table[i].name;
  }
  for (; j < len; j++)
    data += BOM_string[j];
  return retval;
}

// ---------------------------------------------------------
// Get first two lines from input stream.
//
// Return string (allocated with `new') without zero bytes
// or NULL in case no coding tag can occur in the data
// (which is stored unmodified in `data').
// ---------------------------------------------------------
char *
get_tag_lines(FILE *fp, string &data)
{
  int newline_count = 0;
  int c, prev = -1;
  // Handle CR, LF, and CRLF as line separators.
  for (int i = 0; i < data.length(); i++) {
    c = data[i];
    if (c == '\n' || c == '\r')
      newline_count++;
    if (c == '\n' && prev == '\r')
      newline_count--;
    prev = c;
  }
  if (newline_count > 1)
    return NULL;
  int emit_warning = 1;
  for (int lines = newline_count; lines < 2; lines++) {
    while ((c = getc(fp)) != EOF) {
      if (c == '\0' && debug_flag && emit_warning) {
	fprintf(stderr,
		"  null byte(s) found in input stream --\n"
		"  search for coding tag might return false result\n");
	emit_warning = 0;
      }
      data += char(c);
      if (c == '\n' || c == '\r')
	break;
    }
    // Handle CR, LF, and CRLF as line separators.
    if (c == '\r') {
      c = getc(fp);
      if (c != EOF && c != '\n')
	ungetc(c, fp);
      else
	data += char(c);
    }
  }
  return data.extract();
}

// ---------------------------------------------------------
// Check whether C string starts with a comment.
//
// Return 1 if true, 0 otherwise.
// ---------------------------------------------------------
int
is_comment_line(char *s)
{
  if (!s || !*s)
    return 0;
  if (*s == '.' || *s == '\'')
  {
    s++;
    while (*s == ' ' || *s == '\t')
      s++;
    if (*s && *s == '\\')
    {
      s++;
      if (*s == '"' || *s == '#')
	return 1;
    }
  }
  else if (*s == '\\')
  {
    s++;
    if (*s == '#')
      return 1;
  }
  return 0;
}

// ---------------------------------------------------------
// Get a value/variable pair from a local variables list
// in a C string which look like this:
//
//   <variable1>: <value1>; <variable2>: <value2>; ...
//
// Leading and trailing blanks are ignored.  There might be
// more than one blank after `:' and `;'.
//
// Return position of next value/variable pair or NULL if
// at end of data.
// ---------------------------------------------------------
char *
get_variable_value_pair(char *d1, char **variable, char **value)
{
  static char var[MAX_VAR_LEN], val[MAX_VAR_LEN];
  *variable = var;
  *value = val;
  while (*d1 == ' ' || *d1 == '\t')
    d1++;
  // Get variable.
  int l = 0;
  while (l < MAX_VAR_LEN - 1 && *d1 && !strchr(";: \t", *d1))
    var[l++] = *(d1++);
  var[l] = 0;
  // Skip everything until `:', `;', or end of data.
  while (*d1 && *d1 != ':' && *d1 != ';')
    d1++;
  val[0] = 0;
  if (!*d1)
    return NULL;
  if (*d1 == ';')
    return d1 + 1;
  d1++;
  while (*d1 == ' ' || *d1 == '\t')
    d1++;
  // Get value.
  l = 0;
  while (l < MAX_VAR_LEN - 1 && *d1 && !strchr("; \t", *d1))
    val[l++] = *(d1++);
  val[l] = 0;
  // Skip everything until `;' or end of data.
  while (*d1 && *d1 != ';')
    d1++;
  if (*d1 == ';')
    return d1 + 1;
  return NULL;
}

// ---------------------------------------------------------
// Check coding tag in the read buffer.
//
// We search for the following line:
//
//   <comment> ... -*-<local variables list>-*-
//
// (`...' might be anything).
//
// <comment> can be one of the following syntax forms at the
// beginning of the line:
//
//   .\"   .\#   '\"   '\#   \#
//
// There can be whitespace after the leading `.' or "'".
//
// The local variables list must occur within the first
// comment block at the very beginning of the data stream.
//
// Within the <local variables list>, we search for
//
//   coding: <value>
//
// which specifies the coding system used for the data
// stream.
//
// Return <value> if found, NULL otherwise.
//
// Note that null bytes in the data are skipped before applying
// the algorithm.  This should work even with files encoded as
// UTF-16 or UTF-32 (or its siblings) in most cases.
//
// XXX Add support for tag at the end of buffer.
// ---------------------------------------------------------
char *
check_coding_tag(FILE *fp, string &data)
{
  char *inbuf = get_tag_lines(fp, data);
  char *lineend;
  for (char *p = inbuf; is_comment_line(p); p = lineend + 1) {
    if ((lineend = strchr(p, '\n')) == NULL)
      break;
    *lineend = 0;		// switch temporarily to '\0'
    char *d1 = strstr(p, "-*-");
    char *d2 = 0;
    if (d1)
      d2 = strstr(d1 + 3, "-*-");
    *lineend = '\n';		// restore newline
    if (!d1 || !d2)
      continue;
    *d2 = 0;			// switch temporarily to '\0'
    d1 += 3;
    while (d1) {
      char *variable, *value;
      d1 = get_variable_value_pair(d1, &variable, &value);
      if (!strcasecmp(variable, "coding")) {
	*d2 = '-';		// restore '-'
	a_delete inbuf;
	return value;
      }
    }
    *d2 = '-';			// restore '-'
  }
  a_delete inbuf;
  return NULL;
}

// ---------------------------------------------------------
// Handle an input file.  If filename is `-' handle stdin.
//
// Return 1 on success, 0 otherwise.
// ---------------------------------------------------------
int
do_file(const char *filename)
{
  FILE *fp;
  string BOM, data;
  if (strcmp(filename, "-")) {
    if (debug_flag)
      fprintf(stderr, "file `%s':\n", filename);
    fp = fopen(filename, FOPEN_RB);
    if (!fp) {
      error("can't open `%1': %2", filename, strerror(errno));
      return 0;
    }
  }
  else {
    if (debug_flag)
      fprintf(stderr, "standard input:\n");
    SET_BINARY(fileno(stdin));
    fp = stdin;
  }
  const char *BOM_encoding = get_BOM(fp, BOM, data);
  // Determine the encoding.
  char *encoding;
  if (user_encoding[0]) {
    if (debug_flag) {
      fprintf(stderr, "  user-specified encoding `%s', "
		      "no search for coding tag\n",
		      user_encoding);
      if (BOM_encoding && strcmp(BOM_encoding, user_encoding))
	fprintf(stderr, "  but BOM in data stream implies encoding `%s'!\n",
			BOM_encoding);
    }
    encoding = (char *)user_encoding;
  }
  else if (BOM_encoding) {
    if (debug_flag)
      fprintf(stderr, "  found BOM, no search for coding tag\n");
    encoding = (char *)BOM_encoding;
  }
  else {
    // `check_coding_tag' returns a pointer to a static array (or NULL).
    char *file_encoding = check_coding_tag(fp, data);
    if (!file_encoding) {
      if (debug_flag)
	fprintf(stderr, "  no file encoding\n");
      file_encoding = default_encoding;
    }
    else
      if (debug_flag)
	fprintf(stderr, "  file encoding: `%s'\n", file_encoding);
    encoding = file_encoding;
  }
  strncpy(encoding_string, encoding, MAX_VAR_LEN - 1);
  encoding_string[MAX_VAR_LEN - 1] = 0;
  encoding = encoding_string;
  // Translate from MIME & Emacs encoding names to locale encoding names.
  encoding = emacs2mime(encoding_string);
  if (encoding[0] == '\0') {
    error("encoding `%1' not supported, not a portable encoding",
	  encoding_string);
    return 0;
  }
  if (debug_flag)
    fprintf(stderr, "  encoding used: `%s'\n", encoding);
  if (!raw_flag)
    printf(".lf 1 %s\n", filename);
  int success = 1;
  // Call converter (converters write to stdout).
  if (!strcasecmp(encoding, "ISO-8859-1"))
    conversion_latin1(fp, BOM + data);
  else if (!strcasecmp(encoding, "UTF-8"))
    conversion_utf8(fp, data);
  else if (!strcasecmp(encoding, "cp1047"))
    conversion_cp1047(fp, BOM + data);
  else {
#if HAVE_ICONV
    conversion_iconv(fp, BOM + data, encoding);
#else
    error("encoding system `%1' not supported", encoding);
    success = 0;
#endif /* HAVE_ICONV */
  }
  if (fp != stdin)
    fclose(fp);
  return success;
}

// ---------------------------------------------------------
// Print usage.
// ---------------------------------------------------------
void
usage(FILE *stream)
{
  fprintf(stream, "usage: %s [ option ] [ files ]\n"
		  "\n"
		  "-d           show debugging messages\n"
		  "-D encoding  specify default encoding\n"
		  "-e encoding  specify input encoding\n"
		  "-h           print this message\n"
		  "-r           don't add .lf requests\n"
		  "-v           print version number\n"
		  "\n"
		  "The default encoding is `%s'.\n",
		  program_name, default_encoding);
}

// ---------------------------------------------------------
// Main routine.
// ---------------------------------------------------------
int
main(int argc, char **argv)
{
  program_name = argv[0];
  // Determine the default encoding.  This must be done before
  // getopt() is called since the usage message shows the default
  // encoding.
  setlocale(LC_ALL, "");
  char *locale = getlocale(LC_CTYPE);
  if (!locale || !strcmp(locale, "C") || !strcmp(locale, "POSIX"))
    strcpy(default_encoding, "latin1");
  else {
    strncpy(default_encoding, locale_charset(), MAX_VAR_LEN - 1);
    default_encoding[MAX_VAR_LEN - 1] = 0;
  }

  program_name = argv[0];
  int opt;
  static const struct option long_options[] = {
    { "help", no_argument, 0, 'h' },
    { "version", no_argument, 0, 'v' },
    { NULL, 0, 0, 0 }
  };
  // Parse the command line options.
  while ((opt = getopt_long(argc, argv,
			    "dD:e:hrv", long_options, NULL)) != EOF)
    switch (opt) {
    case 'v':
      printf("GNU preconv (groff) version %s %s iconv support\n",
	     Version_string,
#ifdef HAVE_ICONV
	     "with"
#else
	     "without"
#endif /* HAVE_ICONV */
	    );
      exit(0);
      break;
    case 'd':
      debug_flag = 1;
      break;
    case 'e':
      if (optarg) {
	strncpy(user_encoding, optarg, MAX_VAR_LEN - 1);
	user_encoding[MAX_VAR_LEN - 1] = 0;
      }
      else
	user_encoding[0] = 0;
      break;
    case 'D':
      if (optarg) {
	strncpy(default_encoding, optarg, MAX_VAR_LEN - 1);
	default_encoding[MAX_VAR_LEN - 1] = 0;
      }
      break;
    case 'r':
      raw_flag = 1;
      break;
    case 'h':
      usage(stdout);
      exit(0);
      break;
    case '?':
      usage(stderr);
      exit(1);
      break;
    default:
      assert(0);
    }
  int nbad = 0;
  if (debug_flag)
    fprintf(stderr, "default encoding: `%s'\n", default_encoding);
  if (optind >= argc)
    nbad += !do_file("-");
  else
    for (int i = optind; i < argc; i++)
      nbad += !do_file(argv[i]);
  if (ferror(stdout) || fflush(stdout) < 0)
    fatal("output error");
  return nbad != 0;
}

/* end of preconv.cpp */
