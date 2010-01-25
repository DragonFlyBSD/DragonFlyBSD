// -*- C++ -*-
/* Copyright (C) 1989, 1990, 1991, 1992, 2000, 2001, 2002, 2003, 2004, 2006,
                 2008, 2009
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

#include "lib.h"

#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include "errarg.h"
#include "error.h"
#include "font.h"
#include "ptable.h"
#include "itable.h"

// Every glyphinfo is actually a charinfo.
class charinfo : glyph {
public:
  const char *name;	// The glyph name, or NULL.
  friend class character_indexer;
};

// PTABLE(charinfo) is a hash table mapping `const char *' to `charinfo *'.
declare_ptable(charinfo)
implement_ptable(charinfo)

// ITABLE(charinfo) is a hash table mapping `int >= 0' to `charinfo *'.
declare_itable(charinfo)
implement_itable(charinfo)

// This class is as a registry storing all named and numbered glyphs known
// so far, and assigns a unique index to each glyph.
class character_indexer {
public:
  character_indexer();
  ~character_indexer();
  // --------------------- Lookup or creation of a glyph.
  glyph *ascii_char_glyph(unsigned char);
  glyph *named_char_glyph(const char *);
  glyph *numbered_char_glyph(int);
private:
  int next_index;		// Number of glyphs already allocated.
  PTABLE(charinfo) table;	// Table mapping name to glyph.
  glyph *ascii_glyph[256];	// Shorthand table for looking up "charNNN"
				// glyphs.
  ITABLE(charinfo) ntable;	// Table mapping number to glyph.
  enum { NSMALL = 256 };
  glyph *small_number_glyph[NSMALL]; // Shorthand table for looking up
				// numbered glyphs with small numbers.
};

character_indexer::character_indexer()
: next_index(0)
{
  int i;
  for (i = 0; i < 256; i++)
    ascii_glyph[i] = UNDEFINED_GLYPH;
  for (i = 0; i < NSMALL; i++)
    small_number_glyph[i] = UNDEFINED_GLYPH;
}

character_indexer::~character_indexer()
{
}

glyph *character_indexer::ascii_char_glyph(unsigned char c)
{
  if (ascii_glyph[c] == UNDEFINED_GLYPH) {
    char buf[4+3+1];
    memcpy(buf, "char", 4);
    strcpy(buf + 4, i_to_a(c));
    charinfo *ci = new charinfo;
    ci->index = next_index++;
    ci->number = -1;
    ci->name = strsave(buf);
    ascii_glyph[c] = ci;
  }
  return ascii_glyph[c];
}

inline glyph *character_indexer::named_char_glyph(const char *s)
{
  // Glyphs with name `charNNN' are only stored in ascii_glyph[], not
  // in the table.  Therefore treat them specially here.
  if (s[0] == 'c' && s[1] == 'h' && s[2] == 'a' && s[3] == 'r') {
    char *val;
    long n = strtol(s + 4, &val, 10);
    if (val != s + 4 && *val == '\0' && n >= 0 && n < 256)
      return ascii_char_glyph((unsigned char)n);
  }
  charinfo *ci = table.lookupassoc(&s);
  if (ci == NULL) {
    ci = new charinfo[1];
    ci->index = next_index++;
    ci->number = -1;
    ci->name = table.define(s, ci);
  }
  return ci;
}

inline glyph *character_indexer::numbered_char_glyph(int n)
{
  if (n >= 0 && n < NSMALL) {
    if (small_number_glyph[n] == UNDEFINED_GLYPH) {
      charinfo *ci = new charinfo;
      ci->index = next_index++;
      ci->number = n;
      ci->name = NULL;
      small_number_glyph[n] = ci;
    }
    return small_number_glyph[n];
  }
  charinfo *ci = ntable.lookup(n);
  if (ci == NULL) {
    ci = new charinfo[1];
    ci->index = next_index++;
    ci->number = n;
    ci->name = NULL;
    ntable.define(n, ci);
  }
  return ci;
}

static character_indexer indexer;

glyph *number_to_glyph(int n)
{
  return indexer.numbered_char_glyph(n);
}

// troff overrides this function with its own version.

glyph *name_to_glyph(const char *s)
{
  assert(s != 0 && s[0] != '\0' && s[0] != ' ');
  if (s[1] == '\0')
    // \200 and char128 are synonyms
    return indexer.ascii_char_glyph(s[0]);
  return indexer.named_char_glyph(s);
}

const char *glyph_to_name(glyph *g)
{
  charinfo *ci = (charinfo *)g; // Every glyph is actually a charinfo.
  return ci->name;
}
