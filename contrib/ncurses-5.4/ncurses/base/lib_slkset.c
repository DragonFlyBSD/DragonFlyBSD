/****************************************************************************
 * Copyright (c) 1998-2001,2003 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/

/*
 *	lib_slkset.c
 *      Set soft label text.
 */
#include <curses.priv.h>
#include <ctype.h>

MODULE_ID("$Id: lib_slkset.c,v 1.10 2003/04/12 21:32:16 tom Exp $")

NCURSES_EXPORT(int)
slk_set(int i, const char *astr, int format)
{
    SLK *slk = SP->_slk;
    size_t len;
    int offset;
    const char *str = astr;
    const char *p;

    T((T_CALLED("slk_set(%d, \"%s\", %d)"), i, str, format));

    if (slk == NULL || i < 1 || i > slk->labcnt || format < 0 || format > 2)
	returnCode(ERR);
    if (str == NULL)
	str = "";

    while (isspace(UChar(*str)))
	str++;			/* skip over leading spaces  */
    p = str;
    while (isprint(UChar(*p)))
	p++;			/* The first non-print stops */

    --i;			/* Adjust numbering of labels */

    len = (size_t) (p - str);
    if (len > (size_t) slk->maxlen)
	len = slk->maxlen;
    if (len == 0)
	slk->ent[i].ent_text[0] = 0;
    else
	strncpy(slk->ent[i].ent_text, str, len)[len] = 0;
    memset(slk->ent[i].form_text, ' ', (unsigned) slk->maxlen);
    slk->ent[i].ent_text[slk->maxlen] = 0;

    switch (format) {
    default:
    case 0:			/* left-justified */
	offset = 0;
	break;
    case 1:			/* centered */
	offset = (slk->maxlen - len) / 2;
	break;
    case 2:			/* right-justified */
	offset = slk->maxlen - len;
	break;
    }
    memcpy(slk->ent[i].form_text + offset,
	   slk->ent[i].ent_text,
	   len);
    slk->ent[i].form_text[slk->maxlen] = 0;
    slk->ent[i].dirty = TRUE;
    returnCode(OK);
}
