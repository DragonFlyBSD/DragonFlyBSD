/* lang.h -- declarations for language codes etc.
   $Id: lang.h,v 1.14 2007/11/21 23:02:22 karl Exp $

   Copyright (C) 1999, 2001, 2002, 2003, 2006, 2007
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

#ifndef LANG_H
#define LANG_H

/* The language code which can be changed through @documentlanguage
   These code are the ISO-639 two letter codes.  */

#undef hz /* AIX 4.3.3 */
typedef enum
{ 
  aa, ab, ae, af, ak, am, an, ar, as, av, ay, az, ba, be, bg, bh, bi,
  bm, bn, bo, br, bs, ca, ce, ch, co, cr, cs, cu, cv, cy, da, de, dv,
  dz, ee, el, en, eo, es, et, eu, fa, ff, fi, fj, fo, fr, fy, ga, gd,
  gl, gn, gu, gv, ha, he, hi, ho, hr, ht, hu, hy, hz, ia, id, ie, ig,
  ii, ik, io, is, it, iu, ja, jv, ka, kg, ki, kj, kk, kl, km, kn, ko,
  kr, ks, ku, kv, kw, ky, la, lb, lg, li, ln, lo, lt, lu, lv, mg, mh,
  mi, mk, ml, mn, mo, mr, ms, mt, my, na, nb, nd, ne, ng, nl, nn, no,
  nr, nv, ny, oc, oj, om, or, os, pa, pi, pl, ps, pt, qu, rm, rn, ro,
  ru, rw, sa, sc, sd, se, sg, si, sk, sl, sm, sn, so, sq, sr, ss, st,
  su, sv, sw, ta, te, tg, th, ti, tk, tl, tn, to, tr, ts, tt, tw, ty,
  ug, uk, ur, uz, ve, vi, vo, wa, wo, xh, yi, yo, za, zh, zu,
  last_language_code
} language_code_type;

/* The current language code.  */
extern language_code_type language_code;


/* Information for each language.  */
typedef struct
{
  language_code_type lc; /* language code as enum type */
  char *abbrev;          /* two letter language code */
  char *desc;            /* full name for language code */
} language_type;

extern language_type language_table[];



/* The document encoding. This is useful to produce true 8-bit
   characters according to the @documentencoding.  */

typedef enum {
  no_encoding,
  US_ASCII,
  ISO_8859_1,
  ISO_8859_2,
  ISO_8859_3,    /* this and none of the rest are supported. */
  ISO_8859_4,
  ISO_8859_5,
  ISO_8859_6,
  ISO_8859_7,
  ISO_8859_8,
  ISO_8859_9,
  ISO_8859_10,
  ISO_8859_11,
  ISO_8859_12,
  ISO_8859_13,
  ISO_8859_14,
  ISO_8859_15,
  KOI8_R,
  KOI8_U,
  UTF_8,
  last_encoding_code
} encoding_code_type;

/* The current document encoding, or null if not set.  */
extern encoding_code_type document_encoding_code;

/* If an encoding is not supported, just keep it as a string.  */
extern char *unknown_encoding;

/* Maps an HTML abbreviation to ISO and Unicode codes for a given code.  */

typedef unsigned short int unicode_t; /* should be 16 bits */
typedef unsigned char byte_t;

typedef struct
{
  char *html;        /* HTML equivalent like umlaut auml => &auml; */
  byte_t bytecode;   /* 8-Bit Code (ISO 8859-1,...) */
  unicode_t unicode; /* Unicode in U+ convention */
  char *translit;    /* 7-bit transliteration */
} iso_map_type;

/* Information about the document encoding. */
typedef struct
{
  encoding_code_type ec; /* document encoding type (see above enum) */
  char *encname;         /* encoding name like "iso-8859-1", valid in
                            HTML and Emacs */
  iso_map_type *isotab;  /* address of ISO translation table */
} encoding_type;

/* Table with all the encoding codes that we recognize.  */
extern encoding_type encoding_table[];


/* The commands.  */
extern void cm_documentlanguage (void),
     cm_documentencoding (void);

/* Accents, other non-English characters.  */
void cm_accent (int arg), cm_special_char (int arg),
     cm_dotless (int arg, int start, int end);

extern void cm_accent_umlaut (int arg, int start, int end),
     cm_accent_acute (int arg, int start, int end),
     cm_accent_cedilla (int arg, int start, int end),
     cm_accent_hat (int arg, int start, int end),
     cm_accent_grave (int arg, int start, int end),
     cm_accent_tilde (int arg, int start, int end);

extern char *current_document_encoding (void);

extern const char *lang_transliterate_char (byte_t ch);

extern char *document_language;

#endif /* not LANG_H */
