/*
 * Copyright (c) 2004 Joerg Sonnenberger <joerg@bec.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/lib/libiberty/hex.c,v 1.1 2004/10/23 12:15:21 joerg Exp $
 */

#include <libiberty.h>

static const char hex_table[128] = {
   0,    0,    0,    0,    0,    0,    0,    0, 
   0,    0,    0,    0,    0,    0,    0,    0, 
   0,    0,    0,    0,    0,    0,    0,    0, 
   0,    0,    0,    0,    0,    0,    0,    0, 
   0,    0,    0,    0,    0,    0,    0,    0, 
   0,    0,    0,    0,    0,    0,    0,    0, 
   1,    2,    3,    4,    5,    6,    7,    8, 
   9,   10,    0,    0,    0,    0,    0,    0, 
   0,   11,   12,   13,   14,   15,   16,    0, 
   0,    0,    0,    0,    0,    0,    0,    0, 
   0,    0,    0,    0,    0,    0,    0,    0, 
   0,    0,    0,    0,    0,    0,    0,    0, 
   0,   11,   12,   13,   14,   15,   16,    0, 
   0,    0,    0,    0,    0,    0,    0,    0, 
   0,    0,    0,    0,    0,    0,    0,    0, 
   0,    0,    0,    0,    0,    0,    0,    0
};
#define HEX_TABLE_INITIALZED

#ifndef HEX_TABLE_INITIALZED
static char	hex_table[128];
#endif

void
hex_init(void)
{
#ifndef HEX_TABLE_INITIALZED
	hex_table['0'] = 1;
	hex_table['1'] = 2;
	hex_table['2'] = 3;
	hex_table['3'] = 4;
	hex_table['4'] = 5;
	hex_table['5'] = 6;
	hex_table['6'] = 7;
	hex_table['7'] = 8;
	hex_table['8'] = 9;
	hex_table['9'] = 10;
	hex_table['a'] = 11;
	hex_table['b'] = 12;
	hex_table['c'] = 13;
	hex_table['d'] = 14;
	hex_table['e'] = 15;
	hex_table['f'] = 16;
	hex_table['a'] = 11;
	hex_table['b'] = 12;
	hex_table['c'] = 13;
	hex_table['d'] = 14;
	hex_table['e'] = 15;
	hex_table['f'] = 16;
	hex_table['A'] = 11;
	hex_table['B'] = 12;
	hex_table['C'] = 13;
	hex_table['D'] = 14;
	hex_table['E'] = 15;
	hex_table['F'] = 16;
#endif
}

int
hex_p(int c)
{
	if ((c & ~127) != 0)
		return(0);
	return(hex_table[c]);
}

unsigned int
hex_value(int c)
{
	if ((c & ~127) != 0)
		return(0);
	return(hex_table[c] - 1);
}
