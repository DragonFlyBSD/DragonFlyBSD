/* error.c -- error handler for noninteractive utilities
   Copyright (C) 1990-1992 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.  */

/* David MacKenzie */
/* Brian Berliner added support for CVS */

#include "cvs.h"

/* If non-zero, error will use the CVS protocol to stdout to report error
   messages.  This will only be set in the CVS server parent process;
   most other code is run via do_cvs_command, which forks off a child
   process and packages up its stderr in the protocol.  */
int error_use_protocol; 

#ifndef strerror
extern char *strerror (int);
#endif



/* Print the program name and error message MESSAGE, which is a printf-style
   format string with optional args.  This is a very limited printf subset:
   %s, %d, %c, %x and %% only (without anything between the % and the s,
   d, &c).  Callers who want something fancier can use sprintf.

   If ERRNUM is nonzero, print its corresponding system error message.
   Exit with status EXIT_FAILURE if STATUS is nonzero.  If MESSAGE is "",
   no need to print a message.

   I think this is largely cleaned up to the point where it does the right
   thing for the server, whether the normal server_active (child process)
   case or the error_use_protocol (parent process) case.  The one exception
   is that STATUS nonzero for error_use_protocol probably doesn't work yet;
   in that case still need to use the pending_error machinery in server.c.

   error() does not molest errno; some code (e.g. Entries_Open) depends
   on being able to say something like:
      error (0, 0, "foo");
      error (0, errno, "bar");

   */

/* VARARGS */
void
error (int status, int errnum, const char *message, ...)
{
    int save_errno = errno;

    if (message[0] != '\0')
    {
	va_list args;
	const char *p;
	char *q;
	char *str;
	int num;
	long lnum;
	unsigned int unum;
	unsigned long ulnum;
	int ch;
	char buf[100];

	cvs_outerr (program_name, 0);
	if (cvs_cmd_name && *cvs_cmd_name)
	{
	    cvs_outerr (" ", 1);
	    if (status != 0)
		cvs_outerr ("[", 1);
	    cvs_outerr (cvs_cmd_name, 0);
	    if (status != 0)
		cvs_outerr (" aborted]", 0);
	}
	cvs_outerr (": ", 2);

	va_start( args, message );
	p = message;
	while ((q = strchr (p, '%')) != NULL)
	{
	    static const char msg[] =
		"\ninternal error: bad % in error()\n";
	    if (q - p > 0)
		cvs_outerr (p, q - p);

	    switch (q[1])
	    {
	    case 's':
		str = va_arg (args, char *);
		cvs_outerr (str, strlen (str));
		break;
	    case 'd':
		num = va_arg (args, int);
		sprintf (buf, "%d", num);
		cvs_outerr (buf, strlen (buf));
		break;
	    case 'l':
		if (q[2] == 'd')
		{
		    lnum = va_arg (args, long);
		    sprintf (buf, "%ld", lnum);
		}
		else if (q[2] == 'u')
		{
		    ulnum = va_arg (args, unsigned long);
		    sprintf (buf, "%lu", ulnum);
		}
		else goto bad;
		cvs_outerr (buf, strlen (buf));
		q++;
		break;
	    case 'x':
		unum = va_arg (args, unsigned int);
		sprintf (buf, "%x", unum);
		cvs_outerr (buf, strlen (buf));
		break;
	    case 'c':
		ch = va_arg (args, int);
		buf[0] = ch;
		cvs_outerr (buf, 1);
		break;
	    case '%':
		cvs_outerr ("%", 1);
		break;
	    default:
	    bad:
		cvs_outerr (msg, sizeof (msg) - 1);
		/* Don't just keep going, because q + 1 might point to the
		   terminating '\0'.  */
		goto out;
	    }
	    p = q + 2;
	}
	cvs_outerr (p, strlen (p));
    out:
	va_end (args);

	if (errnum != 0)
	{
	    cvs_outerr (": ", 2);
	    cvs_outerr (strerror (errnum), 0);
	}
	cvs_outerr ("\n", 1);
    }

    if (status)
	exit (EXIT_FAILURE);
    errno = save_errno;
}

/* Print the program name and error message MESSAGE, which is a printf-style
   format string with optional args to the file specified by FP.
   If ERRNUM is nonzero, print its corresponding system error message.
   Exit with status EXIT_FAILURE if STATUS is nonzero.  */
/* VARARGS */
void
fperrmsg (FILE *fp, int status, int errnum, char *message, ...)
{
    va_list args;

    fprintf (fp, "%s: ", program_name);
    va_start( args, message );
    vfprintf (fp, message, args);
    va_end (args);
    if (errnum)
	fprintf (fp, ": %s", strerror (errnum));
    putc ('\n', fp);
    fflush (fp);
    if (status)
	exit (EXIT_FAILURE);
}
