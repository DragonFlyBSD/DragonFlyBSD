/*
 * $Id: color_set.c,v 1.2 2003/12/07 00:08:47 tom Exp $
 */

#include <test.priv.h>

#define SHOW(n) ((n) == ERR ? "ERR" : "OK")

int
main(int argc GCC_UNUSED, char *argv[]GCC_UNUSED)
{
    short f, b;
    int i;

    initscr();
    cbreak();
    noecho();

    if (has_colors()) {
	start_color();

	pair_content(0, &f, &b);
	printw("pair 0 contains (%d,%d)\n", f, b);
	getch();

	printw("Initializing pair 1 to red/black\n");
	init_pair(1, COLOR_RED, COLOR_BLACK);
	i = color_set(1, NULL);
	printw("RED/BLACK (%s)\n", SHOW(i));
	getch();

	printw("Initializing pair 2 to white/blue\n");
	init_pair(2, COLOR_WHITE, COLOR_BLUE);
	i = color_set(2, NULL);
	printw("WHITE/BLUE (%s)\n", SHOW(i));
	getch();

	printw("Resetting colors to pair 0\n");
	i = color_set(0, NULL);
	printw("Default Colors (%s)\n", SHOW(i));
	getch();

	printw("Resetting colors to pair 1\n");
	i = color_set(1, NULL);
	printw("RED/BLACK (%s)\n", SHOW(i));
	getch();

    } else {
	printw("This demo requires a color terminal");
	getch();
    }
    endwin();

    ExitProgram(EXIT_SUCCESS);
}
