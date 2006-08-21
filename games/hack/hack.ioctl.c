/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* hack.ioctl.c - version 1.0.2 */
/* $FreeBSD: src/games/hack/hack.ioctl.c,v 1.2 1999/09/12 07:01:23 marcel Exp $
   $DragonFly: src/games/hack/hack.ioctl.c,v 1.4 2006/08/21 19:45:32 pavalos Exp $

   This cannot be part of hack.tty.c (as it was earlier) since on some
   systems (e.g. MUNIX) the include files <termio.h> and <sgtty.h>
   define the same constants, and the C preprocessor complains. */
#include "hack.h"
#ifdef BSD
#include	<sgtty.h>
struct ltchars ltchars, ltchars0;
#else
#include	<termio.h>	/* also includes part of <sgtty.h> */
struct termio termio;
#endif /* BSD */

void
getioctls(void)
{
#ifdef BSD
	ioctl(fileno(stdin), (int) TIOCGLTC, (char *) &ltchars);
	ioctl(fileno(stdin), (int) TIOCSLTC, (char *) &ltchars0);
#else
	ioctl(fileno(stdin), (int) TCGETA, &termio);
#endif /* BSD */
}

void
setioctls(void)
{
#ifdef BSD
	ioctl(fileno(stdin), (int) TIOCSLTC, (char *) &ltchars);
#else
	ioctl(fileno(stdin), (int) TCSETA, &termio);
#endif /* BSD */
}

#ifdef SUSPEND		/* implies BSD */
#include	<signal.h>
int
dosuspend(void)
{
#ifdef SIGTSTP
	if(signal(SIGTSTP, SIG_IGN) == SIG_DFL) {
		settty((char *) 0);
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
	return(0);
}
#endif /* SUSPEND */
