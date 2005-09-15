 /*
  * Routines to report various classes of problems. Each report is decorated
  * with the current context (file name and line number), if available.
  * 
  * tcpd_warn() reports a problem and proceeds.
  * 
  * tcpd_jump() reports a problem and jumps.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * @(#) diag.c 1.1 94/12/28 17:42:20
  * $DragonFly: src/contrib/tcp_wrappers/diag.c,v 1.2 2005/09/15 04:33:04 sephe Exp $
  */

/* System libraries */

#include <syslog.h>
#include <setjmp.h>
#include <stdarg.h>

/* Local stuff */

#include "tcpd.h"

struct tcpd_context tcpd_context;
jmp_buf tcpd_buf;

/* tcpd_diag - centralize error reporter */

static void
tcpd_diag(int severity, const char *tag, const char *format, va_list ap)
{
    char    fmt[BUFSIZ];

    if (tcpd_context.file)
	sprintf(fmt, "%s: %s, line %d: %s",
		tag, tcpd_context.file, tcpd_context.line, format);
    else
	sprintf(fmt, "%s: %s", tag, format);
    vsyslog(severity, fmt, ap);
}

/* tcpd_warn - report problem of some sort and proceed */

void
tcpd_warn(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    tcpd_diag(LOG_ERR, "warning", format, ap);
    va_end(ap);
}

/* tcpd_jump - report serious problem and jump */

void
tcpd_jump(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    tcpd_diag(LOG_ERR, "error", format, ap);
    va_end(ap);
    longjmp(tcpd_buf, AC_ERROR);
}
