/*-
 * Copyright (c) 1986, 1988, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)subr_prf.c	8.3 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/subr_prf.c,v 1.61.2.5 2002/08/31 18:22:08 dwmalone Exp $
 */

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/msgbuf.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/tty.h>
#include <sys/tprintf.h>
#include <sys/stdint.h>
#include <sys/syslog.h>
#include <sys/cons.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/ctype.h>
#include <sys/eventhandler.h>
#include <sys/kthread.h>
#include <sys/cpu_topology.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

/*
 * Note that stdarg.h and the ANSI style va_start macro is used for both
 * ANSI and traditional C compilers.  We use the __ machine version to stay
 * within the kernel header file set.
 */
#include <machine/stdarg.h>

#define TOCONS		0x01
#define TOTTY		0x02
#define TOLOG		0x04
#define TOWAKEUP	0x08

/* Max number conversion buffer length: a u_quad_t in base 2, plus NUL byte. */
#define MAXNBUF	(sizeof(intmax_t) * NBBY + 1)

struct putchar_arg {
	int	flags;
	int	pri;
	struct	tty *tty;
};

struct snprintf_arg {
	char	*str;
	size_t	remain;
};

extern	int log_open;

struct	tty *constty;			/* pointer to console "window" tty */

static void  msglogchar(int c, int pri);
static void  msgaddchar(int c, void *dummy);
static void  kputchar (int ch, void *arg);
static char *ksprintn (char *nbuf, uintmax_t num, int base, int *lenp,
		       int upper);
static void  snprintf_func (int ch, void *arg);

static int consintr = 1;		/* Ok to handle console interrupts? */
static int msgbufmapped;		/* Set when safe to use msgbuf */
static struct spinlock cons_spin = SPINLOCK_INITIALIZER(cons_spin, "cons_spin");
static thread_t constty_td = NULL;

int msgbuftrigger;

static int      log_console_output = 1;
TUNABLE_INT("kern.log_console_output", &log_console_output);
SYSCTL_INT(_kern, OID_AUTO, log_console_output, CTLFLAG_RW,
    &log_console_output, 0, "");

static int unprivileged_read_msgbuf = 1;
SYSCTL_INT(_security, OID_AUTO, unprivileged_read_msgbuf, CTLFLAG_RW,
    &unprivileged_read_msgbuf, 0,
    "Unprivileged processes may read the kernel message buffer");

/*
 * Warn that a system table is full.
 */
void
tablefull(const char *tab)
{

	log(LOG_ERR, "%s: table is full\n", tab);
}

/*
 * Uprintf prints to the controlling terminal for the current process.
 */
int
uprintf(const char *fmt, ...)
{
	struct proc *p = curproc;
	__va_list ap;
	struct putchar_arg pca;
	int retval = 0;

	if (p && (p->p_flags & P_CONTROLT) && p->p_session->s_ttyvp) {
		__va_start(ap, fmt);
		pca.tty = p->p_session->s_ttyp;
		pca.flags = TOTTY;

		retval = kvcprintf(fmt, kputchar, &pca, 10, ap);
		__va_end(ap);
	}
	return (retval);
}

tpr_t
tprintf_open(struct proc *p)
{
	if ((p->p_flags & P_CONTROLT) && p->p_session->s_ttyvp) {
		sess_hold(p->p_session);
		return ((tpr_t) p->p_session);
	}
	return (NULL);
}

void
tprintf_close(tpr_t sess)
{
	if (sess)
		sess_rele((struct session *) sess);
}

/*
 * tprintf prints on the controlling terminal associated
 * with the given session.
 */
int
tprintf(tpr_t tpr, const char *fmt, ...)
{
	struct session *sess = (struct session *)tpr;
	struct tty *tp = NULL;
	int flags = TOLOG;
	__va_list ap;
	struct putchar_arg pca;
	int retval;

	if (sess && sess->s_ttyvp && ttycheckoutq(sess->s_ttyp, 0)) {
		flags |= TOTTY;
		tp = sess->s_ttyp;
	}
	__va_start(ap, fmt);
	pca.tty = tp;
	pca.flags = flags;
	pca.pri = LOG_INFO;
	retval = kvcprintf(fmt, kputchar, &pca, 10, ap);
	__va_end(ap);
	msgbuftrigger = 1;
	return (retval);
}

/*
 * Ttyprintf displays a message on a tty; it should be used only by
 * the tty driver, or anything that knows the underlying tty will not
 * be revoke(2)'d away.  Other callers should use tprintf.
 */
int
ttyprintf(struct tty *tp, const char *fmt, ...)
{
	__va_list ap;
	struct putchar_arg pca;
	int retval;

	__va_start(ap, fmt);
	pca.tty = tp;
	pca.flags = TOTTY;
	retval = kvcprintf(fmt, kputchar, &pca, 10, ap);
	__va_end(ap);
	return (retval);
}

/*
 * Log writes to the log buffer, and guarantees not to sleep (so can be
 * called by interrupt routines).  If there is no process reading the
 * log yet, it writes to the console also.
 */
int
log(int level, const char *fmt, ...)
{
	__va_list ap;
	int retval;
	struct putchar_arg pca;

	pca.tty = NULL;
	pca.pri = level;
	pca.flags = log_open ? TOLOG : TOCONS;

	__va_start(ap, fmt);
	retval = kvcprintf(fmt, kputchar, &pca, 10, ap);
	__va_end(ap);

	msgbuftrigger = 1;
	return (retval);
}

#define CONSCHUNK 128

void
log_console(struct uio *uio)
{
	int c, i, error, iovlen, nl;
	struct uio muio;
	struct iovec *miov = NULL;
	char *consbuffer;
	int pri;

	if (!log_console_output)
		return;

	pri = LOG_INFO | LOG_CONSOLE;
	muio = *uio;
	iovlen = uio->uio_iovcnt * sizeof (struct iovec);
	miov = kmalloc(iovlen, M_TEMP, M_WAITOK);
	consbuffer = kmalloc(CONSCHUNK, M_TEMP, M_WAITOK);
	bcopy((caddr_t)muio.uio_iov, (caddr_t)miov, iovlen);
	muio.uio_iov = miov;
	uio = &muio;

	nl = 0;
	while (uio->uio_resid > 0) {
		c = (int)szmin(uio->uio_resid, CONSCHUNK);
		error = uiomove(consbuffer, (size_t)c, uio);
		if (error != 0)
			break;
		for (i = 0; i < c; i++) {
			msglogchar(consbuffer[i], pri);
			if (consbuffer[i] == '\n')
				nl = 1;
			else
				nl = 0;
		}
	}
	if (!nl)
		msglogchar('\n', pri);
	msgbuftrigger = 1;
	kfree(miov, M_TEMP);
	kfree(consbuffer, M_TEMP);
	return;
}

/*
 * Output to the console.
 */
int
kprintf(const char *fmt, ...)
{
	__va_list ap;
	int savintr;
	struct putchar_arg pca;
	int retval;

	savintr = consintr;		/* disable interrupts */
	consintr = 0;
	__va_start(ap, fmt);
	pca.tty = NULL;
	pca.flags = TOCONS | TOLOG;
	pca.pri = -1;
	retval = kvcprintf(fmt, kputchar, &pca, 10, ap);
	__va_end(ap);
	if (!panicstr)
		msgbuftrigger = 1;
	consintr = savintr;		/* reenable interrupts */
	return (retval);
}

int
kvprintf(const char *fmt, __va_list ap)
{
	int savintr;
	struct putchar_arg pca;
	int retval;

	savintr = consintr;		/* disable interrupts */
	consintr = 0;
	pca.tty = NULL;
	pca.flags = TOCONS | TOLOG;
	pca.pri = -1;
	retval = kvcprintf(fmt, kputchar, &pca, 10, ap);
	if (!panicstr)
		msgbuftrigger = 1;
	consintr = savintr;		/* reenable interrupts */
	return (retval);
}

/*
 * Limited rate kprintf.  The passed rate structure must be initialized
 * with the desired reporting frequency.  A frequency of 0 will result in
 * no output.
 *
 * count may be initialized to a negative number to allow an initial
 * burst.
 */
void
krateprintf(struct krate *rate, const char *fmt, ...)
{
	__va_list ap;

	if (rate->ticks != (int)time_uptime) {
		rate->ticks = (int)time_uptime;
		if (rate->count > 0)
			rate->count = 0;
	}
	if (rate->count < rate->freq) {
		++rate->count;
		__va_start(ap, fmt);
		kvprintf(fmt, ap);
		__va_end(ap);
	}
}

/*
 * Print a character to the dmesg log, the console, and/or the user's
 * terminal.
 *
 * NOTE: TOTTY does not require nonblocking operation, but TOCONS
 * 	 and TOLOG do.  When we have a constty we still output to
 *	 the real console but we have a monitoring thread which
 *	 we wakeup which tracks the log.
 */
static void
kputchar(int c, void *arg)
{
	struct putchar_arg *ap = (struct putchar_arg*) arg;
	int flags = ap->flags;
	struct tty *tp = ap->tty;

	if (panicstr)
		constty = NULL;
	if ((flags & TOCONS) && tp == NULL && constty)
		flags |= TOLOG | TOWAKEUP;
	if ((flags & TOTTY) && tputchar(c, tp) < 0)
		ap->flags &= ~TOTTY;
	if ((flags & TOLOG))
		msglogchar(c, ap->pri);
	if ((flags & TOCONS) && c)
		cnputc(c);
	if (flags & TOWAKEUP)
		wakeup(constty_td);
}

/*
 * Scaled down version of sprintf(3).
 */
int
ksprintf(char *buf, const char *cfmt, ...)
{
	int retval;
	__va_list ap;

	__va_start(ap, cfmt);
	retval = kvcprintf(cfmt, NULL, buf, 10, ap);
	buf[retval] = '\0';
	__va_end(ap);
	return (retval);
}

/*
 * Scaled down version of vsprintf(3).
 */
int
kvsprintf(char *buf, const char *cfmt, __va_list ap)
{
	int retval;

	retval = kvcprintf(cfmt, NULL, buf, 10, ap);
	buf[retval] = '\0';
	return (retval);
}

/*
 * Scaled down version of snprintf(3).
 */
int
ksnprintf(char *str, size_t size, const char *format, ...)
{
	int retval;
	__va_list ap;

	__va_start(ap, format);
	retval = kvsnprintf(str, size, format, ap);
	__va_end(ap);
	return(retval);
}

/*
 * Scaled down version of vsnprintf(3).
 */
int
kvsnprintf(char *str, size_t size, const char *format, __va_list ap)
{
	struct snprintf_arg info;
	int retval;

	info.str = str;
	info.remain = size;
	retval = kvcprintf(format, snprintf_func, &info, 10, ap);
	if (info.remain >= 1)
		*info.str++ = '\0';
	return (retval);
}

int
ksnrprintf(char *str, size_t size, int radix, const char *format, ...)
{
	int retval;
	__va_list ap;

	__va_start(ap, format);
	retval = kvsnrprintf(str, size, radix, format, ap);
	__va_end(ap);
	return(retval);
}

int
kvsnrprintf(char *str, size_t size, int radix, const char *format, __va_list ap)
{
	struct snprintf_arg info;
	int retval;

	info.str = str;
	info.remain = size;
	retval = kvcprintf(format, snprintf_func, &info, radix, ap);
	if (info.remain >= 1)
		*info.str++ = '\0';
	return (retval);
}

int
kvasnrprintf(char **strp, size_t size, int radix,
	     const char *format, __va_list ap)
{
	struct snprintf_arg info;
	int retval;

	*strp = kmalloc(size, M_TEMP, M_WAITOK);
	info.str = *strp;
	info.remain = size;
	retval = kvcprintf(format, snprintf_func, &info, radix, ap);
	if (info.remain >= 1)
		*info.str++ = '\0';
	return (retval);
}

void
kvasfree(char **strp)
{
	if (*strp) {
		kfree(*strp, M_TEMP);
		*strp = NULL;
	}
}

static void
snprintf_func(int ch, void *arg)
{
	struct snprintf_arg *const info = arg;

	if (info->remain >= 2) {
		*info->str++ = ch;
		info->remain--;
	}
}

/*
 * Put a NUL-terminated ASCII number (base <= 36) in a buffer in reverse
 * order; return an optional length and a pointer to the last character
 * written in the buffer (i.e., the first character of the string).
 * The buffer pointed to by `nbuf' must have length >= MAXNBUF.
 */
static char *
ksprintn(char *nbuf, uintmax_t num, int base, int *lenp, int upper)
{
	char *p, c;

	p = nbuf;
	*p = '\0';
	do {
		c = hex2ascii(num % base);
		*++p = upper ? toupper(c) : c;
	} while (num /= base);
	if (lenp)
		*lenp = p - nbuf;
	return (p);
}

/*
 * Scaled down version of printf(3).
 *
 * Two additional formats:
 *
 * The format %b is supported to decode error registers.
 * Its usage is:
 *
 *	kprintf("reg=%b\n", regval, "<base><arg>*");
 *
 * where <base> is the output base expressed as a control character, e.g.
 * \10 gives octal; \20 gives hex.  Each arg is a sequence of characters,
 * the first of which gives the bit number to be inspected (origin 1), and
 * the next characters (up to a control character, i.e. a character <= 32),
 * give the name of the register.  Thus:
 *
 *	kvcprintf("reg=%b\n", 3, "\10\2BITTWO\1BITONE\n");
 *
 * would produce output:
 *
 *	reg=3<BITTWO,BITONE>
 */

#define PCHAR(c) {int cc=(c); if(func) (*func)(cc,arg); else *d++=cc; retval++;}

int
kvcprintf(char const *fmt, void (*func)(int, void*), void *arg,
	  int radix, __va_list ap)
{
	char nbuf[MAXNBUF];
	char *d;
	const char *p, *percent, *q;
	int ch, n;
	uintmax_t num;
	int base, tmp, width, ladjust, sharpflag, neg, sign, dot;
	int cflag, hflag, jflag, lflag, qflag, tflag, zflag;
	int dwidth, upper;
	char padc;
	int retval = 0, stop = 0;
	int usespin;

	/*
	 * Make a supreme effort to avoid reentrant panics or deadlocks.
	 *
	 * NOTE!  Do nothing that would access mycpu/gd/fs unless the
	 *	  function is the normal kputchar(), which allows us to
	 *	  use this function for very early debugging with a special
	 *	  function.
	 */
	if (func == kputchar) {
		if (mycpu->gd_flags & GDF_KPRINTF)
			return(0);
		atomic_set_long(&mycpu->gd_flags, GDF_KPRINTF);
	}

	num = 0;
	if (!func)
		d = (char *) arg;
	else
		d = NULL;

	if (fmt == NULL)
		fmt = "(fmt null)\n";

	if (radix < 2 || radix > 36)
		radix = 10;

	usespin = (func == kputchar &&
		   panic_cpu_gd != mycpu &&
		   (((struct putchar_arg *)arg)->flags & TOTTY) == 0);
	if (usespin) {
		crit_enter_hard();
		spin_lock(&cons_spin);
	}

	for (;;) {
		padc = ' ';
		width = 0;
		while ((ch = (u_char)*fmt++) != '%' || stop) {
			if (ch == '\0')
				goto done;
			PCHAR(ch);
		}
		percent = fmt - 1;
		dot = dwidth = ladjust = neg = sharpflag = sign = upper = 0;
		cflag = hflag = jflag = lflag = qflag = tflag = zflag = 0;

reswitch:
		switch (ch = (u_char)*fmt++) {
		case '.':
			dot = 1;
			goto reswitch;
		case '#':
			sharpflag = 1;
			goto reswitch;
		case '+':
			sign = 1;
			goto reswitch;
		case '-':
			ladjust = 1;
			goto reswitch;
		case '%':
			PCHAR(ch);
			break;
		case '*':
			if (!dot) {
				width = __va_arg(ap, int);
				if (width < 0) {
					ladjust = !ladjust;
					width = -width;
				}
			} else {
				dwidth = __va_arg(ap, int);
			}
			goto reswitch;
		case '0':
			if (!dot) {
				padc = '0';
				goto reswitch;
			}
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
				for (n = 0;; ++fmt) {
					n = n * 10 + ch - '0';
					ch = *fmt;
					if (ch < '0' || ch > '9')
						break;
				}
			if (dot)
				dwidth = n;
			else
				width = n;
			goto reswitch;
		case 'b':
			num = (u_int)__va_arg(ap, int);
			p = __va_arg(ap, char *);
			for (q = ksprintn(nbuf, num, *p++, NULL, 0); *q;)
				PCHAR(*q--);

			if (num == 0)
				break;

			for (tmp = 0; *p;) {
				n = *p++;
				if (num & (1 << (n - 1))) {
					PCHAR(tmp ? ',' : '<');
					for (; (n = *p) > ' '; ++p)
						PCHAR(n);
					tmp = 1;
				} else
					for (; *p > ' '; ++p)
						continue;
			}
			if (tmp)
				PCHAR('>');
			break;
		case 'c':
			PCHAR(__va_arg(ap, int));
			break;
		case 'd':
		case 'i':
			base = 10;
			sign = 1;
			goto handle_sign;
		case 'h':
			if (hflag) {
				hflag = 0;
				cflag = 1;
			} else
				hflag = 1;
			goto reswitch;
		case 'j':
			jflag = 1;
			goto reswitch;
		case 'l':
			if (lflag) {
				lflag = 0;
				qflag = 1;
			} else
				lflag = 1;
			goto reswitch;
		case 'n':
			if (cflag)
				*(__va_arg(ap, char *)) = retval;
			else if (hflag)
				*(__va_arg(ap, short *)) = retval;
			else if (jflag)
				*(__va_arg(ap, intmax_t *)) = retval;
			else if (lflag)
				*(__va_arg(ap, long *)) = retval;
			else if (qflag)
				*(__va_arg(ap, quad_t *)) = retval;
			else
				*(__va_arg(ap, int *)) = retval;
			break;
		case 'o':
			base = 8;
			goto handle_nosign;
		case 'p':
			base = 16;
			sharpflag = (width == 0);
			sign = 0;
			num = (uintptr_t)__va_arg(ap, void *);
			goto number;
		case 'q':
			qflag = 1;
			goto reswitch;
		case 'r':
			base = radix;
			if (sign)
				goto handle_sign;
			goto handle_nosign;
		case 's':
			p = __va_arg(ap, char *);
			if (p == NULL)
				p = "(null)";
			if (!dot)
				n = strlen (p);
			else
				for (n = 0; n < dwidth && p[n]; n++)
					continue;

			width -= n;

			if (!ladjust && width > 0)
				while (width--)
					PCHAR(padc);
			while (n--)
				PCHAR(*p++);
			if (ladjust && width > 0)
				while (width--)
					PCHAR(padc);
			break;
		case 't':
			tflag = 1;
			goto reswitch;
		case 'u':
			base = 10;
			goto handle_nosign;
		case 'X':
			upper = 1;
			/* FALLTHROUGH */
		case 'x':
			base = 16;
			goto handle_nosign;
		case 'z':
			zflag = 1;
			goto reswitch;
handle_nosign:
			sign = 0;
			if (cflag)
				num = (u_char)__va_arg(ap, int);
			else if (hflag)
				num = (u_short)__va_arg(ap, int);
			else if (jflag)
				num = __va_arg(ap, uintmax_t);
			else if (lflag)
				num = __va_arg(ap, u_long);
			else if (qflag)
				num = __va_arg(ap, u_quad_t);
			else if (tflag)
				num = __va_arg(ap, ptrdiff_t);
			else if (zflag)
				num = __va_arg(ap, size_t);
			else
				num = __va_arg(ap, u_int);
			goto number;
handle_sign:
			if (cflag)
				num = (char)__va_arg(ap, int);
			else if (hflag)
				num = (short)__va_arg(ap, int);
			else if (jflag)
				num = __va_arg(ap, intmax_t);
			else if (lflag)
				num = __va_arg(ap, long);
			else if (qflag)
				num = __va_arg(ap, quad_t);
			else if (tflag)
				num = __va_arg(ap, ptrdiff_t);
			else if (zflag)
				num = __va_arg(ap, ssize_t);
			else
				num = __va_arg(ap, int);
number:
			if (sign && (intmax_t)num < 0) {
				neg = 1;
				num = -(intmax_t)num;
			}
			p = ksprintn(nbuf, num, base, &n, upper);
			tmp = 0;
			if (sharpflag && num != 0) {
				if (base == 8)
					tmp++;
				else if (base == 16)
					tmp += 2;
			}
			if (neg)
				tmp++;

			if (!ladjust && padc == '0')
				dwidth = width - tmp;
			width -= tmp + imax(dwidth, n);
			dwidth -= n;
			if (!ladjust)
				while (width-- > 0)
					PCHAR(' ');
			if (neg)
				PCHAR('-');
			if (sharpflag && num != 0) {
				if (base == 8) {
					PCHAR('0');
				} else if (base == 16) {
					PCHAR('0');
					PCHAR('x');
				}
			}
			while (dwidth-- > 0)
				PCHAR('0');

			while (*p)
				PCHAR(*p--);

			if (ladjust)
				while (width-- > 0)
					PCHAR(' ');

			break;
		default:
			while (percent < fmt)
				PCHAR(*percent++);
			/*
			 * Since we ignore an formatting argument it is no 
			 * longer safe to obey the remaining formatting
			 * arguments as the arguments will no longer match
			 * the format specs.
			 */
			stop = 1;
			break;
		}
	}
done:
	/*
	 * Cleanup reentrancy issues.
	 */
	if (func == kputchar)
		atomic_clear_long(&mycpu->gd_flags, GDF_KPRINTF);
	if (usespin) {
		spin_unlock(&cons_spin);
		crit_exit_hard();
	}
	return (retval);
}

#undef PCHAR

/*
 * Called from the panic code to try to get the console working
 * again in case we paniced inside a kprintf().
 */
void
kvcreinitspin(void)
{
	spin_init(&cons_spin, "kvcre");
	atomic_clear_long(&mycpu->gd_flags, GDF_KPRINTF);
}

/*
 * Console support thread for constty intercepts.  This is needed because
 * console tty intercepts can block.  Instead of having kputchar() attempt
 * to directly write to the console intercept we just force it to log
 * and wakeup this baby to track and dump the log to constty.
 */
static void
constty_daemon(void)
{
	u_int rindex;
	u_int xindex;
	u_int n;
        struct msgbuf *mbp;
	struct tty *tp;

        EVENTHANDLER_REGISTER(shutdown_pre_sync, shutdown_kproc,
                              constty_td, SHUTDOWN_PRI_FIRST);
        constty_td->td_flags |= TDF_SYSTHREAD;

	mbp = msgbufp;
	rindex = mbp->msg_bufr;		/* persistent loop variable */
	xindex = mbp->msg_bufx - 1;	/* anything different than bufx */
	cpu_ccfence();

        for (;;) {
                kproc_suspend_loop();

		crit_enter();
		if (mbp != msgbufp)
			mbp = msgbufp;
		if (xindex == mbp->msg_bufx ||
		    mbp == NULL ||
		    msgbufmapped == 0) {
			tsleep(constty_td, 0, "waiting", hz*60);
			crit_exit();
			continue;
		}
		crit_exit();

		/*
		 * Get message buf FIFO indices.  rindex is tracking.
		 */
		xindex = mbp->msg_bufx;
		cpu_ccfence();
		if ((tp = constty) == NULL) {
			rindex = xindex;
			continue;
		}

		/*
		 * Check if the calculated bytes has rolled the whole
		 * message buffer.
		 */
		n = xindex - rindex;
		if (n > mbp->msg_size - 1024) {
			rindex = xindex - mbp->msg_size + 2048;
			n = xindex - rindex;
		}

		/*
		 * And dump it.  If constty gets stuck will give up.
		 */
		while (rindex != xindex) {
			u_int ri = rindex % mbp->msg_size;
			if (tputchar((uint8_t)mbp->msg_ptr[ri], tp) < 0) {
				constty = NULL;
				rindex = xindex;
				break;
			}
                        if (tp->t_outq.c_cc >= tp->t_ohiwat) {
				tsleep(constty_daemon, 0, "blocked", hz / 10);
				if (tp->t_outq.c_cc >= tp->t_ohiwat) {
					rindex = xindex;
					break;
				}
			}
			++rindex;
		}
	}
}

static struct kproc_desc constty_kp = {
        "consttyd",
	constty_daemon,
        &constty_td
};
SYSINIT(bufdaemon, SI_SUB_KTHREAD_UPDATE, SI_ORDER_ANY,
        kproc_start, &constty_kp)

/*
 * Put character in log buffer with a particular priority.
 *
 * MPSAFE
 */
static void
msglogchar(int c, int pri)
{
	static int lastpri = -1;
	static int dangling;
	char nbuf[MAXNBUF];
	char *p;

	if (!msgbufmapped)
		return;
	if (c == '\0' || c == '\r')
		return;
	if (pri != -1 && pri != lastpri) {
		if (dangling) {
			msgaddchar('\n', NULL);
			dangling = 0;
		}
		msgaddchar('<', NULL);
		for (p = ksprintn(nbuf, (uintmax_t)pri, 10, NULL, 0); *p;)
			msgaddchar(*p--, NULL);
		msgaddchar('>', NULL);
		lastpri = pri;
	}
	msgaddchar(c, NULL);
	if (c == '\n') {
		dangling = 0;
		lastpri = -1;
	} else {
		dangling = 1;
	}
}

/*
 * Put char in log buffer.   Make sure nothing blows up beyond repair if
 * we have an MP race.
 *
 * MPSAFE.
 */
static void
msgaddchar(int c, void *dummy)
{
	struct msgbuf *mbp;
	u_int lindex;
	u_int rindex;
	u_int xindex;
	u_int n;

	if (!msgbufmapped)
		return;
	mbp = msgbufp;
	lindex = mbp->msg_bufl;
	rindex = mbp->msg_bufr;
	xindex = mbp->msg_bufx++;	/* Allow SMP race */
	cpu_ccfence();

	mbp->msg_ptr[xindex % mbp->msg_size] = c;
	n = xindex - lindex;
	if (n > mbp->msg_size - 1024) {
		lindex = xindex - mbp->msg_size + 2048;
		cpu_ccfence();
		mbp->msg_bufl = lindex;
	}
	n = xindex - rindex;
	if (n > mbp->msg_size - 1024) {
		rindex = xindex - mbp->msg_size + 2048;
		cpu_ccfence();
		mbp->msg_bufr = rindex;
	}
}

static void
msgbufcopy(struct msgbuf *oldp)
{
	u_int rindex;
	u_int xindex;
	u_int n;

	rindex = oldp->msg_bufr;
	xindex = oldp->msg_bufx;
	cpu_ccfence();

	n = xindex - rindex;
	if (n > oldp->msg_size - 1024)
		rindex = xindex - oldp->msg_size + 2048;
	while (rindex != xindex) {
		msglogchar(oldp->msg_ptr[rindex % oldp->msg_size], -1);
		++rindex;
	}
}

void
msgbufinit(void *ptr, size_t size)
{
	char *cp;
	static struct msgbuf *oldp = NULL;

	size -= sizeof(*msgbufp);
	cp = (char *)ptr;
	msgbufp = (struct msgbuf *) (cp + size);
	if (msgbufp->msg_magic != MSG_MAGIC || msgbufp->msg_size != size) {
		bzero(cp, size);
		bzero(msgbufp, sizeof(*msgbufp));
		msgbufp->msg_magic = MSG_MAGIC;
		msgbufp->msg_size = (char *)msgbufp - cp;
	}
	msgbufp->msg_ptr = cp;
	if (msgbufmapped && oldp != msgbufp)
		msgbufcopy(oldp);
	cpu_mfence();
	msgbufmapped = 1;
	oldp = msgbufp;
}

/* Sysctls for accessing/clearing the msgbuf */

static int
sysctl_kern_msgbuf(SYSCTL_HANDLER_ARGS)
{
        struct msgbuf *mbp;
	struct ucred *cred;
	int error;
	u_int rindex_modulo;
	u_int xindex_modulo;
	u_int rindex;
	u_int xindex;
	u_int n;

	/*
	 * Only wheel or root can access the message log.
	 */
	if (unprivileged_read_msgbuf == 0) {
		KKASSERT(req->td->td_proc);
		cred = req->td->td_proc->p_ucred;

		if ((cred->cr_prison || groupmember(0, cred) == 0) &&
		    priv_check(req->td, PRIV_ROOT) != 0
		) {
			return (EPERM);
		}
	}

	/*
	 * Unwind the buffer, so that it's linear (possibly starting with
	 * some initial nulls).
	 *
	 * We don't push the entire buffer like we did before because
	 * bufr (and bufl) now advance in chunks when the fifo is full,
	 * rather than one character.
	 */
	mbp = msgbufp;
	rindex = mbp->msg_bufr;
	xindex = mbp->msg_bufx;
	n = xindex - rindex;
	if (n > mbp->msg_size - 1024) {
		rindex = xindex - mbp->msg_size + 2048;
		n = xindex - rindex;
	}
	rindex_modulo = rindex % mbp->msg_size;
	xindex_modulo = xindex % mbp->msg_size;

	if (rindex_modulo < xindex_modulo) {
		/*
		 * Can handle in one linear section.
		 */
		error = sysctl_handle_opaque(oidp,
					     mbp->msg_ptr + rindex_modulo,
					     xindex_modulo - rindex_modulo,
					     req);
	} else if (rindex_modulo == xindex_modulo) {
		/*
		 * Empty buffer, just return a single newline
		 */
		error = sysctl_handle_opaque(oidp, "\n", 1, req);
	} else if (n <= mbp->msg_size - rindex_modulo) {
		/*
		 * Can handle in one linear section.
		 */
		error = sysctl_handle_opaque(oidp,
					     mbp->msg_ptr + rindex_modulo,
					     n - rindex_modulo,
					     req);
	} else {
		/*
		 * Glue together two linear sections into one contiguous
		 * output.
		 */
		error = sysctl_handle_opaque(oidp,
					     mbp->msg_ptr + rindex_modulo,
					     mbp->msg_size - rindex_modulo,
					     req);
		n -= mbp->msg_size - rindex_modulo;
		if (error == 0)
			error = sysctl_handle_opaque(oidp, mbp->msg_ptr,
						     n, req);
	}
	if (error)
		return (error);
	return (error);
}

SYSCTL_PROC(_kern, OID_AUTO, msgbuf, CTLTYPE_STRING | CTLFLAG_RD,
    0, 0, sysctl_kern_msgbuf, "A", "Contents of kernel message buffer");

static int msgbuf_clear;

static int
sysctl_kern_msgbuf_clear(SYSCTL_HANDLER_ARGS)
{
	int error;
	error = sysctl_handle_int(oidp, oidp->oid_arg1, oidp->oid_arg2, req);
	if (!error && req->newptr) {
		/* Clear the buffer and reset write pointer */
		msgbufp->msg_bufr = msgbufp->msg_bufx;
		msgbufp->msg_bufl = msgbufp->msg_bufx;
		bzero(msgbufp->msg_ptr, msgbufp->msg_size);
		msgbuf_clear = 0;
	}
	return (error);
}

SYSCTL_PROC(_kern, OID_AUTO, msgbuf_clear,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_SECURE, &msgbuf_clear, 0,
    sysctl_kern_msgbuf_clear, "I", "Clear kernel message buffer");

#ifdef DDB

DB_SHOW_COMMAND(msgbuf, db_show_msgbuf)
{
	u_int rindex;
	u_int i;
	u_int j;

	if (!msgbufmapped) {
		db_printf("msgbuf not mapped yet\n");
		return;
	}
	db_printf("msgbufp = %p\n", msgbufp);
	db_printf("magic = %x, size = %d, r= %d, w = %d, ptr = %p\n",
		  msgbufp->msg_magic, msgbufp->msg_size,
		  msgbufp->msg_bufr % msgbufp->msg_size,
		  msgbufp->msg_bufx % msgbufp->msg_size,
		  msgbufp->msg_ptr);

	rindex = msgbufp->msg_bufr;
	for (i = 0; i < msgbufp->msg_size; i++) {
		j = (i + rindex) % msgbufp->msg_size;
		db_printf("%c", msgbufp->msg_ptr[j]);
	}
	db_printf("\n");
}

#endif /* DDB */


void
hexdump(const void *ptr, int length, const char *hdr, int flags)
{
	int i, j, k;
	int cols;
	const unsigned char *cp;
	char delim;

	if ((flags & HD_DELIM_MASK) != 0)
		delim = (flags & HD_DELIM_MASK) >> 8;
	else
		delim = ' ';

	if ((flags & HD_COLUMN_MASK) != 0)
		cols = flags & HD_COLUMN_MASK;
	else
		cols = 16;

	cp = ptr;
	for (i = 0; i < length; i+= cols) {
		if (hdr != NULL)
			kprintf("%s", hdr);

		if ((flags & HD_OMIT_COUNT) == 0)
			kprintf("%04x  ", i);

		if ((flags & HD_OMIT_HEX) == 0) {
			for (j = 0; j < cols; j++) {
				k = i + j;
				if (k < length)
					kprintf("%c%02x", delim, cp[k]);
				else
					kprintf("   ");
			}
		}

		if ((flags & HD_OMIT_CHARS) == 0) {
			kprintf("  |");
			for (j = 0; j < cols; j++) {
				k = i + j;
				if (k >= length)
					kprintf(" ");
				else if (cp[k] >= ' ' && cp[k] <= '~')
					kprintf("%c", cp[k]);
				else
					kprintf(".");
			}
			kprintf("|");
		}
		kprintf("\n");
	}
}

void
kprint_cpuset(cpumask_t *mask)
{
	int i;
	int b = -1;
	int e = -1;
	int more = 0;

	kprintf("cpus(");
	CPUSET_FOREACH(i, *mask) {
		if (b < 0) {
			b = i;
			e = b + 1;
			continue;
		}
		if (e == i) {
			++e;
			continue;
		}
		if (more)
			kprintf(", ");
		if (b == e - 1) {
			kprintf("%d", b);
		} else {
			kprintf("%d-%d", b, e - 1);
		}
		more = 1;
		b = i;
		e = b + 1;
	}
	if (more)
		kprintf(", ");
	if (b >= 0) {
		if (b == e + 1) {
			kprintf("%d", b);
		} else {
			kprintf("%d-%d", b, e - 1);
		}
	}
	kprintf(") ");
}
