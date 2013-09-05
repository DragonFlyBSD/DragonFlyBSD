/*
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD: src/sys/ddb/db_output.c,v 1.26 1999/08/28 00:41:09 peter Exp $
 */

/*
 * 	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

/*
 * Printf and character output for debugger.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cons.h>
#include <sys/ctype.h>
#include <sys/thread2.h>
#include <sys/spinlock2.h>

#include <machine/stdarg.h>

#include <ddb/ddb.h>
#include <ddb/db_output.h>

/*
 *	Character output - tracks position in line.
 *	To do this correctly, we should know how wide
 *	the output device is - then we could zero
 *	the line position when the output device wraps
 *	around to the start of the next line.
 *
 *	Instead, we count the number of spaces printed
 *	since the last printing character so that we
 *	don't print trailing spaces.  This avoids most
 *	of the wraparounds.
 */
static int	db_output_position = 0;		/* output column */
static int	db_last_non_space = 0;		/* last non-space character */
db_expr_t	db_tab_stop_width = 8;		/* how wide are tab stops? */
#define	NEXT_TAB(i) \
	((((i) + db_tab_stop_width) / db_tab_stop_width) * db_tab_stop_width)
db_expr_t	db_max_width = 79;		/* output line width */

static void db_putchar (int c, void *arg);

/*
 * Force pending whitespace.
 */
void
db_force_whitespace(void)
{
	int last_print, next_tab;

	last_print = db_last_non_space;
	while (last_print < db_output_position) {
	    next_tab = NEXT_TAB(last_print);
	    if (next_tab <= db_output_position) {
		while (last_print < next_tab) { /* DON'T send a tab!!! */
			cnputc(' ');
			last_print++;
		}
	    }
	    else {
		cnputc(' ');
		last_print++;
	    }
	}
	db_last_non_space = db_output_position;
}

/*
 * Output character.  Buffer whitespace.
 *
 * Parameters:
 *     arg:	character to output
 */
static void
db_putchar(int c, void *arg)
{
	/*
	 * If not in the debugger, output data to both the console and
	 * the message buffer.
	 */
	if (!db_active) {
		if (c == '\r' || c == '\n' || c == '\t' ||
		    isprint(c)) {
			kprintf("%c", c);
		} else {
			kprintf("?");
		}
		if (!db_active)
			return;
		if (c == '\r' || c == '\n')
			db_check_interrupt();
		return;
	}

	crit_enter_hard();

	if (c > ' ' && c <= '~') {
	    /*
	     * Printing character.
	     * If we have spaces to print, print them first.
	     * Use tabs if possible.
	     */
	    db_force_whitespace();
	    cnputc(c);
	    db_output_position++;
	    db_last_non_space = db_output_position;
	}
	else if (c == '\n') {
	    /* Newline */
	    cnputc(c);
	    db_output_position = 0;
	    db_last_non_space = 0;
	    db_check_interrupt();
	}
	else if (c == '\r') {
	    /* Return */
	    cnputc(c);
	    db_output_position = 0;
	    db_last_non_space = 0;
	    db_check_interrupt();
	}
	else if (c == '\t') {
	    /* assume tabs every 8 positions */
	    db_output_position = NEXT_TAB(db_output_position);
	}
	else if (c == ' ') {
	    /* space */
	    db_output_position++;
	}
	else if (c == '\007') {
	    /* bell */
	    cnputc(c);
	}
	/* other characters are assumed non-printing */
	crit_exit_hard();
}

/*
 * Return output position
 */
int
db_print_position(void)
{
	return (db_output_position);
}

/*
 * Printing
 *
 * NOTE: We bypass subr_prf's cons_spin here by using our own putchar
 *	 function.
 */
void
db_printf(const char *fmt, ...)
{
	__va_list listp;

	__va_start(listp, fmt);
	kvcprintf (fmt, db_putchar, NULL, db_radix, listp);
	__va_end(listp);
/*	DELAY(100000);*/
}

void
db_vprintf(const char *fmt, __va_list va)
{
	kvcprintf (fmt, db_putchar, NULL, db_radix, va);
/*	DELAY(100000);*/
}

int db_indent;

void
db_iprintf(const char *fmt,...)
{
	int i;
	__va_list listp;

	for (i = db_indent; i >= 8; i -= 8)
		db_printf("\t");
	while (--i >= 0)
		db_printf(" ");
	__va_start(listp, fmt);
	kvcprintf (fmt, db_putchar, NULL, db_radix, listp);
	__va_end(listp);
}

/*
 * End line if too long.
 */
void
db_end_line(int field_width)
{
	if (db_output_position + field_width > db_max_width)
	    db_printf("\n");
}

/*
 * Simple pager
 */
int
db_more(int *nl)
{
	++*nl;
	if (*nl == 20) {
		int c;

		db_printf("--More--");
		c = cngetc();
		db_printf("\r");
		/*
		 * A whole screenfull or just one line?
		 */
		switch (c) {
		case '\n':		/* just one line */
			*nl = 19;
			break;
		case ' ':
			*nl = 0;	/* another screenfull */
			break;
		default:		/* exit */
			db_printf("\n");
			return(-1);
		}
	}
	return(0);
}

/*
 * Replacement for old '%z' kprintf format.
 */
void
db_format_hex(char *buf, size_t bufsiz, quad_t val, int altflag)
{
	/* Only use alternate form if val is nonzero. */
	const char *fmt = (altflag && val) ? "-%#qx" : "-%qx";

	if (val < 0)
		val = -val;
	else
		++fmt;

	ksnprintf(buf, bufsiz, fmt, val);
}

/* #define TEMPORARY_DEBUGGING */
#ifdef TEMPORARY_DEBUGGING

/*
 * Temporary Debugging, only turned on manually by kernel hackers trying
 * to debug extremely low level code.  Adjust PCHAR_ as required.
 */
static void PCHAR_(int, void * __unused);

void
kprintf0(const char *fmt, ...)
{
	__va_list ap;

	__va_start(ap, fmt);
	kvcprintf(fmt, PCHAR_, NULL, 10, ap);
	__va_end(ap);
}

static void
PCHAR_(int c, void *dummy __unused)
{
	const int COMC_TXWAIT = 0x40000;
	const int COMPORT = 0x2f8;		/* 0x3f8 COM1, 0x2f8 COM2 */
	const int LSR_TXRDY = 0x20;
	const int BAUD = 9600;
	const int com_lsr = 5;
	const int com_data = 0;
	int wait;
	static int setbaud;

	if (setbaud == 0) {
		setbaud = 1;
		outb(COMPORT+3, 0x83);    /* DLAB + 8N1 */
		outb(COMPORT+0, (115200 / BAUD) & 0xFF);
		outb(COMPORT+1, (115200 / BAUD) >> 8);
		outb(COMPORT+3, 0x03);    /* 8N1 */
		outb(COMPORT+4, 0x03);    /* RTS+DTR */
		outb(COMPORT+2, 0x01);    /* FIFO_ENABLE */
	}

	for (wait = COMC_TXWAIT; wait > 0; wait--) {
		if (inb(COMPORT + com_lsr) & LSR_TXRDY) {
			outb(COMPORT + com_data, (u_char)c);
			break;
		}
	}
}

#endif
