/*
 * Copyright (c) 1993, David Greenman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/imgact_shell.c,v 1.21.2.2 2001/12/22 01:21:39 jwd Exp $
 * $DragonFly: src/sys/kern/imgact_shell.c,v 1.3 2003/11/12 01:00:33 daver Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/kernel.h>

#if BYTE_ORDER == LITTLE_ENDIAN
#define SHELLMAGIC	0x2123 /* #! */
#else
#define SHELLMAGIC	0x2321
#endif

/*
 * Shell interpreter image activator. A interpreter name beginning
 *	at imgp->args->begin_argv is the minimal successful exit requirement.
 */
int
exec_shell_imgact(struct image_params *imgp)
{
	const char *image_header = imgp->image_header;
	const char *ihp;
	int error, length, offset;

	/* a shell script? */
	if (((const short *) image_header)[0] != SHELLMAGIC)
		return(-1);

	/*
	 * Don't allow a shell script to be the shell for a shell
	 *	script. :-)
	 */
	if (imgp->interpreted)
		return(ENOEXEC);

	imgp->interpreted = 1;

	/*
	 * We must determine how far to offset the contents of the buffer
	 * to make room for the interpreter + args and the full path of
	 * the interpreted file while overwriting the first argument
	 * currently in the buffer.
	 */
	ihp = &image_header[2];
	offset = 0;
	while (ihp < &image_header[MAXSHELLCMDLEN]) {
		/* Skip any whitespace */
		while ((*ihp == ' ') || (*ihp == '\t'))
			ihp++;

		/* End of line? */
		if ((*ihp == '\n') || (*ihp == '#'))
			break;

		/* Found a token */
		while ((*ihp != ' ') && (*ihp != '\t') && (*ihp != '\n') &&
		    (*ihp != '#')) {
			offset++;
			ihp++;
		}
		/* Include terminating nulls in the offset */
		offset++;
	}

	/* If the script gives a null line as the interpreter, we bail */
	if (offset == 0)
		return (ENOEXEC);

	/* Check that we aren't too big */
	if (offset > MAXSHELLCMDLEN)
		return (ENAMETOOLONG);

	/* The file name is used to replace argv[0] */
	offset += strlen(imgp->args->fname) + 1;
	offset -= strlen(imgp->args->begin_argv) + 1;

	if (offset > imgp->args->space)
		return (E2BIG);

	/* Move the contents of imgp->args->buf by offset bytes. */
	bcopy(imgp->args->buf, imgp->args->buf + offset,
	    ARG_MAX - imgp->args->space);

	/*
	 * Loop through the interpreter name yet again, copying as
	 * we go.
	 */
	ihp = &image_header[2];
	offset = 0;
	while (ihp < &image_header[MAXSHELLCMDLEN]) {
		/* Skip whitespace */
		while ((*ihp == ' ' || *ihp == '\t'))
			ihp++;

		/* End of line? */
		if ((*ihp == '\n') || (*ihp == '#'))
			break;

		/* Found a token, copy it */
		while ((*ihp != ' ') && (*ihp != '\t') && (*ihp != '\n') &&
		   (*ihp != '#'))
			imgp->args->buf[offset++] = *ihp++;

		imgp->args->buf[offset++] = '\0';
		imgp->args->argc++;
	}

	error = copystr(imgp->args->fname, imgp->args->buf + offset,
	    imgp->args->space, &length);

	if (error == 0)
		error = copystr(imgp->args->begin_argv,
		    imgp->interpreter_name, MAXSHELLCMDLEN, &length);

	return (error);
}

/*
 * Tell kern_execve.c about it, with a little help from the linker.
 */
static struct execsw shell_execsw = { exec_shell_imgact, "#!" };
EXEC_SET(shell, shell_execsw);
