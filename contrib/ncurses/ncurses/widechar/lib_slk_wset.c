/****************************************************************************
 * Copyright (c) 2003,2004 Free Software Foundation, Inc.                   *
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
 *  Author: Thomas E. Dickey, 2003                                          *
 ****************************************************************************/

/*
 *	lib_slk_wset.c
 *      Set soft label text.
 */
#include <curses.priv.h>

#ifdef HAVE_WCTYPE_H
#include <wctype.h>
#endif

MODULE_ID("$Id: lib_slk_wset.c,v 1.6 2004/01/03 21:14:03 tom Exp $")

NCURSES_EXPORT(int)
slk_wset(int i, const wchar_t * astr, int format)
{
    static wchar_t empty[] =
    {L'\0'};
    int result = ERR;
    SLK *slk = SP->_slk;
    int offset;
    size_t arglen;
    const wchar_t *p;

    T((T_CALLED("slk_wset(%d, %s, %d)"), i, _nc_viswbuf(astr), format));

    if (astr == 0)
	astr = empty;
    arglen = wcslen(astr);
    while (iswspace(*astr)) {
	--arglen;
	++astr;			/* skip over leading spaces  */
    }
    p = astr;
    while (iswprint(*p))
	p++;			/* The first non-print stops */

    arglen = (size_t) (p - astr);

    if (slk != NULL &&
	i >= 1 &&
	i <= slk->labcnt &&
	format >= 0 &&
	format <= 2) {
	char *new_text;
	size_t n;
	size_t used = 0;
	int mycols;
	mbstate_t state;

	--i;			/* Adjust numbering of labels */

	/*
	 * Reduce the actual number of columns to fit in the label field.
	 */
	while (arglen != 0 && wcswidth(astr, arglen) > slk->maxlen) {
	    --arglen;
	}
	mycols = wcswidth(astr, arglen);

	/*
	 * translate the wide-character string to multibyte form.
	 */
	memset(&state, 0, sizeof(state));

	if ((new_text = (char *) _nc_doalloc(0, MB_LEN_MAX * arglen)) == 0)
	    returnCode(ERR);

	for (n = 0; n < arglen; ++n) {
	    used += wcrtomb(new_text + used, astr[n], &state);
	}
	new_text[used] = '\0';

	if (used >= (size_t) (slk->maxlen + 1)) {
	    if ((slk->ent[i].ent_text = (char *) _nc_doalloc(slk->ent[i].ent_text,
							     used + 1)) == 0)
		returnCode(ERR);
	    if ((slk->ent[i].form_text = (char *) _nc_doalloc(slk->ent[i].form_text,
							      used + 1)) == 0)
		returnCode(ERR);
	}

	(void) strcpy(slk->ent[i].ent_text, new_text);
	free(new_text);

	sprintf(slk->ent[i].form_text, "%*s", (size_t) slk->maxlen, " ");

	switch (format) {
	default:
	case 0:		/* left-aligned */
	    offset = 0;
	    break;
	case 1:		/* centered */
	    offset = (slk->maxlen - mycols) / 2;
	    break;
	case 2:		/* right-aligned */
	    offset = slk->maxlen - mycols;
	    break;
	}
	if (offset < 0)
	    offset = 0;
	strcpy(slk->ent[i].form_text + offset,
	       slk->ent[i].ent_text);
	/*
	 * Pad the display with blanks on the right, unless it is already
	 * right-aligned.
	 */
	if (format != 2 && mycols < slk->maxlen) {
	    sprintf(slk->ent[i].form_text + offset + used,
		    "%*s",
		    slk->maxlen - (mycols - offset),
		    " ");
	}
	slk->ent[i].dirty = TRUE;
	result = OK;
    }
    returnCode(result);
}
