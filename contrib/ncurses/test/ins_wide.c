/*
 * $Id: ins_wide.c,v 1.3 2003/08/09 22:07:23 tom Exp $
 *
 * Demonstrate the wins_wstr() and wins_wch functions.
 * Thomas Dickey - 2002/11/23
 *
 * Note: to provide inputs for *ins_wch(), we use setcchar().  A quirk of the
 * X/Open definition for that function is that the string contains no
 * characters with negative width.  Any control character (such as tab) falls
 * into that category.  So it follows that *ins_wch() cannot render a tab
 * character because there is no legal way to construct a cchar_t containing
 * one.  X/Open does not document this, and it would be logical to assume that
 * *ins_wstr() has the same limitation, but it uses a wchar_t string directly,
 * and does not document how tabs are handled.
 */

#include <test.priv.h>

#define TABSIZE 8

#if USE_WIDEC_SUPPORT
static int margin = (2 * TABSIZE) - 1;

static void
legend(WINDOW *win, wchar_t * buffer, int length)
{
    wmove(win, 0, 0);
    wprintw(win,
	    "The Strings/Chars displays should match.  Enter any characters.\n");
    wprintw(win,
	    "Use down-arrow or ^N to repeat on the next line, 'q' to exit.\n");
    wclrtoeol(win);
    wprintw(win, "Inserted %d characters <", length);
    waddwstr(win, buffer);
    waddstr(win, ">");
}

static int
ColOf(wchar_t * buffer, int length)
{
    int n;
    int result;

    for (n = 0, result = margin + 1; n < length; ++n) {
	int ch = buffer[n];
	switch (ch) {
	case '\n':
	    /* actually newline should clear the remainder of the line
	     * and move to the next line - but that seems a little awkward
	     * in this example.
	     */
	case '\r':
	    result = 0;
	    break;
	case '\b':
	    if (result > 0)
		--result;
	    break;
	case '\t':
	    result += (TABSIZE - (result % TABSIZE));
	    break;
	case '\177':
	    result += 2;
	    break;
	default:
	    ++result;
	    if (ch < 32)
		++result;
	    break;
	}
    }
    return result;
}

int
main(int argc GCC_UNUSED, char *argv[]GCC_UNUSED)
{
    cchar_t tmp_cchar;
    wchar_t tmp_wchar[2];
    wint_t ch;
    int code;
    int limit;
    int row = 1;
    int col;
    int length;
    wchar_t buffer[BUFSIZ];
    WINDOW *work;
    WINDOW *show;

    putenv("TABSIZE=8");
    initscr();
    (void) cbreak();		/* take input chars one at a time, no wait for \n */
    (void) noecho();		/* don't echo input */
    keypad(stdscr, TRUE);

    limit = LINES - 5;
    work = newwin(limit, COLS, 0, 0);
    show = newwin(4, COLS, limit + 1, 0);
    keypad(work, TRUE);

    for (col = margin + 1; col < COLS; col += TABSIZE)
	mvwvline(work, row, col, '.', limit - 2);

    box(work, 0, 0);
    mvwvline(work, row, margin, ACS_VLINE, limit - 2);
    mvwvline(work, row, margin + 1, ACS_VLINE, limit - 2);
    limit /= 2;

    mvwaddstr(work, 1, 2, "String");
    mvwaddstr(work, limit + 1, 2, "Chars");
    wnoutrefresh(work);

    buffer[length = 0] = '\0';
    legend(show, buffer, length);
    wnoutrefresh(show);

    doupdate();

    /*
     * Show the characters inserted in color, to distinguish from those that
     * are shifted.
     */
    if (has_colors()) {
	start_color();
	init_pair(1, COLOR_WHITE, COLOR_BLUE);
	wbkgdset(work, COLOR_PAIR(1) | ' ');
    }

    while ((code = wget_wch(work, &ch)) != ERR) {

	switch (code) {
	case KEY_CODE_YES:
	    switch (ch) {
	    case KEY_DOWN:
		ch = CTRL('N');
		break;
	    case KEY_BACKSPACE:
		ch = '\b';
		break;
	    default:
		beep();
		continue;
	    }
	    break;
	}
	if (ch == 'q')
	    break;

	wmove(work, row, margin + 1);
	if (ch == CTRL('N')) {
	    if (row < limit) {
		++row;
		/* put the whole string in, all at once */
		mvwins_wstr(work, row, margin + 1, buffer);

		/* do the corresponding single-character insertion */
		for (col = 0; col < length; ++col) {
		    memset(&tmp_cchar, 0, sizeof(tmp_cchar));
		    if (setcchar(&tmp_cchar,
				 &(buffer[col]),
				 A_NORMAL,
				 0,
				 (void *) 0) != ERR) {
			mvwins_wch(work, limit + row, ColOf(buffer, col), &tmp_cchar);
		    } else {
			beep();	/* even for tabs! */
			mvwinsch(work,
				 limit + row,
				 ColOf(buffer, col), buffer[col]);
		    }
		}
	    } else {
		beep();
	    }
	} else {
	    buffer[length++] = ch;
	    buffer[length] = '\0';
	    /* put the string in, one character at a time */
	    mvwins_wstr(work,
			row,
			ColOf(buffer, length - 1), buffer + length - 1);

	    /* do the corresponding single-character insertion */
	    tmp_wchar[0] = ch;
	    tmp_wchar[1] = 0;
	    if (setcchar(&tmp_cchar,
			 tmp_wchar,
			 A_NORMAL,
			 0,
			 (void *) 0) != ERR) {
		mvwins_wch(work,
			   limit + row,
			   ColOf(buffer, length - 1), &tmp_cchar);
	    } else {
		beep();		/* even for tabs! */
		mvwinsch(work,
			 limit + row,
			 ColOf(buffer, length - 1), ch);
	    }
	    wnoutrefresh(work);

	    legend(show, buffer, length);
	    wnoutrefresh(show);

	    doupdate();
	}
    }
    endwin();
    ExitProgram(EXIT_SUCCESS);
}
#else
int
main(void)
{
    printf("This program requires the wide-ncurses library\n");
    ExitProgram(EXIT_FAILURE);
}
#endif
