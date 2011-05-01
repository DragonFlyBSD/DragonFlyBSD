/* lang.c -- language-dependent support.
   $Id: lang.c,v 1.34 2007/12/03 01:38:43 karl Exp $

   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Originally written by Karl Heinz Marbaise <kama@hippo.fido.de>.  */

#include "system.h"
#include "cmds.h"
#include "files.h"
#include "lang.h"
#include "makeinfo.h"
#include "xml.h"

#include <assert.h>

/* Current document encoding.  */
encoding_code_type document_encoding_code = no_encoding;

/* Current language code; default is English.  */
language_code_type language_code = en;

/* Language to use for translations that end up in the output. */
char *document_language = "C";

/* By default, unsupported encoding is an empty string.  */
char *unknown_encoding = NULL;

static iso_map_type asis_map [] = {{NULL, 0, 0}}; /* ASCII, etc. */

/* Translation table between HTML and ISO Codes.  The last item is
   hopefully the Unicode. It might be possible that those Unicodes are
   not correct, cause I didn't check them. kama */
static iso_map_type iso8859_1_map [] = {
  { "nbsp",   0xA0, 0x00A0 },
  { "iexcl",  0xA1, 0x00A1 },
  { "cent",   0xA2, 0x00A2 },
  { "pound",  0xA3, 0x00A3 },
  { "curren", 0xA4, 0x00A4 },
  { "yen",    0xA5, 0x00A5 },
  { "brkbar", 0xA6, 0x00A6 },
  { "sect",   0xA7, 0x00A7 },
  { "uml",    0xA8, 0x00A8 },
  { "copy",   0xA9, 0x00A9 },
  { "ordf",   0xAA, 0x00AA },
  { "laquo",  0xAB, 0x00AB },
  { "not",    0xAC, 0x00AC },
  { "shy",    0xAD, 0x00AD },
  { "reg",    0xAE, 0x00AE },
  { "hibar",  0xAF, 0x00AF },
  { "deg",    0xB0, 0x00B0 },
  { "plusmn", 0xB1, 0x00B1 },
  { "sup2",   0xB2, 0x00B2 },
  { "sup3",   0xB3, 0x00B3 },
  { "acute",  0xB4, 0x00B4 },
  { "micro",  0xB5, 0x00B5 },
  { "para",   0xB6, 0x00B6 },
  { "middot", 0xB7, 0x00B7 },
  { "cedil",  0xB8, 0x00B8 },
  { "sup1",   0xB9, 0x00B9 },
  { "ordm",   0xBA, 0x00BA },
  { "raquo",  0xBB, 0x00BB },
  { "frac14", 0xBC, 0x00BC },
  { "frac12", 0xBD, 0x00BD },
  { "frac34", 0xBE, 0x00BE },
  { "iquest", 0xBF, 0x00BF },
  { "Agrave", 0xC0, 0x00C0, "A" },
  { "Aacute", 0xC1, 0x00C1, "A" },
  { "Acirc",  0xC2, 0x00C2, "A" },
  { "Atilde", 0xC3, 0x00C3, "A" },
  { "Auml",   0xC4, 0x00C4, "A" },
  { "Aring",  0xC5, 0x00C5, "AA" },
  { "AElig",  0xC6, 0x00C6, "AE" },
  { "Ccedil", 0xC7, 0x00C7, "C" },
  { "Ccedil", 0xC7, 0x00C7, "C" },
  { "Egrave", 0xC8, 0x00C8, "E" },
  { "Eacute", 0xC9, 0x00C9, "E" },
  { "Ecirc",  0xCA, 0x00CA, "E" },
  { "Euml",   0xCB, 0x00CB, "E" },
  { "Igrave", 0xCC, 0x00CC, "I" },
  { "Iacute", 0xCD, 0x00CD, "I" },
  { "Icirc",  0xCE, 0x00CE, "I" },
  { "Iuml",   0xCF, 0x00CF, "I" },
  { "ETH",    0xD0, 0x00D0, "DH" },
  { "Ntilde", 0xD1, 0x00D1, "N" },
  { "Ograve", 0xD2, 0x00D2, "O" },
  { "Oacute", 0xD3, 0x00D3, "O" },
  { "Ocirc",  0xD4, 0x00D4, "O" },
  { "Otilde", 0xD5, 0x00D5, "O" },
  { "Ouml",   0xD6, 0x00D6, "O" },
  { "times",  0xD7, 0x00D7 },
  { "Oslash", 0xD8, 0x00D8, "OE" },
  { "Ugrave", 0xD9, 0x00D9, "U" },
  { "Uacute", 0xDA, 0x00DA, "U" },
  { "Ucirc",  0xDB, 0x00DB, "U" },
  { "Uuml",   0xDC, 0x00DC, "U" },
  { "Yacute", 0xDD, 0x00DD, "Y" },
  { "THORN",  0xDE, 0x00DE, "TH" },
  { "szlig",  0xDF, 0x00DF, "s" },
  { "agrave", 0xE0, 0x00E0, "a" },
  { "aacute", 0xE1, 0x00E1, "a" },
  { "acirc",  0xE2, 0x00E2, "a" },
  { "atilde", 0xE3, 0x00E3, "a" },
  { "auml",   0xE4, 0x00E4, "a" },
  { "aring",  0xE5, 0x00E5, "aa" },
  { "aelig",  0xE6, 0x00E6, "ae" },
  { "ccedil", 0xE7, 0x00E7, "c" },
  { "egrave", 0xE8, 0x00E8, "e" },
  { "eacute", 0xE9, 0x00E9, "e" },
  { "ecirc",  0xEA, 0x00EA, "e" },
  { "euml",   0xEB, 0x00EB, "e" },
  { "igrave", 0xEC, 0x00EC, "i" },
  { "iacute", 0xED, 0x00ED, "i" },
  { "icirc",  0xEE, 0x00EE, "i" },
  { "iuml",   0xEF, 0x00EF, "i" },
  { "eth",    0xF0, 0x00F0, "dh" },
  { "ntilde", 0xF1, 0x00F1, "n" },
  { "ograve", 0xF2, 0x00F2, "o"},
  { "oacute", 0xF3, 0x00F3, "o" },
  { "ocirc",  0xF4, 0x00F4, "o" },
  { "otilde", 0xF5, 0x00F5, "o" },
  { "ouml",   0xF6, 0x00F6, "o" },
  { "divide", 0xF7, 0x00F7 },
  { "oslash", 0xF8, 0x00F8, "oe" },
  { "ugrave", 0xF9, 0x00F9, "u" },
  { "uacute", 0xFA, 0x00FA, "u" },
  { "ucirc",  0xFB, 0x00FB, "u" },
  { "uuml",   0xFC, 0x00FC, "u" },
  { "yacute", 0xFD, 0x00FD, "y" },
  { "thorn",  0xFE, 0x00FE, "th" },
  { "yuml",   0xFF, 0x00FF, "y" },
  { NULL, 0, 0 }
};


/* ISO 8859-15, also known as Latin 9, differs from Latin 1 in only a
   few positions.  http://www.cs.tut.fi/~jkorpela/latin9.html has a good
   explanation and listing, summarized here.  The names here are
   abbreviated to fit in a decent line length.

  code position
  dec	oct   hex   latin1 latin1 name	      latin9 latin9 name

  164  0244  0xA4   U+00A4 currency symbol    U+20AC euro sign
  166  0246  0xA6   U+00A6 broken bar	      U+0160 S with caron
  168  0250  0xA8   U+00A8 diaeresis	      U+0161 s with caron
  180  0264  0xB4   U+00B4 acute accent	      U+017D Z with caron
  184  0270  0xB8   U+00B8 cedilla	      U+017E z with caron
  188  0274  0xBC   U+00BC fraction 1/4	      U+0152 ligature OE
  189  0275  0xBD   U+00BD fraction 1/2	      U+0153 ligature oe
  190  0276  0xBE   U+00BE fraction 3/4	      U+0178 Y with diaeresis
*/

static iso_map_type iso8859_15_map [] = {
  { "nbsp",   0xA0, 0x00A0 },
  { "iexcl",  0xA1, 0x00A1 },
  { "cent",   0xA2, 0x00A2 },
  { "pound",  0xA3, 0x00A3 },
  { "euro",   0xA4, 0x20AC },
  { "yen",    0xA5, 0x00A5 },
  { "Scaron", 0xA6, 0x0160, "S" },
  { "sect",   0xA7, 0x00A7 },
  { "scaron", 0xA8, 0x0161, "s" },
  { "copy",   0xA9, 0x00A9 },
  { "ordf",   0xAA, 0x00AA },
  { "laquo",  0xAB, 0x00AB },
  { "not",    0xAC, 0x00AC },
  { "shy",    0xAD, 0x00AD },
  { "reg",    0xAE, 0x00AE },
  { "hibar",  0xAF, 0x00AF },
  { "deg",    0xB0, 0x00B0 },
  { "plusmn", 0xB1, 0x00B1 },
  { "sup2",   0xB2, 0x00B2 },
  { "sup3",   0xB3, 0x00B3 },
  { "Zcaron", 0xB4, 0x017D, "Z" },
  { "micro",  0xB5, 0x00B5 },
  { "para",   0xB6, 0x00B6 },
  { "middot", 0xB7, 0x00B7 },
  { "zcaron", 0xB8, 0x017E, "z" },
  { "sup1",   0xB9, 0x00B9 },
  { "ordm",   0xBA, 0x00BA },
  { "raquo",  0xBB, 0x00BB },
  { "OElig",  0xBC, 0x0152, "OE" },
  { "oelig",  0xBD, 0x0153, "oe" },
  { "Yuml",   0xBE, 0x0178, "y" },
  { "iquest", 0xBF, 0x00BF },
  { "Agrave", 0xC0, 0x00C0, "A" },
  { "Aacute", 0xC1, 0x00C1, "A" },
  { "Acirc",  0xC2, 0x00C2, "A" },
  { "Atilde", 0xC3, 0x00C3, "A" },
  { "Auml",   0xC4, 0x00C4, "A" },
  { "Aring",  0xC5, 0x00C5, "AA" },
  { "AElig",  0xC6, 0x00C6, "AE" },
  { "Ccedil", 0xC7, 0x00C7, "C" },
  { "Egrave", 0xC8, 0x00C8, "E" },
  { "Eacute", 0xC9, 0x00C9, "E" },
  { "Ecirc",  0xCA, 0x00CA, "E" },
  { "Euml",   0xCB, 0x00CB, "E" },
  { "Igrave", 0xCC, 0x00CC, "I" },
  { "Iacute", 0xCD, 0x00CD, "I" },
  { "Icirc",  0xCE, 0x00CE, "I" },
  { "Iuml",   0xCF, 0x00CF, "I" },
  { "ETH",    0xD0, 0x00D0, "DH" },
  { "Ntilde", 0xD1, 0x00D1, "N" },
  { "Ograve", 0xD2, 0x00D2, "O" },
  { "Oacute", 0xD3, 0x00D3, "O" },
  { "Ocirc",  0xD4, 0x00D4, "O" },
  { "Otilde", 0xD5, 0x00D5, "O" },
  { "Ouml",   0xD6, 0x00D6, "O" },
  { "times",  0xD7, 0x00D7 },
  { "Oslash", 0xD8, 0x00D8, "OE" },
  { "Ugrave", 0xD9, 0x00D9, "U" },
  { "Uacute", 0xDA, 0x00DA, "U" },
  { "Ucirc",  0xDB, 0x00DB, "U" },
  { "Uuml",   0xDC, 0x00DC, "U" },
  { "Yacute", 0xDD, 0x00DD, "Y" },
  { "THORN",  0xDE, 0x00DE, "TH" },
  { "szlig",  0xDF, 0x00DF, "s" },
  { "agrave", 0xE0, 0x00E0, "a" },
  { "aacute", 0xE1, 0x00E1, "a" },
  { "acirc",  0xE2, 0x00E2, "a" },
  { "atilde", 0xE3, 0x00E3, "a" },
  { "auml",   0xE4, 0x00E4, "a" },
  { "aring",  0xE5, 0x00E5, "aa" },
  { "aelig",  0xE6, 0x00E6, "ae" },
  { "ccedil", 0xE7, 0x00E7, "c" },
  { "egrave", 0xE8, 0x00E8, "e" },
  { "eacute", 0xE9, 0x00E9, "e" },
  { "ecirc",  0xEA, 0x00EA, "e" },
  { "euml",   0xEB, 0x00EB, "e" },
  { "igrave", 0xEC, 0x00EC, "i" },
  { "iacute", 0xED, 0x00ED, "i" },
  { "icirc",  0xEE, 0x00EE, "i" },
  { "iuml",   0xEF, 0x00EF, "i" },
  { "eth",    0xF0, 0x00F0, "d" },
  { "ntilde", 0xF1, 0x00F1, "n" },
  { "ograve", 0xF2, 0x00F2, "o" },
  { "oacute", 0xF3, 0x00F3, "o" },
  { "ocirc",  0xF4, 0x00F4, "o" },
  { "otilde", 0xF5, 0x00F5, "o" },
  { "ouml",   0xF6, 0x00F6, "o" },
  { "divide", 0xF7, 0x00F7 },
  { "oslash", 0xF8, 0x00F8, "oe" },
  { "ugrave", 0xF9, 0x00F9, "u" },
  { "uacute", 0xFA, 0x00FA, "u" },
  { "ucirc",  0xFB, 0x00FB, "u" },
  { "uuml",   0xFC, 0x00FC, "u" },
  { "yacute", 0xFD, 0x00FD, "y" },
  { "thorn",  0xFE, 0x00FE, "th" },
  { "yuml",   0xFF, 0x00FF, "y" },
  { NULL, 0, 0 }
};



/* Date: Mon, 31 Mar 2003 00:19:28 +0200
   From: Wojciech Polak <polak@gnu.org>
...
 * Primary Polish site for ogonki is http://www.agh.edu.pl/ogonki/,
   but it's only in Polish language (it has some interesting links).

 * A general site about ISO 8859-2 at http://nl.ijs.si/gnusl/cee/iso8859-2.html

 * ISO 8859-2 Character Set at http://nl.ijs.si/gnusl/cee/charset.html
   This site provides almost all information about iso-8859-2,
   including the character table!!! (must see!)

 * ISO 8859-2 and even HTML entities !!! (must see!)
   88592.txt in the GNU enscript distribution

 * (minor) http://www.agh.edu.pl/ogonki/plchars.html
   One more table, this time it includes even information about Polish
   characters in Unicode.
*/

static iso_map_type iso8859_2_map [] = {
  { "nbsp",	0xA0, 0x00A0 }, /* NO-BREAK SPACE */
  { "",	0xA1, 0x0104, "A" }, /* LATIN CAPITAL LETTER A WITH OGONEK */
  { "",	0xA2, 0x02D8 }, /* BREVE */
  { "",	0xA3, 0x0141, "L" }, /* LATIN CAPITAL LETTER L WITH STROKE */
  { "curren",	0xA4, 0x00A4 }, /* CURRENCY SIGN */
  { "",	0xA5, 0x013D, "L" }, /* LATIN CAPITAL LETTER L WITH CARON */
  { "",	0xA6, 0x015A, "S" }, /* LATIN CAPITAL LETTER S WITH ACUTE */
  { "sect",	0xA7, 0x00A7 }, /* SECTION SIGN */
  { "uml",	0xA8, 0x00A8 }, /* DIAERESIS */
  { "#xa9",	0xA9, 0x0160, "S" }, /* LATIN CAPITAL LETTER S WITH CARON */
  { "",	0xAA, 0x015E, "S" }, /* LATIN CAPITAL LETTER S WITH CEDILLA */
  { "",	0xAB, 0x0164, "T" }, /* LATIN CAPITAL LETTER T WITH CARON */
  { "",	0xAC, 0x0179, "Z" }, /* LATIN CAPITAL LETTER Z WITH ACUTE */
  { "shy",	0xAD, 0x00AD }, /* SOFT HYPHEN */
  { "",	0xAE, 0x017D, "Z" }, /* LATIN CAPITAL LETTER Z WITH CARON */
  { "",	0xAF, 0x017B, "Z" }, /* LATIN CAPITAL LETTER Z WITH DOT ABOVE */
  { "deg",	0xB0, 0x00B0 }, /* DEGREE SIGN */
  { "",	0xB1, 0x0105, "a" }, /* LATIN SMALL LETTER A WITH OGONEK */
  { "",	0xB2, 0x02DB }, /* OGONEK */
  { "",	0xB3, 0x0142, "l" }, /* LATIN SMALL LETTER L WITH STROKE */
  { "acute",	0xB4, 0x00B4 }, /* ACUTE ACCENT */
  { "",	0xB5, 0x013E, "l" }, /* LATIN SMALL LETTER L WITH CARON */
  { "",	0xB6, 0x015B, "s" }, /* LATIN SMALL LETTER S WITH ACUTE */
  { "",	0xB7, 0x02C7 }, /* CARON (Mandarin Chinese third tone) */
  { "cedil",	0xB8, 0x00B8 }, /* CEDILLA */
  { "",	0xB9, 0x0161, "s" }, /* LATIN SMALL LETTER S WITH CARON */
  { "",	0xBA, 0x015F, "s" }, /* LATIN SMALL LETTER S WITH CEDILLA */
  { "",	0xBB, 0x0165, "t" }, /* LATIN SMALL LETTER T WITH CARON */
  { "",	0xBC, 0x017A, "z" }, /* LATIN SMALL LETTER Z WITH ACUTE */
  { "",	0xBD, 0x02DD }, /* DOUBLE ACUTE ACCENT */
  { "",	0xBE, 0x017E, "z" }, /* LATIN SMALL LETTER Z WITH CARON */
  { "",	0xBF, 0x017C, "z" }, /* LATIN SMALL LETTER Z WITH DOT ABOVE */
  { "",	0xC0, 0x0154, "R" }, /* LATIN CAPITAL LETTER R WITH ACUTE */
  { "Aacute",	0xC1, 0x00C1, "A" }, /* LATIN CAPITAL LETTER A WITH ACUTE */
  { "Acirc",	0xC2, 0x00C2, "A" }, /* LATIN CAPITAL LETTER A WITH CIRCUMFLEX */
  { "",	0xC3, 0x0102, "A" }, /* LATIN CAPITAL LETTER A WITH BREVE */
  { "Auml",	0xC4, 0x00C4, "A" }, /* LATIN CAPITAL LETTER A WITH DIAERESIS */
  { "",	0xC5, 0x0139, "L" }, /* LATIN CAPITAL LETTER L WITH ACUTE */
  { "",	0xC6, 0x0106, "C" }, /* LATIN CAPITAL LETTER C WITH ACUTE */
  { "Ccedil",	0xC7, 0x00C7, "C" }, /* LATIN CAPITAL LETTER C WITH CEDILLA */
  { "",	0xC8, 0x010C, "C" }, /* LATIN CAPITAL LETTER C WITH CARON */
  { "Eacute",	0xC9, 0x00C9, "E" }, /* LATIN CAPITAL LETTER E WITH ACUTE */
  { "",	0xCA, 0x0118, "E" }, /* LATIN CAPITAL LETTER E WITH OGONEK */
  { "Euml",	0xCB, 0x00CB, "E" }, /* LATIN CAPITAL LETTER E WITH DIAERESIS */
  { "",	0xCC, 0x011A, "E" }, /* LATIN CAPITAL LETTER E WITH CARON */
  { "Iacute",	0xCD, 0x00CD, "I" }, /* LATIN CAPITAL LETTER I WITH ACUTE */
  { "Icirc",	0xCE, 0x00CE, "I" }, /* LATIN CAPITAL LETTER I WITH CIRCUMFLEX */
  { "",	0xCF, 0x010E, "D" }, /* LATIN CAPITAL LETTER D WITH CARON */
  { "ETH",	0xD0, 0x0110, "D" }, /* LATIN CAPITAL LETTER D WITH STROKE */
  { "",	0xD1, 0x0143, "N" }, /* LATIN CAPITAL LETTER N WITH ACUTE */
  { "",	0xD2, 0x0147, "N" }, /* LATIN CAPITAL LETTER N WITH CARON */
  { "Oacute",	0xD3, 0x00D3, "O" }, /* LATIN CAPITAL LETTER O WITH ACUTE */
  { "Ocirc",	0xD4, 0x00D4, "O" }, /* LATIN CAPITAL LETTER O WITH CIRCUMFLEX */
  { "",	0xD5, 0x0150, "O" }, /* LATIN CAPITAL LETTER O WITH DOUBLE ACUTE */
  { "Ouml",	0xD6, 0x00D6, "O" }, /* LATIN CAPITAL LETTER O WITH DIAERESIS */
  { "times",	0xD7, 0x00D7 }, /* MULTIPLICATION SIGN */
  { "",	0xD8, 0x0158, "R" }, /* LATIN CAPITAL LETTER R WITH CARON */
  { "",	0xD9, 0x016E, "U" }, /* LATIN CAPITAL LETTER U WITH RING ABOVE */
  { "Uacute",	0xDA, 0x00DA, "U" }, /* LATIN CAPITAL LETTER U WITH ACUTE */
  { "",	0xDB, 0x0170, "U" }, /* LATIN CAPITAL LETTER U WITH DOUBLE ACUTE */
  { "Uuml",	0xDC, 0x00DC, "U" }, /* LATIN CAPITAL LETTER U WITH DIAERESIS */
  { "Yacute",	0xDD, 0x00DD, "Y" }, /* LATIN CAPITAL LETTER Y WITH ACUTE */
  { "",	0xDE, 0x0162, "T" }, /* LATIN CAPITAL LETTER T WITH CEDILLA */
  { "szlig",	0xDF, 0x00DF, "ss" }, /* LATIN SMALL LETTER SHARP S (German) */
  { "",	0xE0, 0x0155, "s" }, /* LATIN SMALL LETTER R WITH ACUTE */
  { "aacute",	0xE1, 0x00E1, "a" }, /* LATIN SMALL LETTER A WITH ACUTE */
  { "acirc",	0xE2, 0x00E2, "a" }, /* LATIN SMALL LETTER A WITH CIRCUMFLEX */
  { "",	0xE3, 0x0103, "a" }, /* LATIN SMALL LETTER A WITH BREVE */
  { "auml",	0xE4, 0x00E4, "a" }, /* LATIN SMALL LETTER A WITH DIAERESIS */
  { "",	0xE5, 0x013A, "l" }, /* LATIN SMALL LETTER L WITH ACUTE */
  { "",	0xE6, 0x0107, "c" }, /* LATIN SMALL LETTER C WITH ACUTE */
  { "ccedil",	0xE7, 0x00E7, "c" }, /* LATIN SMALL LETTER C WITH CEDILLA */
  { "",	0xE8, 0x010D, "c" }, /* LATIN SMALL LETTER C WITH CARON */
  { "eacute",	0xE9, 0x00E9, "e" }, /* LATIN SMALL LETTER E WITH ACUTE */
  { "",	0xEA, 0x0119, "e" }, /* LATIN SMALL LETTER E WITH OGONEK */
  { "euml",	0xEB, 0x00EB, "e" }, /* LATIN SMALL LETTER E WITH DIAERESIS */
  { "",	0xEC, 0x011B, "e" }, /* LATIN SMALL LETTER E WITH CARON */
  { "iacute",	0xED, 0x00ED, "i" }, /* LATIN SMALL LETTER I WITH ACUTE */
  { "icirc",	0xEE, 0x00EE, "i" }, /* LATIN SMALL LETTER I WITH CIRCUMFLEX */
  { "",	0xEF, 0x010F, "d" }, /* LATIN SMALL LETTER D WITH CARON */
  { "",	0xF0, 0x0111, "d" }, /* LATIN SMALL LETTER D WITH STROKE */
  { "",	0xF1, 0x0144, "n" }, /* LATIN SMALL LETTER N WITH ACUTE */
  { "",	0xF2, 0x0148, "n" }, /* LATIN SMALL LETTER N WITH CARON */
  { "oacute",	0xF3, 0x00F3, "o" }, /* LATIN SMALL LETTER O WITH ACUTE */
  { "ocirc",	0xF4, 0x00F4, "o" }, /* LATIN SMALL LETTER O WITH CIRCUMFLEX */
  { "",	0xF5, 0x0151, "o" }, /* LATIN SMALL LETTER O WITH DOUBLE ACUTE */
  { "ouml",	0xF6, 0x00F6, "o" }, /* LATIN SMALL LETTER O WITH DIAERESIS */
  { "divide",	0xF7, 0x00F7 }, /* DIVISION SIGN */
  { "",	0xF8, 0x0159, "r" }, /* LATIN SMALL LETTER R WITH CARON */
  { "",	0xF9, 0x016F, "u" }, /* LATIN SMALL LETTER U WITH RING ABOVE */
  { "uacute",	0xFA, 0x00FA, "u" }, /* LATIN SMALL LETTER U WITH ACUTE */
  { "",	0xFB, 0x0171, "u" }, /* LATIN SMALL LETTER U WITH DOUBLE ACUTE */
  { "uuml",	0xFC, 0x00FC, "u" }, /* LATIN SMALL LETTER U WITH DIAERESIS */
  { "yacute",	0xFD, 0x00FD, "y" }, /* LATIN SMALL LETTER Y WITH ACUTE */
  { "",	0xFE, 0x0163, "t" }, /* LATIN SMALL LETTER T WITH CEDILLA */
  { "",	0xFF, 0x02D9 }, /* DOT ABOVE (Mandarin Chinese light tone) */
  { NULL, 0, 0 }
};

/* Common map for koi8-u, koi8-r */
static iso_map_type koi8_map [] = {
  { "", 0xa3, 0x0415, "io"}, /* CYRILLIC SMALL LETTER IO */
  { "", 0xa4, 0x0454, "ie"}, /* CYRILLIC SMALL LETTER UKRAINIAN IE */
  { "", 0xa6, 0x0456, "i"}, /* CYRILLIC SMALL LETTER BYELORUSSIAN-UKRAINIAN I */
  { "", 0xa7, 0x0457, "yi"}, /* CYRILLIC SMALL LETTER YI */

  { "", 0xb3, 0x04d7, "IO"}, /* CYRILLIC CAPITAL LETTER IO */
  { "", 0xb4, 0x0404, "IE"}, /* CYRILLIC CAPITAL LETTER UKRAINIAN IE */
  { "", 0xb6, 0x0406, "I"},  /* CYRILLIC CAPITAL LETTER BYELORUSSIAN-UKRAINIAN I */
  { "", 0xb7, 0x0407, "YI"}, /* CYRILLIC CAPITAL LETTER YI */
/* { "", 0xbf, 0x}, / * CYRILLIC COPYRIGHT SIGN */ 
  { "", 0xc0, 0x042e, "yu"}, /* CYRILLIC SMALL LETTER YU */        
  { "", 0xc1, 0x0430, "a"}, /* CYRILLIC SMALL LETTER A */         
  { "", 0xc2, 0x0431, "b"}, /* CYRILLIC SMALL LETTER BE */        
  { "", 0xc3, 0x0446, "c"}, /* CYRILLIC SMALL LETTER TSE */        
  { "", 0xc4, 0x0434, "d"}, /* CYRILLIC SMALL LETTER DE */        
  { "", 0xc5, 0x0435, "e"}, /* CYRILLIC SMALL LETTER IE */        
  { "", 0xc6, 0x0444, "f"}, /* CYRILLIC SMALL LETTER EF */        
  { "", 0xc7, 0x0433, "g"}, /* CYRILLIC SMALL LETTER GHE */       
  { "", 0xc8, 0x0445, "h"}, /* CYRILLIC SMALL LETTER HA */        
  { "", 0xc9, 0x0438, "i"}, /* CYRILLIC SMALL LETTER I */         
  { "", 0xca, 0x0439, "i"}, /* CYRILLIC SMALL LETTER SHORT I */   
  { "", 0xcb, 0x043a, "k"}, /* CYRILLIC SMALL LETTER KA */        
  { "", 0xcc, 0x043b, "l"}, /* CYRILLIC SMALL LETTER EL */        
  { "", 0xcd, 0x043c, "m"}, /* CYRILLIC SMALL LETTER EM */        
  { "", 0xce, 0x043d, "n"}, /* CYRILLIC SMALL LETTER EN */        
  { "", 0xcf, 0x043e, "o"}, /* CYRILLIC SMALL LETTER O */         
  { "", 0xd0, 0x043f, "p"}, /* CYRILLIC SMALL LETTER PE */        
  { "", 0xd1, 0x044f, "ya"}, /* CYRILLIC SMALL LETTER YA */        
  { "", 0xd2, 0x0440, "r"}, /* CYRILLIC SMALL LETTER ER */        
  { "", 0xd3, 0x0441, "s"}, /* CYRILLIC SMALL LETTER ES */        
  { "", 0xd4, 0x0442, "t"}, /* CYRILLIC SMALL LETTER TE */        
  { "", 0xd5, 0x0443, "u"}, /* CYRILLIC SMALL LETTER U */         
  { "", 0xd6, 0x0436, "zh"}, /* CYRILLIC SMALL LETTER ZHE */       
  { "", 0xd7, 0x0432, "v"}, /* CYRILLIC SMALL LETTER VE */        
  { "", 0xd8, 0x044c, "x"}, /* CYRILLIC SMALL LETTER SOFT SIGN */ 
  { "", 0xd9, 0x044b, "y"}, /* CYRILLIC SMALL LETTER YERU */      
  { "", 0xda, 0x0437, "z"}, /* CYRILLIC SMALL LETTER ZE */        
  { "", 0xdb, 0x0448, "sh"}, /* CYRILLIC SMALL LETTER SHA */       
  { "", 0xdc, 0x044d, "e"}, /* CYRILLIC SMALL LETTER E */         
  { "", 0xdd, 0x0449, "shch"}, /* CYRILLIC SMALL LETTER SHCHA */     
  { "", 0xde, 0x0447, "ch"}, /* CYRILLIC SMALL LETTER CHA */       
  { "", 0xdf, 0x044a, "w"}, /* CYRILLIC SMALL LETTER HARD SIGN */ 
  { "", 0xe0, 0x042d, "YU"}, /* CYRILLIC CAPITAL LETTER YU */       
  { "", 0xe1, 0x0410, "A"}, /* CYRILLIC CAPITAL LETTER A */        
  { "", 0xe2, 0x0411, "B"}, /* CYRILLIC CAPITAL LETTER BE */         
  { "", 0xe3, 0x0426, "C"}, /* CYRILLIC CAPITAL LETTER TSE */        
  { "", 0xe4, 0x0414, "D"}, /* CYRILLIC CAPITAL LETTER DE */        
  { "", 0xe5, 0x0415, "E"}, /* CYRILLIC CAPITAL LETTER IE */        
  { "", 0xe6, 0x0424, "F"}, /* CYRILLIC CAPITAL LETTER EF */        
  { "", 0xe7, 0x0413, "G"}, /* CYRILLIC CAPITAL LETTER GHE */        
  { "", 0xe8, 0x0425, "H"}, /* CYRILLIC CAPITAL LETTER HA */        
  { "", 0xe9, 0x0418, "I"}, /* CYRILLIC CAPITAL LETTER I */        
  { "", 0xea, 0x0419, "I"}, /* CYRILLIC CAPITAL LETTER SHORT I */  
  { "", 0xeb, 0x041a, "K"}, /* CYRILLIC CAPITAL LETTER KA */        
  { "", 0xec, 0x041b, "L"}, /* CYRILLIC CAPITAL LETTER EL */        
  { "", 0xed, 0x041c, "M"}, /* CYRILLIC CAPITAL LETTER EM */        
  { "", 0xee, 0x041d, "N"}, /* CYRILLIC CAPITAL LETTER EN */        
  { "", 0xef, 0x041e, "O"}, /* CYRILLIC CAPITAL LETTER O */        
  { "", 0xf0, 0x041f, "P"}, /* CYRILLIC CAPITAL LETTER PE */        
  { "", 0xf1, 0x042f, "YA"}, /* CYRILLIC CAPITAL LETTER YA */       
  { "", 0xf2, 0x0420, "R"}, /* CYRILLIC CAPITAL LETTER ER */        
  { "", 0xf3, 0x0421, "S"}, /* CYRILLIC CAPITAL LETTER ES */        
  { "", 0xf4, 0x0422, "T"}, /* CYRILLIC CAPITAL LETTER TE */        
  { "", 0xf5, 0x0423, "U"}, /* CYRILLIC CAPITAL LETTER U */        
  { "", 0xf6, 0x0416, "ZH"}, /* CYRILLIC CAPITAL LETTER ZHE */       
  { "", 0xf7, 0x0412, "V"}, /* CYRILLIC CAPITAL LETTER VE */        
  { "", 0xf8, 0x042c, "X"}, /* CYRILLIC CAPITAL LETTER SOFT SIGN */
  { "", 0xf9, 0x042b, "Y"}, /* CYRILLIC CAPITAL LETTER YERU */     
  { "", 0xfa, 0x0417, "Z"}, /* CYRILLIC CAPITAL LETTER ZE */        
  { "", 0xfb, 0x0428, "SH"}, /* CYRILLIC CAPITAL LETTER SHA */       
  { "", 0xfc, 0x042d, "E"}, /* CYRILLIC CAPITAL LETTER E */        
  { "", 0xfd, 0x0429, "SHCH"}, /* CYRILLIC CAPITAL LETTER SHCHA */     
  { "", 0xfe, 0x0427, "CH"}, /* CYRILLIC CAPITAL LETTER CHE */      
  { "", 0xff, 0x042a, "W"}, /* CYRILLIC CAPITAL LETTER HARD SIGN */
  { NULL, 0, 0 }
};	      

encoding_type encoding_table[] = {
  { no_encoding, "(no encoding)", NULL },
  { US_ASCII,    "US-ASCII",    asis_map },
  { ISO_8859_1,  "iso-8859-1",  (iso_map_type *) iso8859_1_map },
  { ISO_8859_2,  "iso-8859-2",  (iso_map_type *) iso8859_2_map },
  { ISO_8859_3,  "iso-8859-3",  NULL },
  { ISO_8859_4,  "iso-8859-4",  NULL },
  { ISO_8859_5,  "iso-8859-5",  NULL },
  { ISO_8859_6,  "iso-8859-6",  NULL },
  { ISO_8859_7,  "iso-8859-7",  NULL },
  { ISO_8859_8,  "iso-8859-8",  NULL },
  { ISO_8859_9,  "iso-8859-9",  NULL },
  { ISO_8859_10, "iso-8859-10", NULL },
  { ISO_8859_11, "iso-8859-11", NULL },
  { ISO_8859_12, "iso-8859-12", NULL },
  { ISO_8859_13, "iso-8859-13", NULL },
  { ISO_8859_14, "iso-8859-14", NULL },
  { ISO_8859_15, "iso-8859-15", (iso_map_type *) iso8859_15_map },
  { KOI8_R,      "koi8-r",      (iso_map_type *) koi8_map },
  { KOI8_U,      "koi8-u",      (iso_map_type *) koi8_map },
  { UTF_8,       "utf-8",       asis_map },  /* specific code for this below */
  { last_encoding_code, NULL, NULL }
};


/* List of HTML entities.  */
static struct { const char *html; unsigned int unicode; } unicode_map[] = {
/* Extracted from http://www.w3.org/TR/html401/sgml/entities.html through
   sed -n -e 's|<!ENTITY \([^ ][^ ]*\) *CDATA "[&]#\([0-9][0-9]*\);".*|  { "\1", \2 },|p'
   | LC_ALL=C sort -k2  */
  { "AElig",     198 },
  { "Aacute",    193 },
  { "Acirc",     194 },
  { "Agrave",    192 },
  { "Alpha",     913 },
  { "Aring",     197 },
  { "Atilde",    195 },
  { "Auml",      196 },
  { "Beta",      914 },
  { "Ccedil",    199 },
  { "Chi",       935 },
  { "Dagger",   8225 },
  { "Delta",     916 },
  { "ETH",       208 },
  { "Eacute",    201 },
  { "Ecirc",     202 },
  { "Egrave",    200 },
  { "Epsilon",   917 },
  { "Eta",       919 },
  { "Euml",      203 },
  { "Gamma",     915 },
  { "Iacute",    205 },
  { "Icirc",     206 },
  { "Igrave",    204 },
  { "Iota",      921 },
  { "Iuml",      207 },
  { "Kappa",     922 },
  { "Lambda",    923 },
  { "Mu",        924 },
  { "Ntilde",    209 },
  { "Nu",        925 },
  { "OElig",     338 },
  { "Oacute",    211 },
  { "Ocirc",     212 },
  { "Ograve",    210 },
  { "Omega",     937 },
  { "Omicron",   927 },
  { "Oslash",    216 },
  { "Otilde",    213 },
  { "Ouml",      214 },
  { "Phi",       934 },
  { "Pi",        928 },
  { "Prime",    8243 },
  { "Psi",       936 },
  { "Rho",       929 },
  { "Scaron",    352 },
  { "Sigma",     931 },
  { "THORN",     222 },
  { "Tau",       932 },
  { "Theta",     920 },
  { "Uacute",    218 },
  { "Ucirc",     219 },
  { "Ugrave",    217 },
  { "Upsilon",   933 },
  { "Uuml",      220 },
  { "Xi",        926 },
  { "Yacute",    221 },
  { "Yuml",      376 },
  { "Zeta",      918 },
  { "aacute",    225 },
  { "acirc",     226 },
  { "acute",     180 },
  { "aelig",     230 },
  { "agrave",    224 },
  { "alefsym",  8501 },
  { "alpha",     945 },
  { "amp",        38 },
  { "and",      8743 },
  { "ang",      8736 },
  { "aring",     229 },
  { "asymp",    8776 },
  { "atilde",    227 },
  { "auml",      228 },
  { "bdquo",    8222 },
  { "beta",      946 },
  { "brvbar",    166 },
  { "bull",     8226 },
  { "cap",      8745 },
  { "ccedil",    231 },
  { "cedil",     184 },
  { "cent",      162 },
  { "chi",       967 },
  { "circ",      710 },
  { "clubs",    9827 },
  { "cong",     8773 },
  { "copy",      169 },
  { "crarr",    8629 },
  { "cup",      8746 },
  { "curren",    164 },
  { "dArr",     8659 },
  { "dagger",   8224 },
  { "darr",     8595 },
  { "deg",       176 },
  { "delta",     948 },
  { "diams",    9830 },
  { "divide",    247 },
  { "eacute",    233 },
  { "ecirc",     234 },
  { "egrave",    232 },
  { "empty",    8709 },
  { "emsp",     8195 },
  { "ensp",     8194 },
  { "epsilon",   949 },
  { "equiv",    8801 },
  { "eta",       951 },
  { "eth",       240 },
  { "euml",      235 },
  { "euro",     8364 },
  { "exist",    8707 },
  { "fnof",      402 },
  { "forall",   8704 },
  { "frac12",    189 },
  { "frac14",    188 },
  { "frac34",    190 },
  { "frasl",    8260 },
  { "gamma",     947 },
  { "ge",       8805 },
  { "gt",         62 },
  { "hArr",     8660 },
  { "harr",     8596 },
  { "hearts",   9829 },
  { "hellip",   8230 },
  { "iacute",    237 },
  { "icirc",     238 },
  { "iexcl",     161 },
  { "igrave",    236 },
  { "image",    8465 },
  { "infin",    8734 },
  { "int",      8747 },
  { "iota",      953 },
  { "iquest",    191 },
  { "isin",     8712 },
  { "iuml",      239 },
  { "kappa",     954 },
  { "lArr",     8656 },
  { "lambda",    955 },
  { "lang",     9001 },
  { "laquo",     171 },
  { "larr",     8592 },
  { "lceil",    8968 },
  { "ldquo",    8220 },
  { "le",       8804 },
  { "lfloor",   8970 },
  { "lowast",   8727 },
  { "loz",      9674 },
  { "lrm",      8206 },
  { "lsaquo",   8249 },
  { "lsquo",    8216 },
  { "lt",         60 },
  { "macr",      175 },
  { "mdash",    8212 },
  { "micro",     181 },
  { "middot",    183 },
  { "minus",    8722 },
  { "mu",        956 },
  { "nabla",    8711 },
  { "nbsp",      160 },
  { "ndash",    8211 },
  { "ne",       8800 },
  { "ni",       8715 },
  { "not",       172 },
  { "notin",    8713 },
  { "nsub",     8836 },
  { "ntilde",    241 },
  { "nu",        957 },
  { "oacute",    243 },
  { "ocirc",     244 },
  { "oelig",     339 },
  { "ograve",    242 },
  { "oline",    8254 },
  { "omega",     969 },
  { "omicron",   959 },
  { "oplus",    8853 },
  { "or",       8744 },
  { "ordf",      170 },
  { "ordm",      186 },
  { "oslash",    248 },
  { "otilde",    245 },
  { "otimes",   8855 },
  { "ouml",      246 },
  { "para",      182 },
  { "part",     8706 },
  { "permil",   8240 },
  { "perp",     8869 },
  { "phi",       966 },
  { "pi",        960 },
  { "piv",       982 },
  { "plusmn",    177 },
  { "pound",     163 },
  { "prime",    8242 },
  { "prod",     8719 },
  { "prop",     8733 },
  { "psi",       968 },
  { "quot",       34 },
  { "rArr",     8658 },
  { "radic",    8730 },
  { "rang",     9002 },
  { "raquo",     187 },
  { "rarr",     8594 },
  { "rceil",    8969 },
  { "rdquo",    8221 },
  { "real",     8476 },
  { "reg",       174 },
  { "rfloor",   8971 },
  { "rho",       961 },
  { "rlm",      8207 },
  { "rsaquo",   8250 },
  { "rsquo",    8217 },
  { "sbquo",    8218 },
  { "scaron",    353 },
  { "sdot",     8901 },
  { "sect",      167 },
  { "shy",       173 },
  { "sigma",     963 },
  { "sigmaf",    962 },
  { "sim",      8764 },
  { "spades",   9824 },
  { "sub",      8834 },
  { "sube",     8838 },
  { "sum",      8721 },
  { "sup",      8835 },
  { "sup1",      185 },
  { "sup2",      178 },
  { "sup3",      179 },
  { "supe",     8839 },
  { "szlig",     223 },
  { "tau",       964 },
  { "there4",   8756 },
  { "theta",     952 },
  { "thetasym",  977 },
  { "thinsp",   8201 },
  { "thorn",     254 },
  { "tilde",     732 },
  { "times",     215 },
  { "trade",    8482 },
  { "uArr",     8657 },
  { "uacute",    250 },
  { "uarr",     8593 },
  { "ucirc",     251 },
  { "ugrave",    249 },
  { "uml",       168 },
  { "upsih",     978 },
  { "upsilon",   965 },
  { "uuml",      252 },
  { "weierp",   8472 },
  { "xi",        958 },
  { "yacute",    253 },
  { "yen",       165 },
  { "yuml",      255 },
  { "zeta",      950 },
  { "zwj",      8205 },
  { "zwnj",     8204 }
};
 


/* To update this list of language codes, download the current data from
   http://www.loc.gov/standards/iso639-2; specifically,
   http://www.loc.gov/standards/iso639-2/ISO-639-2_values_8bits-8559-1.txt.
   Run cut -d\| -f 3,4 <ISO-639-2_values_8bits-8559-1.txt | sort -u >/tmp/639.2
   
   Extract the C table below to a file, say /tmp/lc, and run:
   cut -c 10- /tmp/lc| sed -e 's/", "/|/' -e 's,".*,,'>/tmp/lang.2
   
   Then: comm -3 /tmp/639.2 /tmp/lang.c.
   
   Also update the enum in lang.h.  */

language_type language_table[] = {
  { aa, "aa", "Afar" },
  { ab, "ab", "Abkhazian" },
  { ae, "ae", "Avestan" },
  { af, "af", "Afrikaans" },
  { ak, "ak", "Akan" },
  { am, "am", "Amharic" },
  { an, "an", "Aragonese" },
  { ar, "ar", "Arabic" },
  { as, "as", "Assamese" },
  { av, "av", "Avaric" },
  { ay, "ay", "Aymara" },
  { az, "az", "Azerbaijani" },
  { ba, "ba", "Bashkir" },
  { be, "be", "Belarusian" },
  { bg, "bg", "Bulgarian" },
  { bh, "bh", "Bihari" },
  { bi, "bi", "Bislama" },
  { bm, "bm", "Bambara" },
  { bn, "bn", "Bengali" },
  { bo, "bo", "Tibetan" },
  { br, "br", "Breton" },
  { bs, "bs", "Bosnian" },
  { ca, "ca", "Catalan; Valencian" },
  { ce, "ce", "Chechen" },
  { ch, "ch", "Chamorro" },
  { co, "co", "Corsican" },
  { cr, "cr", "Cree" },
  { cs, "cs", "Czech" },
  { cu, "cu", "Church Slavic; Old Slavonic; Church Slavonic; Old Bulgarian; Old Church Slavonic" },
  { cv, "cv", "Chuvash" },
  { cy, "cy", "Welsh" },
  { da, "da", "Danish" },
  { de, "de", "German" },
  { dv, "dv", "Divehi; Dhivehi; Maldivian" },
  { dz, "dz", "Dzongkha" },
  { ee, "ee", "Ewe" },
  { el, "el", "Greek, Modern (1453-)" },
  { en, "en", "English" },
  { eo, "eo", "Esperanto" },
  { es, "es", "Spanish; Castilian" },
  { et, "et", "Estonian" },
  { eu, "eu", "Basque" },
  { fa, "fa", "Persian" },
  { ff, "ff", "Fulah" },
  { fi, "fi", "Finnish" },
  { fj, "fj", "Fijian" },
  { fo, "fo", "Faroese" },
  { fr, "fr", "French" },
  { fy, "fy", "Western Frisian" },
  { ga, "ga", "Irish" },
  { gd, "gd", "Gaelic; Scottish Gaelic" },
  { gl, "gl", "Galician" },
  { gn, "gn", "Guarani" },
  { gu, "gu", "Gujarati" },
  { gv, "gv", "Manx" },
  { ha, "ha", "Hausa" },
  { he, "he", "Hebrew" } /* (formerly iw) */,
  { hi, "hi", "Hindi" },
  { ho, "ho", "Hiri Motu" },
  { hr, "hr", "Croatian" },
  { ht, "ht", "Haitian; Haitian Creole" },
  { hu, "hu", "Hungarian" },
  { hy, "hy", "Armenian" },
  { hz, "hz", "Herero" },
  { ia, "ia", "Interlingua (International Auxiliary Language Association)" },
  { id, "id", "Indonesian" } /* (formerly in) */,
  { ie, "ie", "Interlingue" },
  { ig, "ig", "Igbo" },
  { ii, "ii", "Sichuan Yi" },
  { ik, "ik", "Inupiaq" },
  { io, "io", "Ido" },
  { is, "is", "Icelandic" },
  { it, "it", "Italian" },
  { iu, "iu", "Inuktitut" },
  { ja, "ja", "Japanese" },
  { jv, "jv", "Javanese" },
  { ka, "ka", "Georgian" },
  { kg, "kg", "Kongo" },
  { ki, "ki", "Kikuyu; Gikuyu" },
  { kj, "kj", "Kuanyama; Kwanyama" },
  { kk, "kk", "Kazakh" },
  { kl, "kl", "Kalaallisut; Greenlandic" },
  { km, "km", "Central Khmer" },
  { kn, "kn", "Kannada" },
  { ko, "ko", "Korean" },
  { kr, "kr", "Kanuri" },
  { ks, "ks", "Kashmiri" },
  { ku, "ku", "Kurdish" },
  { kv, "kv", "Komi" },
  { kw, "kw", "Cornish" },
  { ky, "ky", "Kirghiz; Kyrgyz" },
  { la, "la", "Latin" },
  { lb, "lb", "Luxembourgish; Letzeburgesch" },
  { lg, "lg", "Ganda" },
  { li, "li", "Limburgan; Limburger; Limburgish" },
  { ln, "ln", "Lingala" },
  { lo, "lo", "Lao" },
  { lt, "lt", "Lithuanian" },
  { lu, "lu", "Luba-Katanga" },
  { lv, "lv", "Latvian" },
  { mg, "mg", "Malagasy" },
  { mh, "mh", "Marshallese" },
  { mi, "mi", "Maori" },
  { mk, "mk", "Macedonian" },
  { ml, "ml", "Malayalam" },
  { mn, "mn", "Mongolian" },
  { mo, "mo", "Moldavian" },
  { mr, "mr", "Marathi" },
  { ms, "ms", "Malay" },
  { mt, "mt", "Maltese" },
  { my, "my", "Burmese" },
  { na, "na", "Nauru" },
  { nb, "nb", "Bokmål, Norwegian; Norwegian Bokmål" },
  { nd, "nd", "Ndebele, North; North Ndebele" },
  { ne, "ne", "Nepali" },
  { ng, "ng", "Ndonga" },
  { nl, "nl", "Dutch; Flemish" },
  { nn, "nn", "Norwegian Nynorsk; Nynorsk, Norwegian" },
  { no, "no", "Norwegian" },
  { nr, "nr", "Ndebele, South; South Ndebele" },
  { nv, "nv", "Navajo; Navaho" },
  { ny, "ny", "Chichewa; Chewa; Nyanja" },
  { oc, "oc", "Occitan (post 1500); Provençal" },
  { oj, "oj", "Ojibwa" },
  { om, "om", "Oromo" },
  { or, "or", "Oriya" },
  { os, "os", "Ossetian; Ossetic" },
  { pa, "pa", "Panjabi; Punjabi" },
  { pi, "pi", "Pali" },
  { pl, "pl", "Polish" },
  { ps, "ps", "Pushto" },
  { pt, "pt", "Portuguese" },
  { qu, "qu", "Quechua" },
  { rm, "rm", "Romansh" },
  { rn, "rn", "Rundi" },
  { ro, "ro", "Romanian" },
  { ru, "ru", "Russian" },
  { rw, "rw", "Kinyarwanda" },
  { sa, "sa", "Sanskrit" },
  { sc, "sc", "Sardinian" },
  { sd, "sd", "Sindhi" },
  { se, "se", "Northern Sami" },
  { sg, "sg", "Sango" },
  { si, "si", "Sinhala; Sinhalese" },
  { sk, "sk", "Slovak" },
  { sl, "sl", "Slovenian" },
  { sm, "sm", "Samoan" },
  { sn, "sn", "Shona" },
  { so, "so", "Somali" },
  { sq, "sq", "Albanian" },
  { sr, "sr", "Serbian" },
  { ss, "ss", "Swati" },
  { st, "st", "Sotho, Souther" },
  { su, "su", "Sundanese" },
  { sv, "sv", "Swedish" },
  { sw, "sw", "Swahili" },
  { ta, "ta", "Tamil" },
  { te, "te", "Telugu" },
  { tg, "tg", "Tajik" },
  { th, "th", "Thai" },
  { ti, "ti", "Tigrinya" },
  { tk, "tk", "Turkmen" },
  { tl, "tl", "Tagalog" },
  { tn, "tn", "Tswana" },
  { to, "to", "Tonga (Tonga Islands)" },
  { tr, "tr", "Turkish" },
  { ts, "ts", "Tsonga" },
  { tt, "tt", "Tatar" },
  { tw, "tw", "Twi" },
  { ty, "ty", "Tahitian" },
  { ug, "ug", "Uighur; Uyghur" },
  { uk, "uk", "Ukrainian" },
  { ur, "ur", "Urdu" },
  { uz, "uz", "Uzbek" },
  { ve, "ve", "Venda" },
  { vi, "vi", "Vietnamese" },
  { vo, "vo", "Volapük" },
  { wa, "wa", "Walloon" },
  { wo, "wo", "Wolof" },
  { xh, "xh", "Xhosa" },
  { yi, "yi", "Yiddish" } /* (formerly ji) */,
  { yo, "yo", "Yoruba" },
  { za, "za", "Zhuang; Chuang" },
  { zh, "zh", "Chinese" },
  { zu, "zu", "Zulu" },
  { last_language_code, NULL, NULL }
};

static const char *
cm_search_iso_map_char (byte_t ch)
{
  int i;
  iso_map_type *iso = encoding_table[document_encoding_code].isotab;

  /* If no conversion table for this encoding, quit.  */
  if (!iso)
    return NULL;
  
  for (i = 0; iso[i].html; i++)
    if (iso[i].bytecode == ch)
      return iso[i].translit;

  return NULL;
}

const char *
lang_transliterate_char (byte_t ch)
{
  if (transliterate_file_names
      && document_encoding_code != no_encoding)
    return cm_search_iso_map_char (ch);
  return NULL;
}	



/* Given a language code LL_CODE, return a "default" country code (in
   new memory).  We use the same table as gettext, and return LL_CODE
   uppercased in the absence of any better possibility, with a warning.
   (gettext silently defaults to the C locale, but we want to give users
   a shot at fixing ambiguities.)  We also return en_US for en, while
   gettext does not.  */

#define SIZEOF(a) (sizeof(a) / sizeof(a[0]))

static char *
default_country_for_lang (const char *ll_code)
{
  /* This table comes from msginit.c in gettext (0.16.1).  It is
     intentionally copied verbatim even though the format is not the
     most convenient for our purposes, to make updates simple.  */
  static const char *locales_with_principal_territory[] = {
		/* Language	Main territory */
    "ace_ID",	/* Achinese	Indonesia */
    "af_ZA",	/* Afrikaans	South Africa */
    "ak_GH",	/* Akan		Ghana */
    "am_ET",	/* Amharic	Ethiopia */
    "an_ES",	/* Aragonese	Spain */
    "ang_GB",	/* Old English	Britain */
    "as_IN",	/* Assamese	India */
    "av_RU",	/* Avaric	Russia */
    "awa_IN",	/* Awadhi	India */
    "az_AZ",	/* Azerbaijani	Azerbaijan */
    "bad_CF",	/* Banda	Central African Republic */
    "ban_ID",	/* Balinese	Indonesia */
    "be_BY",	/* Belarusian	Belarus */
    "bem_ZM",	/* Bemba	Zambia */
    "bg_BG",	/* Bulgarian	Bulgaria */
    "bho_IN",	/* Bhojpuri	India */
    "bik_PH",	/* Bikol	Philippines */
    "bin_NG",	/* Bini		Nigeria */
    "bm_ML",	/* Bambara	Mali */
    "bn_IN",	/* Bengali	India */
    "bo_CN",	/* Tibetan	China */
    "br_FR",	/* Breton	France */
    "bs_BA",	/* Bosnian	Bosnia */
    "btk_ID",	/* Batak	Indonesia */
    "bug_ID",	/* Buginese	Indonesia */
    "ca_ES",	/* Catalan	Spain */
    "ce_RU",	/* Chechen	Russia */
    "ceb_PH",	/* Cebuano	Philippines */
    "co_FR",	/* Corsican	France */
    "cr_CA",	/* Cree		Canada */
    "cs_CZ",	/* Czech	Czech Republic */
    "csb_PL",	/* Kashubian	Poland */
    "cy_GB",	/* Welsh	Britain */
    "da_DK",	/* Danish	Denmark */
    "de_DE",	/* German	Germany */
    "din_SD",	/* Dinka	Sudan */
    "doi_IN",	/* Dogri	India */
    "dv_MV",	/* Divehi	Maldives */
    "dz_BT",	/* Dzongkha	Bhutan */
    "ee_GH",	/* Ã‰wÃ©		Ghana */
    "el_GR",	/* Greek	Greece */
    /* Don't put "en_GB" or "en_US" here.  That would be asking for fruitless
       political discussion.  */
    "es_ES",	/* Spanish	Spain */
    "et_EE",	/* Estonian	Estonia */
    "fa_IR",	/* Persian	Iran */
    "fi_FI",	/* Finnish	Finland */
    "fil_PH",	/* Filipino	Philippines */
    "fj_FJ",	/* Fijian	Fiji */
    "fo_FO",	/* Faroese	Faeroe Islands */
    "fon_BJ",	/* Fon		Benin */
    "fr_FR",	/* French	France */
    "fy_NL",	/* Western Frisian	Netherlands */
    "ga_IE",	/* Irish	Ireland */
    "gd_GB",	/* Scots	Britain */
    "gon_IN",	/* Gondi	India */
    "gsw_CH",	/* Swiss German	Switzerland */
    "gu_IN",	/* Gujarati	India */
    "he_IL",	/* Hebrew	Israel */
    "hi_IN",	/* Hindi	India */
    "hil_PH",	/* Hiligaynon	Philippines */
    "hr_HR",	/* Croatian	Croatia */
    "ht_HT",	/* Haitian	Haiti */
    "hu_HU",	/* Hungarian	Hungary */
    "hy_AM",	/* Armenian	Armenia */
    "id_ID",	/* Indonesian	Indonesia */
    "ig_NG",	/* Igbo		Nigeria */
    "ii_CN",	/* Sichuan Yi	China */
    "ilo_PH",	/* Iloko	Philippines */
    "is_IS",	/* Icelandic	Iceland */
    "it_IT",	/* Italian	Italy */
    "ja_JP",	/* Japanese	Japan */
    "jab_NG",	/* Hyam		Nigeria */
    "jv_ID",	/* Javanese	Indonesia */
    "ka_GE",	/* Georgian	Georgia */
    "kab_DZ",	/* Kabyle	Algeria */
    "kaj_NG",	/* Jju		Nigeria */
    "kam_KE",	/* Kamba	Kenya */
    "kmb_AO",	/* Kimbundu	Angola */
    "kcg_NG",	/* Tyap		Nigeria */
    "kdm_NG",	/* Kagoma	Nigeria */
    "kg_CD",	/* Kongo	Democratic Republic of Congo */
    "kk_KZ",	/* Kazakh	Kazakhstan */
    "kl_GL",	/* Kalaallisut	Greenland */
    "km_KH",	/* Khmer	Cambodia */
    "kn_IN",	/* Kannada	India */
    "ko_KR",	/* Korean	Korea (South) */
    "kok_IN",	/* Konkani	India */
    "kr_NG",	/* Kanuri	Nigeria */
    "kru_IN",	/* Kurukh	India */
    "lg_UG",	/* Ganda	Uganda */
    "li_BE",	/* Limburgish	Belgium */
    "lo_LA",	/* Laotian	Laos */
    "lt_LT",	/* Lithuanian	Lithuania */
    "lu_CD",	/* Luba-Katanga	Democratic Republic of Congo */
    "lua_CD",	/* Luba-Lulua	Democratic Republic of Congo */
    "luo_KE",	/* Luo		Kenya */
    "lv_LV",	/* Latvian	Latvia */
    "mad_ID",	/* Madurese	Indonesia */
    "mag_IN",	/* Magahi	India */
    "mai_IN",	/* Maithili	India */
    "mak_ID",	/* Makasar	Indonesia */
    "man_ML",	/* Mandingo	Mali */
    "men_SL",	/* Mende	Sierra Leone */
    "mg_MG",	/* Malagasy	Madagascar */
    "min_ID",	/* Minangkabau	Indonesia */
    "mk_MK",	/* Macedonian	Macedonia */
    "ml_IN",	/* Malayalam	India */
    "mn_MN",	/* Mongolian	Mongolia */
    "mni_IN",	/* Manipuri	India */
    "mos_BF",	/* Mossi	Burkina Faso */
    "mr_IN",	/* Marathi	India */
    "ms_MY",	/* Malay	Malaysia */
    "mt_MT",	/* Maltese	Malta */
    "mwr_IN",	/* Marwari	India */
    "my_MM",	/* Burmese	Myanmar */
    "na_NR",	/* Nauru	Nauru */
    "nah_MX",	/* Nahuatl	Mexico */
    "nap_IT",	/* Neapolitan	Italy */
    "nb_NO",	/* Norwegian BokmÃ¥l	Norway */
    "nds_DE",	/* Low Saxon	Germany */
    "ne_NP",	/* Nepali	Nepal */
    "nl_NL",	/* Dutch	Netherlands */
    "nn_NO",	/* Norwegian Nynorsk	Norway */
    "no_NO",	/* Norwegian	Norway */
    "nr_ZA",	/* South Ndebele	South Africa */
    "nso_ZA",	/* Northern Sotho	South Africa */
    "nym_TZ",	/* Nyamwezi	Tanzania */
    "nyn_UG",	/* Nyankole	Uganda */
    "oc_FR",	/* Occitan	France */
    "oj_CA",	/* Ojibwa	Canada */
    "or_IN",	/* Oriya	India */
    "pa_IN",	/* Punjabi	India */
    "pag_PH",	/* Pangasinan	Philippines */
    "pam_PH",	/* Pampanga	Philippines */
    "pbb_CO",	/* PÃ¡ez		Colombia */
    "pl_PL",	/* Polish	Poland */
    "ps_AF",	/* Pashto	Afghanistan */
    "pt_PT",	/* Portuguese	Portugal */
    "raj_IN",	/* Rajasthani	India */
    "rm_CH",	/* Rhaeto-Roman	Switzerland */
    "rn_BI",	/* Kirundi	Burundi */
    "ro_RO",	/* Romanian	Romania */
    "ru_RU",	/* Russian	Russia */
    "sa_IN",	/* Sanskrit	India */
    "sas_ID",	/* Sasak	Indonesia */
    "sat_IN",	/* Santali	India */
    "sc_IT",	/* Sardinian	Italy */
    "scn_IT",	/* Sicilian	Italy */
    "sg_CF",	/* Sango	Central African Republic */
    "shn_MM",	/* Shan		Myanmar */
    "si_LK",	/* Sinhala	Sri Lanka */
    "sid_ET",	/* Sidamo	Ethiopia */
    "sk_SK",	/* Slovak	Slovakia */
    "sl_SI",	/* Slovenian	Slovenia */
    "so_SO",	/* Somali	Somalia */
    "sq_AL",	/* Albanian	Albania */
    "sr_RS",	/* Serbian	Serbia */
    "sr_YU",	/* Serbian	Yugoslavia */
    "srr_SN",	/* Serer	Senegal */
    "suk_TZ",	/* Sukuma	Tanzania */
    "sus_GN",	/* Susu		Guinea */
    "sv_SE",	/* Swedish	Sweden */
    "te_IN",	/* Telugu	India */
    "tem_SL",	/* Timne	Sierra Leone */
    "tet_ID",	/* Tetum	Indonesia */
    "tg_TJ",	/* Tajik	Tajikistan */
    "th_TH",	/* Thai		Thailand */
    "tiv_NG",	/* Tiv		Nigeria */
    "tk_TM",	/* Turkmen	Turkmenistan */
    "tl_PH",	/* Tagalog	Philippines */
    "to_TO",	/* Tonga	Tonga */
    "tr_TR",	/* Turkish	Turkey */
    "tum_MW",	/* Tumbuka	Malawi */
    "uk_UA",	/* Ukrainian	Ukraine */
    "umb_AO",	/* Umbundu	Angola */
    "ur_PK",	/* Urdu		Pakistan */
    "uz_UZ",	/* Uzbek	Uzbekistan */
    "ve_ZA",	/* Venda	South Africa */
    "vi_VN",	/* Vietnamese	Vietnam */
    "wa_BE",	/* Walloon	Belgium */
    "wal_ET",	/* Walamo	Ethiopia */
    "war_PH",	/* Waray	Philippines */
    "wen_DE",	/* Sorbian	Germany */
    "yao_MW",	/* Yao		Malawi */
    "zap_MX"	/* Zapotec	Mexico */
  };
  int c;
  int cc_len;
  int ll_len = strlen (ll_code);
  char *cc_code = xmalloc (ll_len + 1 + 1);
  int principal_len = SIZEOF (locales_with_principal_territory);
  
  strcpy (cc_code, ll_code);
  strcat (cc_code, "_");
  cc_len = ll_len + 1;
  
  for (c = 0; c < principal_len; c++)
    {
      const char *principal_locale = locales_with_principal_territory[c];
      if (strncmp (principal_locale, cc_code, cc_len) == 0)
        {
          const char *underscore = strchr (principal_locale, '_');
          /* should always be there, but in case ... */
          free (cc_code);
          cc_code = xstrdup (underscore ? underscore + 1 : principal_locale);
          break;
        }
    }
  
  /* If we didn't find one to copy, warn and duplicate.  */
  if (c == principal_len)
    {
      if (mbscasecmp (ll_code, "en") == 0)
        cc_code = xstrdup ("en_US");
      else
        {
          warning (_("no default territory known for language `%s'"), ll_code);
          for (c = 0; c < strlen (ll_code); c++)
            cc_code[c] = toupper (ll_code[c]);
          cc_code[c] = 0;
          /* We're probably wasting a byte, oops.  */
        }
    }
  
  return cc_code;
}



/* @documentlanguage ARG.  We set the global `document_language' (which
   `getdocumenttext' uses) to the gettextish string, and 
   `language_code' to the corresponding enum value.  */

void
cm_documentlanguage (void)
{
  language_code_type c;
  char *lang_arg, *ll_part, *cc_part, *locale_string;
  char *underscore;

  /* Read the line with the language code on it.  */
  get_rest_of_line (0, &lang_arg);

  /* What we're passed might be either just a language code LL (we'll 
     also need it with the usual _CC appended, if there is one), or a
     locale name as used by gettext, LL_CC (we'll also need its
     constituents separately).  */
  underscore = strchr (lang_arg, '_');
  if (underscore)
    {
      ll_part = substring (lang_arg, underscore);
      cc_part = xstrdup (underscore + 1);
      locale_string = xstrdup (lang_arg);
    }
  else
    {
      ll_part = xstrdup (lang_arg);
      cc_part = default_country_for_lang (ll_part);
      locale_string = xmalloc (strlen (ll_part) + 1 + strlen (cc_part) + 1);
      strcpy (locale_string, ll_part);
      strcat (locale_string, "_");
      strcat (locale_string, cc_part);
    }

  /* Done with analysis of user-supplied string.  */
  free (lang_arg);
  
  /* Linear search is fine these days.  */
  for (c = aa; c != last_language_code; c++)
    {
      if (strcmp (ll_part, language_table[c].abbrev) == 0)
        { /* Set current language code.  */
          language_code = c;
          break;
        }
    }

  /* If we didn't find this code, complain.  */
  if (c == last_language_code)
    warning (_("%s is not a valid ISO 639 language code"), ll_part);

  /* Set the language our `getdocumenttext' function uses for
     translating document strings.  */
  document_language = xstrdup (locale_string);
  free (locale_string);
  
  if (xml && !docbook)
    { /* According to http://www.opentag.com/xfaq_lang.htm, xml:lang
         takes an ISO 639 language code, optionally followed by a dash
         (not underscore) and an ISO 3166 country code.  So we have
         to make another version with - instead of _.  */
      char *xml_locale = xmalloc (strlen (ll_part) + 1 + strlen (cc_part) + 1);
      strcpy (xml_locale, ll_part);
      strcat (xml_locale, "-");
      strcat (xml_locale, cc_part);
      xml_insert_element_with_attribute (DOCUMENTLANGUAGE, START,
                                         "xml:lang=\"%s\"", xml_locale);
      xml_insert_element (DOCUMENTLANGUAGE, END);
      free (xml_locale);
    }

  free (ll_part);
  free (cc_part);
}



/* Search through the encoding table for the given character, returning
   its equivalent.  */

static int
cm_search_iso_map (char *html)
{
  if (document_encoding_code == UTF_8)
    {
      /* Binary search in unicode_map.  */
      size_t low = 0;
      size_t high = sizeof (unicode_map) / sizeof (unicode_map[0]);

      /* At each loop iteration, low < high; for indices < low the values are
         smaller than HTML; for indices >= high the values are greater than HTML.
         So, if HTML occurs in the list, it is at  low <= position < high.  */
      do
        {
          size_t mid = low + (high - low) / 2; /* low <= mid < high */
          int cmp = strcmp (unicode_map[mid].html, html);

          if (cmp < 0)
            low = mid + 1;
          else if (cmp > 0)
            high = mid;
          else /* cmp == 0 */
            return unicode_map[mid].unicode;
        }
      while (low < high);

      return -1;
    }
  else
    {
      int i;
      iso_map_type *iso = encoding_table[document_encoding_code].isotab;

      /* If no conversion table for this encoding, quit.  */
      if (!iso)
        return -1;

      for (i = 0; iso[i].html; i++)
        {
          if (strcmp (html, iso[i].html) == 0)
            return i;
        }

      return -1;
    }
}


/* @documentencoding.  Set the translation table.  */

void
cm_documentencoding (void)
{
  if (!handling_delayed_writes)
    {
      encoding_code_type enc;
      char *enc_arg;

      /* This is ugly and probably needs to apply to other commands'
         argument parsing as well.  When we're doing @documentencoding,
         we're generally in the frontmatter of the document, and so the.
         expansion in html/xml/docbook would generally be the empty string.
         (Because those modes wait until the first normal text of the
         document to start outputting.)  The result would thus be a warning
         "unrecognized encoding name `'".  Sigh.  */
      int save_html = html;
      int save_xml = xml;

      html = 0;
      xml = 0;
      get_rest_of_line (1, &enc_arg);
      html = save_html;
      xml = save_xml;

      /* See if we have this encoding.  */
      for (enc = no_encoding+1; enc != last_encoding_code; enc++)
        {
          if (mbscasecmp (enc_arg, encoding_table[enc].encname) == 0)
            {
              document_encoding_code = enc;
              break;
            }
        }

      /* If we didn't find this code, complain.  */
      if (enc == last_encoding_code)
        {
          warning (_("unrecognized encoding name `%s'"), enc_arg);
          /* Let the previous one go.  */
          if (unknown_encoding && *unknown_encoding)
            free (unknown_encoding);
          unknown_encoding = xstrdup (enc_arg);
        }

      else if (encoding_table[document_encoding_code].isotab == NULL)
        warning (_("sorry, encoding `%s' not supported"), enc_arg);

      free (enc_arg);
    }
  else if (xml)
    {
      char *encoding = current_document_encoding ();

      if (encoding && *encoding)
        {
          insert_string (" encoding=\"");
          insert_string (encoding);
          insert_string ("\"");
        }

      free (encoding);
    }
}

char *
current_document_encoding (void)
{
  if (document_encoding_code != no_encoding)
    return xstrdup (encoding_table[document_encoding_code].encname);
  else if (unknown_encoding && *unknown_encoding)
    return xstrdup (unknown_encoding);
  else
    return xstrdup ("");
}


/* Add RC per the current encoding.  */

static void
add_encoded_char_from_code (int rc)
{
  if (document_encoding_code == UTF_8)
    {
      if (rc < 0x80)
        add_char (rc);
      else if (rc < 0x800)
        {
          add_char (0xc0 | (rc >> 6));
          add_char (0x80 | (rc & 0x3f));
        }
      else if (rc < 0x10000)
        {
          add_char (0xe0 | (rc >> 12));
          add_char (0x80 | ((rc >> 6) & 0x3f));
          add_char (0x80 | (rc & 0x3f));
        }
      else
        {
          add_char (0xf0 | (rc >> 18));
          add_char (0x80 | ((rc >> 12) & 0x3f));
          add_char (0x80 | ((rc >> 6) & 0x3f));
          add_char (0x80 | (rc & 0x3f));
        }
    }
  else
    add_char (encoding_table[document_encoding_code].isotab[rc].bytecode);
}


/* If html or xml output, add &HTML_STR; to the output.  If not html and
   the user requested encoded output, add the real 8-bit character
   corresponding to HTML_STR from the translation tables.  Otherwise,
   add INFO_STR.  */

static void
add_encoded_char (char *html_str, char *info_str)
{
  if (html)
    add_word_args ("&%s;", html_str);
  else if (xml)
    xml_insert_entity (html_str);
  else if (enable_encoding && document_encoding_code != no_encoding)
    {
      /* Look for HTML_STR in the current translation table.  */
      int rc = cm_search_iso_map (html_str);
      if (rc >= 0)
        /* We found it, add the real character.  */
        add_encoded_char_from_code (rc);
      else
        { /* We didn't find it, that seems bad.  */
          warning (_("invalid encoded character `%s'"), html_str);
          add_word (info_str);
        }
    }
  else
    add_word (info_str);
}



/* Output an accent for HTML or XML. */

static void
cm_accent_generic_html (int arg, int start, int end, char *html_supported,
    int single, int html_solo_standalone, char *html_solo)
{
  static int valid_html_accent; /* yikes */

  if (arg == START)
    { /* If HTML has good support for this character, use it.  */
      if (strchr (html_supported, curchar ()))
        { /* Yes; start with an ampersand.  The character itself
             will be added later in read_command (makeinfo.c).  */
	  int saved_escape_html = escape_html;
	  escape_html = 0;
          valid_html_accent = 1;
          add_char ('&');
	  escape_html = saved_escape_html;
        }
      else
        { /* @dotless{i} is not listed in html_supported but HTML entities
	     starting with `i' can be used, such as &icirc;.  */
	  int save_input_text_offset = input_text_offset;
	  char *accent_contents;

	  get_until_in_braces ("\n", &accent_contents);
	  canon_white (accent_contents);

	  if (strstr (accent_contents, "@dotless{i"))
	    {
	      add_word_args ("&%c", accent_contents[9]);
	      valid_html_accent = 1;
	    }
	  else
	    {
	      /* Search for @dotless{} wasn't successful, so rewind.  */
	      input_text_offset = save_input_text_offset;
	      valid_html_accent = 0;
	      if (html_solo_standalone)
		{ /* No special HTML support, so produce standalone char.  */
		  if (xml)
		    xml_insert_entity (html_solo);
		  else
		    add_word_args ("&%s;", html_solo);
		}
	      else
		/* If the html_solo does not exist as standalone character
		   (namely &circ; &grave; &tilde;), then we use
		   the single character version instead.  */
		add_char (single);
	    }

	  free (accent_contents);
        }
    }
  else if (arg == END)
    { /* Only if we saw a valid_html_accent can we use the full
         HTML accent (umlaut, grave ...).  */
      if (valid_html_accent)
        {
          add_word (html_solo);
          add_char (';');
        }
    }
}


/* If END is zero, there is nothing in the paragraph to accent.  This
   can happen when we're in a menu with an accent command and
   --no-headers is given, so the base character is not added.  In this
   case we're not producing any output anyway, so just forget it.
   Otherwise, produce the ASCII version of the accented char.  */
static void
cm_accent_generic_no_headers (int arg, int start, int end, int single,
    char *html_solo)
{
  if (arg == END && end > 0)
    {
      if (no_encoding)
        add_char (single);
      else
        {
          int rc;
          char *buffer = xmalloc (1 + strlen (html_solo) + 1);
	  assert (end > 0);
          buffer[0] = output_paragraph[end - 1];
          buffer[1] = 0;
          strcat (buffer, html_solo);

          rc = cm_search_iso_map (buffer);
          if (rc >= 0)
            {
              /* Here we replace the character which has
                 been inserted in read_command with
                 the value we have found in the conversion table.  */
              output_paragraph_offset--;
              add_encoded_char_from_code (rc);
            }
          else
            { /* If we didn't find a translation for this character,
                 put the single instead. E.g., &Xuml; does not exist so X&uml;
                 should be produced. */
              /* When the below warning is issued, an author has nothing
                 wrong in their document, let alone anything ``fixable''
                 on their side.  So it is commented out for now.  */
              /* warning (_("%s is an invalid ISO code, using %c"),
                       buffer, single); */
              add_char (single);
            }

          free (buffer);
        }
    }
}



/* Accent commands that take explicit arguments and don't have any
   special HTML support.  */

void
cm_accent (int arg)
{
  int old_escape_html = escape_html;
  escape_html = 0;
  if (arg == START)
    {
      /* Must come first to avoid ambiguity with overdot.  */
      if (strcmp (command, "udotaccent") == 0)      /* underdot */
        add_char ('.');
    }
  else if (arg == END)
    {
      if (strcmp (command, "=") == 0)               /* macron */
        add_word ((html || xml) ? "&macr;" : "=");
      else if (strcmp (command, "H") == 0)          /* Hungarian umlaut */
        add_word ("''");
      else if (strcmp (command, "dotaccent") == 0)  /* overdot */
        add_meta_char ('.');
      else if (strcmp (command, "ringaccent") == 0) /* ring */
        add_char ('*');
      else if (strcmp (command, "tieaccent") == 0)  /* long tie */
        add_char ('[');
      else if (strcmp (command, "u") == 0)          /* breve */
        add_char ('(');
      else if (strcmp (command, "ubaraccent") == 0) /* underbar */
        add_char ('_');
      else if (strcmp (command, "v") == 0)          /* hacek/check */
        add_word ((html || xml) ? "&lt;" : "<");
    }
  escape_html = old_escape_html;
}

/* Common routine for the accent characters that have support in HTML.
   If the character being accented is in the HTML_SUPPORTED set, then
   produce &CHTML_SOLO;, for example, &Auml; for an A-umlaut.  If not in
   HTML_SUPPORTED, just produce &HTML_SOLO;X for the best we can do with
   at an X-umlaut.  If not producing HTML, just use SINGLE, a
   character such as " which is the best plain text representation we
   can manage.  If HTML_SOLO_STANDALONE is nonzero the given HTML_SOLO
   exists as valid standalone character in HTML, e.g., &uml;.  */

static void
cm_accent_generic (int arg, int start, int end, char *html_supported,
    int single, int html_solo_standalone, char *html_solo)
{
  /* Accentuating space characters makes no sense, so issue a warning.  */
  if (arg == START && isspace (input_text[input_text_offset]))
    warning ("Accent command `@%s' must not be followed by whitespace",
        command);

  if (html || xml)
    cm_accent_generic_html (arg, start, end, html_supported,
                            single, html_solo_standalone, html_solo);
  else if (no_headers)
    cm_accent_generic_no_headers (arg, start, end, single, html_solo);
  else if (arg == END)
    {
      if (enable_encoding)
        /* use 8-bit if available */
        cm_accent_generic_no_headers (arg, start, end, single, html_solo);
      else
        /* use regular character */
        add_char (single);
    }
}

void
cm_accent_umlaut (int arg, int start, int end)
{
  cm_accent_generic (arg, start, end, "aouAOUEeIiy", '"', 1, "uml");
}

void
cm_accent_acute (int arg, int start, int end)
{
  cm_accent_generic (arg, start, end, "AEIOUYaeiouy", '\'', 1, "acute");
}

void
cm_accent_cedilla (int arg, int start, int end)
{
  cm_accent_generic (arg, start, end, "Cc", ',', 1, "cedil");
}

void
cm_accent_hat (int arg, int start, int end)
{
  cm_accent_generic (arg, start, end, "AEIOUaeiou", '^', 0, "circ");
}

void
cm_accent_grave (int arg, int start, int end)
{
  cm_accent_generic (arg, start, end, "AEIOUaeiou", '`', 0, "grave");
}

void
cm_accent_tilde (int arg, int start, int end)
{
  cm_accent_generic (arg, start, end, "ANOano", '~', 0, "tilde");
}



/* Non-English letters/characters that don't insert themselves.  */
void
cm_special_char (int arg)
{
  int old_escape_html = escape_html;
  escape_html = 0;

  if (arg == START)
    {
      if ((*command == 'L' || *command == 'l'
           || *command == 'O' || *command == 'o')
          && command[1] == 0)
        { /* Lslash lslash Oslash oslash.
             Lslash and lslash aren't supported in HTML.  */
          if (command[0] == 'O')
            add_encoded_char ("Oslash", "/O");
          else if (command[0] == 'o')
            add_encoded_char ("oslash", "/o");
          else
            add_word_args ("/%c", command[0]);
        }
      else if (strcmp (command, "exclamdown") == 0)
        add_encoded_char ("iexcl", "!");
      else if (strcmp (command, "questiondown") == 0)
        add_encoded_char ("iquest", "?");
      else if (strcmp (command, "euro") == 0)
        /* http://www.cs.tut.fi/~jkorpela/html/euro.html suggests that
           &euro; degrades best in old browsers.  */
        add_encoded_char ("euro", "Euro ");
      else if (strcmp (command, "pounds") == 0)
        add_encoded_char ("pound" , "#");
      else if (strcmp (command, "ordf") == 0)
        add_encoded_char ("ordf" , "a");
      else if (strcmp (command, "ordm") == 0)
        add_encoded_char ("ordm" , "o");
      else if (strcmp (command, "textdegree") == 0)
        add_encoded_char ("deg" , "o");
      else if (strcmp (command, "AE") == 0)
        add_encoded_char ("AElig", command);
      else if (strcmp (command, "ae") == 0)
        add_encoded_char ("aelig",  command);
      else if (strcmp (command, "OE") == 0)
        add_encoded_char ("OElig", command);
      else if (strcmp (command, "oe") == 0)
        add_encoded_char ("oelig", command);
      else if (strcmp (command, "AA") == 0)
        add_encoded_char ("Aring", command);
      else if (strcmp (command, "aa") == 0)
        add_encoded_char ("aring", command);
      else if (strcmp (command, "ss") == 0)
        add_encoded_char ("szlig", command);
      else if (strcmp (command, "guillemetleft") == 0
               || strcmp (command, "guillemotleft") == 0)
        add_encoded_char ("laquo", "<<");
      else if (strcmp (command, "guillemetright") == 0
               || strcmp (command, "guillemotright") == 0)
        add_encoded_char ("raquo", ">>");
      else
        line_error ("cm_special_char internal error: command=@%s", command);
    }
  escape_html = old_escape_html;
}

/* Dotless i or j.  */
void
cm_dotless (int arg, int start, int end)
{
  if (arg == END)
    {
      xml_no_para --;
      if (output_paragraph[start] != 'i' && output_paragraph[start] != 'j')
        /* This error message isn't perfect if the argument is multiple
           characters, but it doesn't seem worth getting right.  */
        line_error (_("%c%s expects `i' or `j' as argument, not `%c'"),
                    COMMAND_PREFIX, command, output_paragraph[start]);

      else if (end - start != 1)
        line_error (_("%c%s expects a single character `i' or `j' as argument"),
                    COMMAND_PREFIX, command);

      /* We've already inserted the `i' or `j', so nothing to do.  */
    }
  else
    xml_no_para ++;
}
