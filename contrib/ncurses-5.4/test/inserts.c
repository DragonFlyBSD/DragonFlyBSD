/*
 * $Id: inserts.c,v 1.5 2003/08/09 22:07:06 tom Exp $
 *
 * Demonstrate the winsstr() and winsch functions.
 * Thomas Dickey - 2002/10/19
 */

#include <test.priv.h>

#define TABSIZE 8

static int margin = (2 * TABSIZE) - 1;

static void
legend(WINDOW *win, char *buffer, int length)
{
    wmove(win, 0, 0);
    wprintw(win,
	    "The Strings/Chars displays should match.  Enter any characters.\n");
    wprintw(win,
	    "Use down-arrow or ^N to repeat on the next line, 'q' to exit.\n");
    wclrtoeol(win);
    wprintw(win, "Inserted %d characters <%s>", length, buffer);
}

static int
ColOf(char *buffer, int length)
{
    int n;
    int result;

    for (n = 0, result = margin + 1; n < length; ++n) {
	int ch = UChar(buffer[n]);
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
    int ch;
    int limit;
    int row = 1;
    int col;
    int length;
    char buffer[BUFSIZ];
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

    while ((ch = wgetch(work)) != 'q') {
	wmove(work, row, margin + 1);
	switch (ch) {
	case CTRL('N'):
	case KEY_DOWN:
	    if (row < limit) {
		++row;
		/* put the whole string in, all at once */
		mvwinsstr(work, row, margin + 1, buffer);

		/* do the corresponding single-character insertion */
		for (col = 0; col < length; ++col) {
		    mvwinsch(work, limit + row, ColOf(buffer, col), buffer[col]);
		}
	    } else {
		beep();
	    }
	    break;
	case KEY_BACKSPACE:
	    ch = '\b';
	    /* FALLTHRU */
	default:
	    if (ch <= 0 || ch > 255) {
		beep();
		break;
	    }
	    buffer[length++] = ch;
	    buffer[length] = '\0';
	    /* put the string in, one character at a time */
	    mvwinsstr(work,
		      row,
		      ColOf(buffer, length - 1), buffer + length - 1);

	    /* do the corresponding single-character insertion */
	    mvwinsch(work,
		     limit + row,
		     ColOf(buffer, length - 1), ch);
	    wnoutrefresh(work);

	    legend(show, buffer, length);
	    wnoutrefresh(show);

	    doupdate();
	    break;
	}
    }
    endwin();
    ExitProgram(EXIT_SUCCESS);
}
