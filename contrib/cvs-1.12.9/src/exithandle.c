/*
 *    Copyright (c) 2003, Derek Price and Ximbiot <http://ximbiot.com>
 *
 *    You may distribute under the terms of the GNU General Public License
 *    as specified in the README file that comes with the CVS source
 *    distribution.
 *
 * This is a convenience wrapper for some of the functions in lib/sighandle.c.
 */

#include "cvs.h"

/*
 * Register a handler for all signals.
 */
void
signals_register (RETSIGTYPE (*handler)(int))
{
#ifndef DONT_USE_SIGNALS
#ifdef SIGABRT
	(void) SIG_register (SIGABRT, handler);
#endif
#ifdef SIGHUP
	(void) SIG_register (SIGHUP, handler);
#endif
#ifdef SIGINT
	(void) SIG_register (SIGINT, handler);
#endif
#ifdef SIGQUIT
	(void) SIG_register (SIGQUIT, handler);
#endif
#ifdef SIGPIPE
	(void) SIG_register (SIGPIPE, handler);
#endif
#ifdef SIGTERM
	(void) SIG_register (SIGTERM, handler);
#endif
#endif /* !DONT_USE_SIGNALS */
}

/*
 * Register a handler for all signals and exit.
 */
void
cleanup_register (void (*handler) (void))
{
    signals_register (handler);
    atexit (handler);
}
