/* CVS client logging buffer.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.  */

#include <config.h>

#include <stdio.h>

#include "cvs.h"
#include "buffer.h"

#ifdef CLIENT_SUPPORT

/* We want to be able to log data sent between us and the server.  We
   do it using log buffers.  Each log buffer has another buffer which
   handles the actual I/O, and a file to log information to.

   This structure is the closure field of a log buffer.  */

struct log_buffer
{
    /* The underlying buffer.  */
    struct buffer *buf;
    /* The file to log information to.  */
    FILE *log;
};

static int log_buffer_input (void *, char *, int, int, int *);
static int log_buffer_output (void *, const char *, int, int *);
static int log_buffer_flush (void *);
static int log_buffer_block (void *, int);
static int log_buffer_shutdown (struct buffer *);

/* Create a log buffer.  */

static struct buffer *
log_buffer_initialize (struct buffer *buf, FILE *fp, int input, void (*memory) (struct buffer *))
{
    struct log_buffer *n;

    n = (struct log_buffer *) xmalloc (sizeof *n);
    n->buf = buf;
    n->log = fp;
    return buf_initialize (input ? log_buffer_input : NULL,
			   input ? NULL : log_buffer_output,
			   input ? NULL : log_buffer_flush,
			   log_buffer_block,
			   log_buffer_shutdown,
			   memory,
			   n);
}

/* The input function for a log buffer.  */

static int
log_buffer_input (void *closure, char *data, int need, int size, int *got)
{
    struct log_buffer *lb = (struct log_buffer *) closure;
    int status;
    size_t n_to_write;

    if (lb->buf->input == NULL)
	abort ();

    status = (*lb->buf->input) (lb->buf->closure, data, need, size, got);
    if (status != 0)
	return status;

    if (*got > 0)
    {
	n_to_write = *got;
	if (fwrite (data, 1, n_to_write, lb->log) != n_to_write)
	    error (0, errno, "writing to log file");
    }

    return 0;
}

/* The output function for a log buffer.  */

static int
log_buffer_output (void *closure, const char *data, int have, int *wrote)
{
    struct log_buffer *lb = (struct log_buffer *) closure;
    int status;
    size_t n_to_write;

    if (lb->buf->output == NULL)
	abort ();

    status = (*lb->buf->output) (lb->buf->closure, data, have, wrote);
    if (status != 0)
	return status;

    if (*wrote > 0)
    {
	n_to_write = *wrote;
	if (fwrite (data, 1, n_to_write, lb->log) != n_to_write)
	    error (0, errno, "writing to log file");
    }

    return 0;
}

/* The flush function for a log buffer.  */

static int
log_buffer_flush (void *closure)
{
    struct log_buffer *lb = (struct log_buffer *) closure;

    if (lb->buf->flush == NULL)
	abort ();

    /* We don't really have to flush the log file here, but doing it
       will let tail -f on the log file show what is sent to the
       network as it is sent.  */
    if (fflush (lb->log) != 0)
        error (0, errno, "flushing log file");

    return (*lb->buf->flush) (lb->buf->closure);
}

/* The block function for a log buffer.  */

static int
log_buffer_block (void *closure, int block)
{
    struct log_buffer *lb = (struct log_buffer *) closure;

    if (block)
	return set_block (lb->buf);
    else
	return set_nonblock (lb->buf);
}

/* The shutdown function for a log buffer.  */

static int
log_buffer_shutdown (struct buffer *buf)
{
    struct log_buffer *lb = (struct log_buffer *) buf->closure;
    int retval;

    retval = buf_shutdown (lb->buf);
    if (fclose (lb->log) < 0)
	error (0, errno, "closing log file");
    return retval;
}


void
setup_logfiles (struct buffer **to_server_p, struct buffer **from_server_p)
{
  char *log = getenv ("CVS_CLIENT_LOG");

  /* Set up logfiles, if any.
   *
   * We do this _after_ authentication on purpose.  Wouldn't really like to
   * worry about logging passwords...
   */
  if (log)
    {
      int len = strlen (log);
      char *buf = xmalloc (len + 5);
      char *p;
      FILE *fp;

      strcpy (buf, log);
      p = buf + len;

      /* Open logfiles in binary mode so that they reflect
	 exactly what was transmitted and received (that is
	 more important than that they be maximally
	 convenient to view).  */
      /* Note that if we create several connections in a single CVS client
	 (currently used by update.c), then the last set of logfiles will
	 overwrite the others.  There is currently no way around this.  */
      strcpy (p, ".in");
      fp = open_file (buf, "wb");
      if (fp == NULL)
	error (0, errno, "opening to-server logfile %s", buf);
      else
	*to_server_p = log_buffer_initialize (*to_server_p, fp, 0,
					      (BUFMEMERRPROC) NULL);

      strcpy (p, ".out");
      fp = open_file (buf, "wb");
      if (fp == NULL)
	error (0, errno, "opening from-server logfile %s", buf);
      else
	*from_server_p = log_buffer_initialize (*from_server_p, fp, 1,
						(BUFMEMERRPROC) NULL);

      free (buf);
    }
}

#endif /* CLIENT_SUPPORT */
