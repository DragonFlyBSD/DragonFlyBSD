/* run.c --- routines for executing subprocesses.
   
   This file is part of GNU CVS.

   GNU CVS is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.  */

#include "cvs.h"

#ifndef HAVE_UNISTD_H
extern int execvp (char *file, char **argv);
#endif



/*
 * To exec a program under CVS, first call run_setup() to setup initial
 * arguments.  The argument to run_setup will be parsed into whitespace 
 * separated words and added to the global run_argv list.
 * 
 * Then, optionally call run_arg() for each additional argument that you'd like
 * to pass to the executed program.
 * 
 * Finally, call run_exec() to execute the program with the specified arguments.
 * The execvp() syscall will be used, so that the PATH is searched correctly.
 * File redirections can be performed in the call to run_exec().
 */
static char **run_argv;
static int run_argc;
static int run_argc_allocated;

/* VARARGS */
void 
run_setup (const char *prog)
{
    int i;
    char *run_prog;
    char *buf, *d, *s;
    size_t length;
    size_t doff;
    char inquotes;
    int dolastarg;

    /* clean out any malloc'ed values from run_argv */
    for (i = 0; i < run_argc; i++)
    {
	if (run_argv[i])
	{
	    free (run_argv[i]);
	    run_argv[i] = NULL;
	}
    }
    run_argc = 0;

    run_prog = xstrdup (prog);

    s = run_prog;
    d = buf = NULL;
    length = 0;
    dolastarg = 1;
    inquotes = '\0';
    doff = d - buf;
    expand_string(&buf, &length, doff + 1);
    d = buf + doff;
    while ((*d = *s++) != '\0')
    {
	switch (*d)
	{
	    case '\\':
		if (*s) *d = *s++;
		d++;
		break;
	    case '"':
	    case '\'':
		if (inquotes == *d) inquotes = '\0';
		else inquotes = *d;
		break;
	    case ' ':
	    case '\t':
		if (inquotes) d++;
		else
		{
		    *d = '\0';
		    run_add_arg (buf);
		    d = buf;
		    while (isspace(*s)) s++;
		    if (!*s) dolastarg = 0;
		}
		break;
	    default:
		d++;
		break;
	}
	doff = d - buf;
	expand_string(&buf, &length, doff + 1);
	d = buf + doff;
    }
    if (dolastarg) run_add_arg (buf);
    /* put each word into run_argv, allocating it as we go */
    if (buf) free (buf);
    free (run_prog);
}



void
run_add_arg (const char *s)
{
    /* allocate more argv entries if we've run out */
    if (run_argc >= run_argc_allocated)
    {
	run_argc_allocated += 50;
	run_argv = xnrealloc (run_argv, run_argc_allocated, sizeof (char **));
    }

    if (s)
	run_argv[run_argc++] = xstrdup (s);
    else
	run_argv[run_argc] = NULL;	/* not post-incremented on purpose! */
}



int
run_exec (const char *stin, const char *stout, const char *sterr, int flags)
{
    int shin, shout, sherr;
    int mode_out, mode_err;
    int status;
    int rc = -1;
    int rerrno = 0;
    int pid, w;

#ifdef POSIX_SIGNALS
    sigset_t sigset_mask, sigset_omask;
    struct sigaction act, iact, qact;

#else
#ifdef BSD_SIGNALS
    int mask;
    struct sigvec vec, ivec, qvec;

#else
    RETSIGTYPE (*istat) (), (*qstat) ();
#endif
#endif

    if (trace)
    {
	cvs_outerr (
#ifdef SERVER_SUPPORT
		    server_active ? "S" :
#endif
		    " ", 1);
	cvs_outerr (" -> system (", 0);
	run_print (stderr);
	cvs_outerr (")\n", 0);
    }
    if (noexec && (flags & RUN_REALLY) == 0)
	return 0;

    /* make sure that we are null terminated, since we didn't calloc */
    run_add_arg (NULL);

    /* setup default file descriptor numbers */
    shin = 0;
    shout = 1;
    sherr = 2;

    /* set the file modes for stdout and stderr */
    mode_out = mode_err = O_WRONLY | O_CREAT;
    mode_out |= ((flags & RUN_STDOUT_APPEND) ? O_APPEND : O_TRUNC);
    mode_err |= ((flags & RUN_STDERR_APPEND) ? O_APPEND : O_TRUNC);

    if (stin && (shin = open (stin, O_RDONLY)) == -1)
    {
	rerrno = errno;
	error (0, errno, "cannot open %s for reading (prog %s)",
	       stin, run_argv[0]);
	goto out0;
    }
    if (stout && (shout = open (stout, mode_out, 0666)) == -1)
    {
	rerrno = errno;
	error (0, errno, "cannot open %s for writing (prog %s)",
	       stout, run_argv[0]);
	goto out1;
    }
    if (sterr && (flags & RUN_COMBINED) == 0)
    {
	if ((sherr = open (sterr, mode_err, 0666)) == -1)
	{
	    rerrno = errno;
	    error (0, errno, "cannot open %s for writing (prog %s)",
		   sterr, run_argv[0]);
	    goto out2;
	}
    }

    /* Make sure we don't flush this twice, once in the subprocess.  */
    cvs_flushout();
    cvs_flusherr();

    /* The output files, if any, are now created.  Do the fork and dups.

       We use vfork not so much for a performance boost (the
       performance boost, if any, is modest on most modern unices),
       but for the sake of systems without a memory management unit,
       which find it difficult or impossible to implement fork at all
       (e.g. Amiga).  The other solution is spawn (see
       windows-NT/run.c).  */

#ifdef HAVE_VFORK
    pid = vfork ();
#else
    pid = fork ();
#endif
    if (pid == 0)
    {
	if (shin != 0)
	{
	    (void) dup2 (shin, 0);
	    (void) close (shin);
	}
	if (shout != 1)
	{
	    (void) dup2 (shout, 1);
	    (void) close (shout);
	}
	if (flags & RUN_COMBINED)
	    (void) dup2 (1, 2);
	else if (sherr != 2)
	{
	    (void) dup2 (sherr, 2);
	    (void) close (sherr);
	}

#ifdef SETXID_SUPPORT
	/*
	** This prevents a user from creating a privileged shell
	** from the text editor when the SETXID_SUPPORT option is selected.
	*/
	if (!strcmp (run_argv[0], Editor) && setegid (getgid ()))
	{
	    error (0, errno, "cannot set egid to gid");
	    _exit (127);
	}
#endif

	/* dup'ing is done.  try to run it now */
	(void) execvp (run_argv[0], run_argv);
	error (0, errno, "cannot exec %s", run_argv[0]);
	_exit (127);
    }
    else if (pid == -1)
    {
	rerrno = errno;
	goto out;
    }

    /* the parent.  Ignore some signals for now */
#ifdef POSIX_SIGNALS
    if (flags & RUN_SIGIGNORE)
    {
	act.sa_handler = SIG_IGN;
	(void) sigemptyset (&act.sa_mask);
	act.sa_flags = 0;
	(void) sigaction (SIGINT, &act, &iact);
	(void) sigaction (SIGQUIT, &act, &qact);
    }
    else
    {
	(void) sigemptyset (&sigset_mask);
	(void) sigaddset (&sigset_mask, SIGINT);
	(void) sigaddset (&sigset_mask, SIGQUIT);
	(void) sigprocmask (SIG_SETMASK, &sigset_mask, &sigset_omask);
    }
#else
#ifdef BSD_SIGNALS
    if (flags & RUN_SIGIGNORE)
    {
	memset (&vec, 0, sizeof vec);
	vec.sv_handler = SIG_IGN;
	(void) sigvec (SIGINT, &vec, &ivec);
	(void) sigvec (SIGQUIT, &vec, &qvec);
    }
    else
	mask = sigblock (sigmask (SIGINT) | sigmask (SIGQUIT));
#else
    istat = signal (SIGINT, SIG_IGN);
    qstat = signal (SIGQUIT, SIG_IGN);
#endif
#endif

    /* wait for our process to die and munge return status */
#ifdef POSIX_SIGNALS
    while ((w = waitpid (pid, &status, 0)) == -1 && errno == EINTR)
	;
#else
    while ((w = wait (&status)) != pid)
    {
	if (w == -1 && errno != EINTR)
	    break;
    }
#endif

    if (w == -1)
    {
	rc = -1;
	rerrno = errno;
    }
#ifndef VMS /* status is return status */
    else if (WIFEXITED (status))
	rc = WEXITSTATUS (status);
    else if (WIFSIGNALED (status))
    {
	if (WTERMSIG (status) == SIGPIPE)
	    error (1, 0, "broken pipe");
	rc = 2;
    }
    else
	rc = 1;
#else /* VMS */
    rc = WEXITSTATUS (status);
#endif /* VMS */

    /* restore the signals */
#ifdef POSIX_SIGNALS
    if (flags & RUN_SIGIGNORE)
    {
	(void) sigaction (SIGINT, &iact, NULL);
	(void) sigaction (SIGQUIT, &qact, NULL);
    }
    else
	(void) sigprocmask (SIG_SETMASK, &sigset_omask, NULL);
#else
#ifdef BSD_SIGNALS
    if (flags & RUN_SIGIGNORE)
    {
	(void) sigvec (SIGINT, &ivec, NULL);
	(void) sigvec (SIGQUIT, &qvec, NULL);
    }
    else
	(void) sigsetmask (mask);
#else
    (void) signal (SIGINT, istat);
    (void) signal (SIGQUIT, qstat);
#endif
#endif

    /* cleanup the open file descriptors */
  out:
    if (sterr)
	(void) close (sherr);
    else
	/* ensure things are received by the parent in the correct order
	 * relative to the protocol pipe
	 */
	cvs_flusherr();
  out2:
    if (stout)
	(void) close (shout);
    else
	/* ensure things are received by the parent in the correct order
	 * relative to the protocol pipe
	 */
	cvs_flushout();
  out1:
    if (stin)
	(void) close (shin);

  out0:
    if (rerrno)
	errno = rerrno;
    return rc;
}



void
run_print (FILE *fp)
{
    int i;
    void (*outfn) (const char *, size_t);

    if (fp == stderr)
	outfn = cvs_outerr;
    else if (fp == stdout)
	outfn = cvs_output;
    else
    {
	error (1, 0, "internal error: bad argument to run_print");
	/* Solely to placate gcc -Wall.
	   FIXME: it'd be better to use a function named `fatal' that
	   is known never to return.  Then kludges wouldn't be necessary.  */
	outfn = NULL;
    }

    for (i = 0; i < run_argc; i++)
    {
	(*outfn) ("'", 1);
	(*outfn) (run_argv[i], 0);
	(*outfn) ("'", 1);
	if (i != run_argc - 1)
	    (*outfn) (" ", 1);
    }
}



/* Return value is NULL for error, or if noexec was set.  If there was an
   error, return NULL and I'm not sure whether errno was set (the Red Hat
   Linux 4.1 popen manpage was kind of vague but discouraging; and the noexec
   case complicates this even aside from popen behavior).  */
FILE *
run_popen (const char *cmd, const char *mode)
{
    TRACE (TRACE_FUNCTION, "run_popen (%s,%s)", cmd, mode);
    if (noexec)
	return NULL;

    return popen (cmd, mode);
}



int
piped_child (char *const *command, int *tofdp, int *fromfdp)
{
    int pid;
    int to_child_pipe[2];
    int from_child_pipe[2];

    if (pipe (to_child_pipe) < 0)
	error (1, errno, "cannot create pipe");
    if (pipe (from_child_pipe) < 0)
	error (1, errno, "cannot create pipe");

#ifdef USE_SETMODE_BINARY
    setmode (to_child_pipe[0], O_BINARY);
    setmode (to_child_pipe[1], O_BINARY);
    setmode (from_child_pipe[0], O_BINARY);
    setmode (from_child_pipe[1], O_BINARY);
#endif

#ifdef HAVE_VFORK
    pid = vfork ();
#else
    pid = fork ();
#endif
    if (pid < 0)
	error (1, errno, "cannot fork");
    if (pid == 0)
    {
	if (dup2 (to_child_pipe[0], STDIN_FILENO) < 0)
	    error (1, errno, "cannot dup2 pipe");
	if (close (to_child_pipe[1]) < 0)
	    error (1, errno, "cannot close pipe");
	if (close (from_child_pipe[0]) < 0)
	    error (1, errno, "cannot close pipe");
	if (dup2 (from_child_pipe[1], STDOUT_FILENO) < 0)
	    error (1, errno, "cannot dup2 pipe");

	/* Okay to cast out const below - execvp don't return anyhow.  */
	execvp ((char *)command[0], (char **)command);
	error (1, errno, "cannot exec %s", command[0]);
    }
    if (close (to_child_pipe[0]) < 0)
	error (1, errno, "cannot close pipe");
    if (close (from_child_pipe[1]) < 0)
	error (1, errno, "cannot close pipe");

    *tofdp = to_child_pipe[1];
    *fromfdp = from_child_pipe[0];
    return pid;
}



int
run_piped (int *tofdp, int *fromfdp)
{
    run_add_arg (NULL);
    return piped_child (run_argv, tofdp, fromfdp);
}



void
close_on_exec (int fd)
{
#ifdef F_SETFD
    if (fcntl (fd, F_SETFD, 1) == -1)
	error (1, errno, "can't set close-on-exec flag on %d", fd);
#endif
}
