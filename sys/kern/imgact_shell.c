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
 * $DragonFly: src/sys/kern/imgact_shell.c,v 1.6 2005/02/28 05:44:52 dillon Exp $
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
	size_t length, offset;
	int error;

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
	 * Figure out the number of bytes that need to be reserved in the
	 * argument string to copy the contents of the interpreter's command
	 * line into the argument string.
	 */
	ihp = &image_header[2];
	offset = 0;
	while (ihp < &image_header[PAGE_SIZE]) {
		/* Skip any whitespace */
		if (*ihp == ' ' || *ihp == '\t') {
			++ihp;
			continue;
		}

		/* End of line? */
		if (*ihp == '\n' || *ihp == '#' || *ihp == '\0')
			break;

		/* Found a token */
		do {
			++offset;
			++ihp;
		} while (ihp < &image_header[PAGE_SIZE] &&
			 *ihp != ' ' && *ihp != '\t' && 
			 *ihp != '\n' && *ihp != '#' && *ihp != '\0');

		/* Take into account the \0 that will terminate the token */
		++offset;
	}

	/* If the script gives a null line as the interpreter, we bail */
	if (offset == 0)
		return (ENOEXEC);

	/* It should not be possible for offset to exceed PAGE_SIZE */
	KKASSERT(offset <= PAGE_SIZE);

	/* Check that we aren't too big */
	if (ihp == &image_header[PAGE_SIZE])
		return (ENAMETOOLONG);

	/*
	 * The full path name of the original script file must be tagged
	 * onto the end, adjust the offset to deal with it.  
	 *
	 * The original argv0 is being replaced, set 'length' to the number
	 * of bytes being removed.  So 'offset' is the number of bytes being
	 * added and 'length' is the number of bytes being removed.
	 */
	offset += strlen(imgp->args->fname) + 1;	/* add fname */
	length = strlen(imgp->args->begin_argv) + 1;	/* bytes to delete */

	if (offset > imgp->args->space + length)
		return (E2BIG);

	bcopy(imgp->args->begin_argv + length, imgp->args->begin_argv + offset,
		imgp->args->endp - (imgp->args->begin_argv + length));

	offset -= length;		/* calculate actual adjustment */
	imgp->args->begin_envv += offset;
	imgp->args->endp += offset;
	imgp->args->space -= offset;
	/* decr argc remove old argv[0], incr argc for fname add, net 0 */

	/*
	 * Loop through the interpreter name yet again, copying as
	 * we go.
	 */
	ihp = &image_header[2];
	offset = 0;
	while (ihp < &image_header[PAGE_SIZE]) {
		/* Skip whitespace */
		if (*ihp == ' ' || *ihp == '\t') {
			++ihp;
			continue;
		}

		/* End of line? */
		if (*ihp == '\n' || *ihp == '#' || *ihp == '\0')
			break;

		/* Found a token, copy it */
		do {
			imgp->args->begin_argv[offset] = *ihp;
			++ihp;
			++offset;
		} while (ihp < &image_header[PAGE_SIZE] &&
			 *ihp != ' ' && *ihp != '\t' && 
			 *ihp != '\n' && *ihp != '#' && *ihp != '\0');

		/* And terminate the argument */
		imgp->args->begin_argv[offset] = '\0';
		imgp->args->argc++;
		++offset;
	}

	/*
	 * Finally, add the filename onto the end for the interpreter to
	 * use and copy the interpreter's name to imgp->interpreter_name
	 * for exec to use.
	 */
	error = copystr(imgp->args->fname, imgp->args->buf + offset,
			imgp->args->space, &length);

	if (error == 0) {
		error = copystr(imgp->args->begin_argv, imgp->interpreter_name,
				MAXSHELLCMDLEN, &length);
	}
	return (error);
}

/*
 * Tell kern_execve.c about it, with a little help from the linker.
 */
static struct execsw shell_execsw = { exec_shell_imgact, "#!" };
EXEC_SET(shell, shell_execsw);
