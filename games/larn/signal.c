/*
 * $DragonFly: src/games/larn/signal.c,v 1.4 2006/08/26 17:05:05 pavalos Exp $
 */
/* "Larn is copyrighted 1986 by Noah Morgan.\n" */

#include <signal.h>
#include "header.h"

static void s2choose(void);
static void cntlc(void);
static void sgam(void);
#ifdef SIGTSTP
static void tstop(void);
#endif
static void sigill(void);
static void sigtrap(void);
static void sigiot(void);
static void sigemt(void);
static void sigfpe(void);
static void sigbus(void);
static void sigsegv(void);
static void sigsys(void);
static void sigpipe(void);
static void sigterm(void);
static void sigpanic(int);

#define BIT(a) (1<<((a)-1))

static void
s2choose(void)	/* text to be displayed if ^C during intro screen */
{
	cursor(1, 24);
	lprcat("Press ");
	setbold();
	lprcat("return");
	resetbold();
	lprcat(" to continue: ");
	lflush();
}

static void
cntlc(void)	/* what to do for a ^C */
{
	if (nosignal)	/* don't do anything if inhibited */
		return;
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	quit();
	if (predostuff == 1)
		s2choose();
	else
		showplayer();
	lflush();
	signal(SIGQUIT, (sig_t)cntlc);
	signal(SIGINT, (sig_t)cntlc);
}

/*
 *	subroutine to save the game if a hangup signal
 */
static void
sgam(void)
{
	savegame(savefilename);
	wizard = 1;
	died(-257);		/* hangup signal */
}

#ifdef SIGTSTP
static void
tstop(void) /* control Y */
{
	if (nosignal)		/* nothing if inhibited */
		return;
	lcreat(NULL);
	clearvt100();
	lflush();
	signal(SIGTSTP, SIG_DFL);
#ifdef SIGVTALRM
	/* looks like BSD4.2 or higher - must clr mask for signal to take effect*/
	sigsetmask(sigblock(0) & ~BIT(SIGTSTP));
#endif
	kill(getpid(), SIGTSTP);

	setupvt100();
	signal(SIGTSTP, (sig_t)tstop);
	if (predostuff == 1)
		s2choose();
	else
		drawscreen();
	showplayer();
	lflush();
}
#endif /* SIGTSTP */

/*
 *	subroutine to issue the needed signal traps  called from main()
 */
static void
sigill(void)
{
	sigpanic(SIGILL);
}

static void
sigtrap(void)
{
	sigpanic(SIGTRAP);
}

static void
sigiot(void)
{
	sigpanic(SIGIOT);
}

static void
sigemt(void)
{
	sigpanic(SIGEMT);
}

static void
sigfpe(void)
{
	sigpanic(SIGFPE);
}

static void
sigbus(void)
{
	sigpanic(SIGBUS);
}

static void
sigsegv(void)
{
	sigpanic(SIGSEGV);
}

static void
sigsys(void)
{
	sigpanic(SIGSYS);
}

static void
sigpipe(void)
{
	sigpanic(SIGPIPE);
}

static void
sigterm(void)
{
	sigpanic(SIGTERM);
}

void
sigsetup(void)
{
	signal(SIGQUIT, (sig_t)cntlc);
	signal(SIGINT, (sig_t)cntlc);
	signal(SIGKILL, SIG_IGN);
	signal(SIGHUP, (sig_t)sgam);
	signal(SIGILL, (sig_t)sigill);
	signal(SIGTRAP, (sig_t)sigtrap);
	signal(SIGIOT, (sig_t)sigiot);
	signal(SIGEMT, (sig_t)sigemt);
	signal(SIGFPE, (sig_t)sigfpe);
	signal(SIGBUS, (sig_t)sigbus);
	signal(SIGSEGV, (sig_t)sigsegv);
	signal(SIGSYS, (sig_t)sigsys);
	signal(SIGPIPE, (sig_t)sigpipe);
	signal(SIGTERM, (sig_t)sigterm);
#ifdef SIGTSTP
	signal(SIGTSTP, (sig_t)tstop);
	signal(SIGSTOP, (sig_t)tstop);
#endif /* SIGTSTP */
}

static const char *signame[NSIG] = { "",
"SIGHUP",  /*   1	hangup */
"SIGINT",  /*   2	interrupt */
"SIGQUIT", /*   3	quit */
"SIGILL",  /*   4	illegal instruction (not reset when caught) */
"SIGTRAP", /*   5	trace trap (not reset when caught) */
"SIGIOT",  /*   6	IOT instruction */
"SIGEMT",  /*   7	EMT instruction */
"SIGFPE",  /*   8	floating point exception */
"SIGKILL", /*   9	kill (cannot be caught or ignored) */
"SIGBUS",  /*  10	bus error */
"SIGSEGV", /*  11	segmentation violation */
"SIGSYS",  /*  12	bad argument to system call */
"SIGPIPE", /*  13	write on a pipe with no one to read it */
"SIGALRM", /*  14	alarm clock */
"SIGTERM", /*  15	software termination signal from kill */
"SIGURG",  /*  16	urgent condition on IO channel */
"SIGSTOP", /*  17	sendable stop signal not from tty */
"SIGTSTP", /*  18	stop signal from tty */
"SIGCONT", /*  19	continue a stopped process */
"SIGCHLD", /*  20	to parent on child stop or exit */
"SIGTTIN", /*  21	to readers pgrp upon background tty read */
"SIGTTOU", /*  22	like TTIN for output if (tp->t_local&LTOSTOP) */
"SIGIO",   /*  23	input/output possible signal */
"SIGXCPU", /*  24	exceeded CPU time limit */
"SIGXFSZ", /*  25	exceeded file size limit */
"SIGVTALRM",/* 26	virtual time alarm */
"SIGPROF", /*  27	profiling time alarm */
"SIGWINCH",/*  28	window size changes */
"SIGINFO", /*  29	information request */
"SIGUSR1", /*  30	user defined signal 1 */
"SIGUSR2", /*  31	user defined signal 2 */
};

/*
 *	routine to process a fatal error signal
 */
static void
sigpanic(int sig)
{
	char buf[128];
	signal(sig, SIG_DFL);
	sprintf(buf, "\nLarn - Panic! Signal %d received [%s]", sig, signame[sig]);
	write(2, buf, strlen(buf));
	sleep(2);
	sncbr();
	savegame(savefilename);
	kill(getpid(), sig);	/* this will terminate us */
}
