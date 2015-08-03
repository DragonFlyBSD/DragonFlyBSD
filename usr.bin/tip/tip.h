/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)tip.h	8.1 (Berkeley) 6/6/93
 *
 * $FreeBSD: src/usr.bin/tip/tip/tip.h,v 1.5.2.1 2000/07/01 12:24:23 ps Exp $
 */

/*
 * tip - terminal interface program
 */

#include <sys/types.h>
#include <machine/endian.h>
#include <sys/file.h>
#include <sys/time.h>
#include <limits.h>

#if HAVE_TERMIOS
#include <sys/ioctl.h>         /* for TIOCHPCL */
#include <sys/filio.h>    /* for FIONREAD */
#include <sys/termios.h>
#else
#include <sgtty.h>
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <ctype.h>
#include <setjmp.h>
#include <unistd.h>
#include <errno.h>

/*
 * Remote host attributes
 */
char	*DV;			/* UNIX device(s) to open */
char	*EL;			/* chars marking an EOL */
char	*CM;			/* initial connection message */
char	*IE;			/* EOT to expect on input */
char	*OE;			/* EOT to send to complete FT */
char	*CU;			/* call unit if making a phone call */
char	*AT;			/* acu type */
char	*PN;			/* phone number(s) */
char	*DI;			/* disconnect string */
char	*PA;			/* parity to be generated */

char	*PH;			/* phone number file */
char	*RM;			/* remote file name */
char	*HO;			/* host name */

char	*LI;			/* login script */
char	*LO;			/* logout script */

long	BR;			/* line speed for conversation */
long	FS;			/* frame size for transfers */

char	DU;			/* this host is dialed up */
char	HW;			/* this device is hardwired, see hunt.c */
char	*ES;			/* escape character */
char	*EX;			/* exceptions */
char	*FO;			/* force (literal next) char*/
char	*RC;			/* raise character */
char	*RE;			/* script record file */
char	*PR;			/* remote prompt */
long	DL;			/* line delay for file transfers to remote */
long	CL;			/* char delay for file transfers to remote */
long	ET;			/* echocheck timeout */
char	HD;			/* this host is half duplex - do local echo */

/*
 * String value table
 */
typedef
	struct {
		char	*v_name;	/* whose name is it */
		char	v_type;		/* for interpreting set's */
		char	v_access;	/* protection of touchy ones */
		char	*v_abrev;	/* possible abreviation */
		char	*v_value;	/* casted to a union later */
	}
	value_t;

#define STRING	01		/* string valued */
#define BOOL	02		/* true-false value */
#define NUMBER	04		/* numeric value */
#define CHAR	010		/* character value */

#define WRITE	01		/* write access to variable */
#define	READ	02		/* read access */

#define CHANGED	01		/* low bit is used to show modification */
#define PUBLIC	1		/* public access rights */
#define PRIVATE	03		/* private to definer */
#define ROOT	05		/* root defined */

#define	TRUE	1
#define FALSE	0

#define ENVIRON	020		/* initialize out of the environment */
#define IREMOTE	040		/* initialize out of remote structure */
#define INIT	0100		/* static data space used for initialization */
#define TMASK	017

/*
 * Definition of ACU line description
 */
typedef
	struct {
		char	*acu_name;
		int	(*acu_dialer)(char *, char *);
		void	(*acu_disconnect)(void);
		void	(*acu_abort)(void);
	}
	acu_t;

#define	equal(a, b)	(strcmp(a,b)==0)/* A nice function to string compare */

/*
 * variable manipulation stuff --
 *   if we defined the value entry in value_t, then we couldn't
 *   initialize it in vars.c, so we cast it as needed to keep lint
 *   happy.
 */
typedef
	union {
		int	zz_number;
		short	zz_boolean[2];
		char	zz_character[4];
		int	*zz_address;
	}
	zzhack;

#define value(v)	vtable[v].v_value

#define number(v)	((((zzhack *)(&(v))))->zz_number)

#if BYTE_ORDER == LITTLE_ENDIAN
#define boolean(v)	((((zzhack *)(&(v))))->zz_boolean[0])
#define character(v)	((((zzhack *)(&(v))))->zz_character[0])
#endif

#if BYTE_ORDER == BIG_ENDIAN
#define boolean(v)	((((zzhack *)(&(v))))->zz_boolean[1])
#define character(v)	((((zzhack *)(&(v))))->zz_character[3])
#endif

#define address(v)	((((zzhack *)(&(v))))->zz_address)

/*
 * Escape command table definitions --
 *   lookup in this table is performed when ``escapec'' is recognized
 *   at the begining of a line (as defined by the eolmarks variable).
*/

typedef
	struct {
		char	e_char;		/* char to match on */
		char	e_flags;	/* experimental, priviledged */
		char	*e_help;	/* help string */
		void 	(*e_func)(int);	/* command */
	}
	esctable_t;

#define NORM	00		/* normal protection, execute anyone */
#define EXP	01		/* experimental, mark it with a `*' on help */
#define PRIV	02		/* priviledged, root execute only */

extern int	vflag;		/* verbose during reading of .tiprc file */
extern value_t	vtable[];	/* variable table */

#if !ACULOG
#define logent(a, b, c, d)
#define loginit(x)
#else
void logent(char *, char *, char *, char*);
void  loginit(void);
#endif

/*
 * Definition of indices into variable table so
 *  value(DEFINE) turns into a static address.
 */

/*
'a,.!awk '{ printf("\%s \%s \%d\n", $1, $2, NR - 1); }'
*/

#define BEAUTIFY 0
#define BAUDRATE 1
#define DIALTIMEOUT 2
#define EOFREAD 3
#define EOFWRITE 4
#define EOL 5
#define ESCAPE 6
#define EXCEPTIONS 7
#define FORCE 8
#define FRAMESIZE 9
#define HOST 10
#define LOG 11
#define LOGIN 12
#define LOGOUT 13
#define PHONES 14
#define PROMPT 15
#define RAISE 16
#define RAISECHAR 17
#define RECORD 18
#define REMOTE 19
#define SCRIPT 20
#define TABEXPAND 21
#define VERBOSE 22
#define SHELL 23
#define HOME 24
#define ECHOCHECK 25
#define DISCONNECT 26
#define TAND 27
#define LDELAY 28
#define CDELAY 29
#define ETIMEOUT 30
#define RAWFTP 31
#define HALFDUPLEX 32
#define LECHO 33
#define PARITY 34

#if HAVE_TERMIOS
struct termios otermios;
struct termios ctermios;
#else /* HAVE_TERMIOS */
struct sgttyb	arg;		/* current mode of local terminal */
struct sgttyb	defarg;		/* initial mode of local terminal */
struct tchars	tchars;		/* current state of terminal */
struct tchars	defchars;	/* initial state of terminal */
struct ltchars	ltchars;	/* current local characters of terminal */
struct ltchars	deflchars;	/* initial local characters of terminal */
#endif /* HAVE_TERMIOS */

FILE	*fscript;		/* FILE for scripting */

int	fildes[2];		/* file transfer synchronization channel */
int	repdes[2];		/* read process sychronization channel */
int	FD;			/* open file descriptor to remote host */
int	AC;			/* open file descriptor to dialer (v831 only) */
int	vflag;			/* print .tiprc initialization sequence */
int	sfd;			/* for ~< operation */
int	pid;			/* pid of tipout */
uid_t	uid, euid;		/* real and effective user id's */
gid_t	gid, egid;		/* real and effective group id's */
int	stop;			/* stop transfer session flag */
int	quit;			/* same; but on other end */
int	intflag;		/* recognized interrupt */
int	stoprompt;		/* for interrupting a prompt session */
int	timedout;		/* ~> transfer timedout */
int	cumode;			/* simulating the "cu" program */

char	fname[PATH_MAX];	/* file name buffer for ~< */
char	copyname[PATH_MAX];	/* file name buffer for ~> */
char	ccc;			/* synchronization character */
char	ch;			/* for tipout */
char	*uucplock;		/* name of lock file for uucp's */

int	odisc;				/* initial tty line discipline */
extern	int disc;			/* current tty discpline */

char	*ctrl(char);
char	*vinterp(char *, char);
char	*connect(void);
int	size(char *);
int	any(char, char *);
void	setscript(void);
void	tipout(void);
void	vinit(void);
long	hunt(char *);
int	vstring(char *, char *);
void	setparity(char *);
void	vlex(char *);
void	daemon_uid(void);
void	disconnect(char *);
void	shell_uid(void);
void	unraw(void);
void	user_uid(void);
void	xpwrite(int, char *, int);
int	prompt(char *, char *, size_t);
void	tipabort(char *);
void	cumain(int, char **);
char	*getremote(char *);
char	*interp(char *);

void	chdirectory(int);
void	cleanup(int);
void	consh(int);
void	cu_put(int);
void	cu_take(int);
void	dollar(int);
char	*expand(char *);
void	finish(int);
void	genbrk(int);
void	getfl(int);
void	help(int);
void	pipefile(int);
void	pipeout(int);
void	sendfile(int);
void	shell(int);
int	speed(int);
void	suspend(int);
void	timeoutfunc(int);
void	ttysetup(int);
void	variable(int);

void	biz22_abort(void);
int	biz22f_dialer(char *, char *);
int	biz22w_dialer(char *, char *);
void	biz22_disconnect(void);

void	cour_abort(void);
int	cour_dialer(char *, char *);
void	cour_disconnect(void);

void	df_abort(void);
int	df02_dialer(char *, char *);
int	df03_dialer(char *, char *);
void	df_disconnect(void);

void	dn_abort(void);
int	dn_dialer(char *, char *);
void	dn_disconnect(void);

void	hay_abort(void);
int	hay_dialer(char *, char *);
void	hay_disconnect(void);

void	multitech_abort(void);
int	multitech_dialer(char *, char *);
void	multitech_disconnect(void);

void	t3000_abort(void);
int	t3000_dialer(char *, char *);
void	t3000_disconnect(void);

acu_t	*unidialer_getmodem(const char *);

void	v3451_abort(void);
int	v3451_dialer(char *, char *);
void	v3451_disconnect(void);

void	v831_abort(void);
int	v831_dialer(char *, char *);
void	v831_disconnect(void);

void	ven_abort(void);
int	ven_dialer(char *, char *);
void	ven_disconnect(void);

#define TL_VERBOSE       0x00000001
#define TL_SIGNAL_TIPOUT 0x00000002

int tiplink(char *cmd, unsigned int flags);
void raw(void);

/* end of tip.h */
