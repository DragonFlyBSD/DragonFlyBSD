/****************************************************************************
 * Copyright (c) 1998-2001,2002 Free Software Foundation, Inc.              *
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
 *     and: Sven Verdoolaege                                                *
 *     and: Thomas E. Dickey                                                *
 ****************************************************************************/

/*
**	lib_insch.c
**
**	The routine winsch().
**
*/

#include <curses.priv.h>
#include <ctype.h>

MODULE_ID("$Id: lib_insch.c,v 1.18 2002/11/23 21:41:05 tom Exp $")

/*
 * Insert the given character, updating the current location to simplify
 * inserting a string.
 */
void
_nc_insert_ch(WINDOW *win, chtype ch)
{
    NCURSES_CH_T wch;
    int count;

    switch (ch) {
    case '\t':
	for (count = (TABSIZE - (win->_curx % TABSIZE)); count > 0; count--)
	    _nc_insert_ch(win, ' ');
	break;
    case '\n':
    case '\r':
    case '\b':
	SetChar2(wch, ch);
	_nc_waddch_nosync(win, wch);
	break;
    default:
	if (is7bits(ch) && iscntrl(ch)) {
	    _nc_insert_ch(win, '^');
	    _nc_insert_ch(win, '@' + (ch));
	} else if (win->_curx <= win->_maxx) {
	    struct ldat *line = &(win->_line[win->_cury]);
	    NCURSES_CH_T *end = &(line->text[win->_curx]);
	    NCURSES_CH_T *temp1 = &(line->text[win->_maxx]);
	    NCURSES_CH_T *temp2 = temp1 - 1;

	    SetChar2(wch, ch);

	    CHANGED_TO_EOL(line, win->_curx, win->_maxx);
	    while (temp1 > end)
		*temp1-- = *temp2--;

	    *temp1 = _nc_render(win, wch);

	    win->_curx++;
	}
	break;
    }
}

NCURSES_EXPORT(int)
winsch(WINDOW *win, chtype c)
{
    NCURSES_SIZE_T oy;
    NCURSES_SIZE_T ox;
    int code = ERR;

    T((T_CALLED("winsch(%p, %s)"), win, _tracechtype(c)));

    if (win != 0) {
	oy = win->_cury;
	ox = win->_curx;

	_nc_insert_ch(win, c);

	win->_curx = ox;
	win->_cury = oy;
	_nc_synchook(win);
	code = OK;
    }
    returnCode(code);
}

NCURSES_EXPORT(int)
winsnstr(WINDOW *win, const char *s, int n)
{
    int code = ERR;
    NCURSES_SIZE_T oy;
    NCURSES_SIZE_T ox;
    const unsigned char *str = (const unsigned char *) s;
    const unsigned char *cp;

    T((T_CALLED("winsnstr(%p,%s,%d)"), win, _nc_visbufn(s, n), n));

    if (win != 0 && str != 0) {
	oy = win->_cury;
	ox = win->_curx;
	for (cp = str; *cp && (n <= 0 || (cp - str) < n); cp++) {
	    _nc_insert_ch(win, (chtype) UChar(*cp));
	}
	win->_curx = ox;
	win->_cury = oy;
	_nc_synchook(win);
	code = OK;
    }
    returnCode(code);
}
