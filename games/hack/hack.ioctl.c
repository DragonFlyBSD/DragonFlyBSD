/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.ioctl.c - version 1.0.2 */
/* $FreeBSD: src/games/hack/hack.ioctl.c,v 1.2 1999/09/12 07:01:23 marcel Exp $
 * $DragonFly: src/games/hack/hack.ioctl.c,v 1.4 2006/08/21 19:45:32 pavalos Exp $
 *
 * This cannot be part of hack.tty.c (as it was earlier) since on some
 * systems (e.g. MUNIX) the include files <termio.h> and <sgtty.h> define the
 * same constants, and the C preprocessor complains.
 */
#include "hack.h"
#include <termios.h>
struct termios termio;

void
getioctls(void)
{
	tcgetattr(fileno(stdin), &termio);
}

void
setioctls(void)
{
	tcsetattr(fileno(stdin), TCSANOW, &termio);
}

#ifdef SUSPEND
#include <signal.h>
int
dosuspend(void)
{
#ifdef SIGTSTP
	if (signal(SIGTSTP, SIG_IGN) == SIG_DFL) {
		settty(NULL);
		signal(SIGTSTP, SIG_DFL);
		kill(0, SIGTSTP);
		gettty();
		setftty();
		docrt();
	} else {
		pline("I don't think your shell has job control.");
	}
#else /* SIGTSTP */
	pline("Sorry, it seems we have no SIGTSTP here. Try ! or S.");
#endif /* SIGTSTP */
	return (0);
}
#endif /* SUSPEND */
