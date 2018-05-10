 /*
  * This module intercepts syslog() library calls and redirects their output
  * to the standard output stream. For interactive testing.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#include <stdarg.h>
#include <stdio.h>

extern char *percent_m();

/* openlog - dummy */

/* ARGSUSED */

void
openlog(name, logopt, facility)
char   *name;
int     logopt;
int     facility;
{
    /* void */
}

/* vsyslog - format one record */

void
vsyslog(severity, fmt, ap)
int     severity;
char   *fmt;
va_list ap;
{
    char    buf[BUFSIZ];

    vprintf(percent_m(buf, fmt), ap);
    printf("\n");
    fflush(stdout);
}

/* syslog - format one record */

/* VARARGS */

void
syslog(int severity, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsyslog(severity, fmt, ap);
    va_end(ap);
}

/* closelog - dummy */

void
closelog()
{
    /* void */
}
