/*
 * Copyright (c)2004 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of the DragonFly Project nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * commands.c
 * Execute a queued series of commands, updating a DFUI progress bar
 * as each is executed.
 * $Id: commands.c,v 1.27 2005/03/12 04:32:14 cpressey Exp $
 */

#include <sys/time.h>
#include <sys/types.h>

#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libaura/mem.h"
#include "libaura/buffer.h"
#include "libaura/popen.h"

#include "libdfui/dfui.h"

#define	NEEDS_COMMANDS_STRUCTURE_DEFINITIONS
#include "commands.h"
#undef	NEEDS_COMMANDS_STRUCTURE_DEFINITIONS

#include "diskutil.h"
#include "functions.h"
#include "uiutil.h"

/*
 * Create a new queue of commands.
 */
struct commands	*
commands_new(void)
{
	struct commands *cmds;

	AURA_MALLOC(cmds, commands);

	cmds->head = NULL;
	cmds->tail = NULL;

	return(cmds);
}

/*
 * Add a new, empty command to an existing queue of commands.
 */
static struct command *
command_new(struct commands *cmds)
{
	struct command *cmd;

	AURA_MALLOC(cmd, command);

	cmd->cmdline = NULL;
	cmd->desc = NULL;
	cmd->log_mode = COMMAND_LOG_VERBOSE;
	cmd->failure_mode = COMMAND_FAILURE_ABORT;
	cmd->tag = NULL;
	cmd->result = COMMAND_RESULT_NEVER_EXECUTED;
	cmd->output = NULL;

	cmd->next = NULL;
	if (cmds->head == NULL)
		cmds->head = cmd;
	else
		cmds->tail->next = cmd;

	cmd->prev = cmds->tail;
	cmds->tail = cmd;

	return(cmd);
}

/*
 * Add a new shell command to an existing queue of commands.
 * The command can be specified as a format string followed by
 * any number of arguments, in the style of "sprintf".
 */
struct command *
command_add(struct commands *cmds, const char *fmt, ...)
{
	va_list args;
	struct command *cmd;

	cmd = command_new(cmds);

	va_start(args, fmt);
	vasprintf(&cmd->cmdline, fmt, args);
	va_end(args);

	return(cmd);
}

/*
 * Set the log mode of the given command.
 * Valid log modes are:
 *   COMMAND_LOG_SILENT - do not log anything at all
 *   COMMAND_LOG_QUIET - only log command name and exit code, not output
 *   COMMAND_LOG_VERBOSE - log everything
 */
void
command_set_log_mode(struct command *cmd, int log_mode)
{
	cmd->log_mode = log_mode;
}

/*
 * Set the failure mode of the given command.
 * Valid failure modes are:
 *   COMMAND_FAILURE_IGNORE - ignore failures and carry on
 *   COMMAND_FAILURE_WARN - issue a non-critical warning
 *   COMMAND_FAILURE_ABORT - halt the command chain and ask the user
 */
void
command_set_failure_mode(struct command *cmd, int failure_mode)
{
	cmd->failure_mode = failure_mode;
}

/*
 * Set the description of the given command.  If present, it will
 * be displayed in the progress bar instead of the command line.
 */
void
command_set_desc(struct command *cmd, const char *fmt, ...)
{
	va_list args;

	if (cmd->desc != NULL)
		free(cmd->desc);

	va_start(args, fmt);
	vasprintf(&cmd->desc, fmt, args);
	va_end(args);
}

/*
 * Set an arbitrary tag on the command.
 */
void
command_set_tag(struct command *cmd, const char *fmt, ...)
{
	va_list args;

	if (cmd->tag != NULL)
		free(cmd->tag);

	va_start(args, fmt);
	vasprintf(&cmd->tag, fmt, args);
	va_end(args);
}

struct command *
command_get_first(const struct commands *cmds)
{
	return(cmds->head);
}

struct command *
command_get_next(const struct command *cmd)
{
	return(cmd->next);
}

char *
command_get_cmdline(const struct command *cmd)
{
	return(cmd->cmdline);
}

char *
command_get_tag(const struct command *cmd)
{
	return(cmd->tag);
}

int
command_get_result(const struct command *cmd)
{
	return(cmd->result);
}

/*
 * Allow the user to view the command log.
 */
void
view_command_log(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;
	struct aura_buffer *error_log;

	error_log = aura_buffer_new(1024);
	aura_buffer_cat_file(error_log, "%sinstall.log", a->tmp);

	f = dfui_form_create(
	    "error_log",
	    "Error Log",
	    aura_buffer_buf(error_log),
	    "",

	    "p",	"role", "informative",
	    "p",	"minimum_width", "72",
	    "p",	"monospaced", "true",

	    "a",	"ok", "OK", "", "",
	    NULL);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	dfui_form_free(f);
	dfui_response_free(r);

	aura_buffer_free(error_log);
}

/*
 * Preview a set of commands.
 */
void
commands_preview(struct dfui_connection *c, const struct commands *cmds)
{
	struct command *cmd;
	struct aura_buffer *preview;

	preview = aura_buffer_new(1024);

	for (cmd = cmds->head; cmd != NULL; cmd = cmd->next) {
		aura_buffer_cat(preview, cmd->cmdline);
		aura_buffer_cat(preview, "\n");
	}

	inform(c, "%s", aura_buffer_buf(preview));

	aura_buffer_free(preview);
}

/*
 * The command chain executing engine proper follows.
 */

/*
 * Read from the pipe that was opened to the executing commands
 * and update the progress bar as data comes and (and/or as the
 * read from the pipe times out.)
 */
static int
pipe_loop(struct i_fn_args *a, struct dfui_progress *pr,
	  struct command *cmd, int *cancelled)
{
	FILE *cmdout = NULL;
	struct timeval tv = { 1, 0 };
	char cline[256];
	char *command;
	pid_t pid;
	fd_set r;
	int n;

	asprintf(&command, "(%s) 2>&1 </dev/null", cmd->cmdline);
	fflush(stdout);
	cmdout = aura_popen("%s", command, "r");
	free(command);

	if (cmdout == NULL) {
		i_log(a, "! could not aura_popen() command");
		return(COMMAND_RESULT_POPEN_ERR);
	}
	pid = aura_pgetpid(cmdout);
#ifdef DEBUG
	fprintf(stderr, "+ pid = %d\n", pid);
#endif

	/*
	 * Loop, selecting on the command and a timeout.
	 */
	for (;;) {
		if (*cancelled)
			break;
		FD_ZERO(&r);
		FD_SET(fileno(cmdout), &r);
		n = select(fileno(cmdout) + 1, &r, NULL, NULL, &tv);
#ifdef DEBUG
		fprintf(stderr, "+ select() = %d\n", n);
#endif
		if (n < 0) {
			/* Error */
			i_log(a, "! select() failed\n");
			aura_pclose(cmdout);
			return(COMMAND_RESULT_SELECT_ERR);
		} else if (n == 0) {
			/* Timeout */
			if (!dfui_be_progress_update(a->c, pr, cancelled))
				abort_backend();
#ifdef DEBUG
			fprintf(stderr, "+ cancelled = %d\n", *cancelled);
#endif
		} else {
			/* Data came in */
			fgets(cline, 255, cmdout);
			while (strlen(cline) > 0 && cline[strlen(cline) - 1] == '\n')
				cline[strlen(cline) - 1] = '\0';
			if (feof(cmdout))
				break;
			if (!dfui_be_progress_update(a->c, pr, cancelled))
				abort_backend();
			if (cmd->log_mode == COMMAND_LOG_VERBOSE) {
				i_log(a, "| %s", cline);
			} else if (cmd->log_mode != COMMAND_LOG_SILENT) {
				fprintf(stderr, "| %s\n", cline);
			}
		}
	}

	if (*cancelled) {
#ifdef DEBUG
		fprintf(stderr, "+ killing %d\n", pid);
#endif
		n = kill(pid, SIGTERM);
#ifdef DEBUG
		fprintf(stderr, "+ kill() = %d\n", n);
#endif
	}

#ifdef DEBUG
	fprintf(stderr, "+ pclosing %d\n", fileno(cmdout));
#endif
	n = aura_pclose(cmdout) / 256;
#ifdef DEBUG
	fprintf(stderr, "+ pclose() = %d\n", n);
#endif
	return(n);
}

/*
 * Execute a single command.
 * Return value is a COMMAND_RESULT_* constant, or
 * a value from 0 to 255 to indicate the exit code
 * from the utility.
 */
static int
command_execute(struct i_fn_args *a, struct dfui_progress *pr,
		struct command *cmd)
{
	FILE *log = NULL;
	char *filename;
	int cancelled = 0, done = 0, report_done = 0;

	if (cmd->desc != NULL)
		dfui_info_set_short_desc(dfui_progress_get_info(pr), cmd->desc);
	else
		dfui_info_set_short_desc(dfui_progress_get_info(pr), cmd->cmdline);

	if (!dfui_be_progress_update(a->c, pr, &cancelled))
		  abort_backend();

	while (!done) {
		asprintf(&filename, "%sinstall.log", a->tmp);
		log = fopen(filename, "a");
		free(filename);

		if (cmd->log_mode != COMMAND_LOG_SILENT)
			i_log(a, ",-<<< Executing `%s'", cmd->cmdline);
		cmd->result = pipe_loop(a, pr, cmd, &cancelled);
		if (cmd->log_mode != COMMAND_LOG_SILENT)
			i_log(a, "`->>> Exit status: %d\n", cmd->result);

		if (log != NULL)
			fclose(log);

		if (cancelled) {
			if (!dfui_be_progress_end(a->c))
				abort_backend();

			report_done = 0;
			while (!report_done) {
				switch (dfui_be_present_dialog(a->c, "Cancelled",
				    "View Log|Retry|Cancel|Skip",
				    "Execution of the command\n\n%s\n\n"
				    "was cancelled.",
				    cmd->cmdline)) {
				case 1:
					/* View Log */
					view_command_log(a);
					break;
				case 2:
					/* Retry */
					cancelled = 0;
					report_done = 1;
					break;
				case 3:
					/* Cancel */
					cmd->result = COMMAND_RESULT_CANCELLED;
					report_done = 1;
					done = 1;
					break;
				case 4:
					/* Skip */
					cmd->result = COMMAND_RESULT_SKIPPED;
					report_done = 1;
					done = 1;
					break;
				}
			}

			if (!dfui_be_progress_begin(a->c, pr))
				abort_backend();

		} else if (cmd->failure_mode == COMMAND_FAILURE_IGNORE) {
			cmd->result = 0;
			done = 1;
		} else if (cmd->result != 0 && cmd->failure_mode != COMMAND_FAILURE_WARN) {
			if (!dfui_be_progress_end(a->c))
				abort_backend();

			report_done = 0;
			while (!report_done) {
				switch (dfui_be_present_dialog(a->c, "Command Failed!",
				    "View Log|Retry|Cancel|Skip",
				    "Execution of the command\n\n%s\n\n"
				    "FAILED with a return code of %d.",
				    cmd->cmdline, cmd->result)) {
				case 1:
					/* View Log */
					view_command_log(a);
					break;
				case 2:
					/* Retry */
					report_done = 1;
					break;
				case 3:
					/* Cancel */
					/* XXX need a better way to retain actual result */
					cmd->result = COMMAND_RESULT_CANCELLED;
					report_done = 1;
					done = 1;
					break;
				case 4:
					/* Skip */
					/* XXX need a better way to retain actual result */
					cmd->result = COMMAND_RESULT_SKIPPED;
					report_done = 1;
					done = 1;
					break;
				}
			}

			if (!dfui_be_progress_begin(a->c, pr))
				abort_backend();

		} else {
			done = 1;
		}
	}

	return(cmd->result);
}

/*
 * Execute a series of external utility programs.
 * Returns 1 if everything executed OK, 0 if one of the
 * critical commands failed or if the user cancelled.
 */
int
commands_execute(struct i_fn_args *a, struct commands *cmds)
{
	struct dfui_progress *pr;
	struct command *cmd;
	int i;
	int n = 0;
	int result = 0;
	int return_val = 1;

	cmd = cmds->head;
	while (cmd != NULL) {
		n++;
		cmd = cmd->next;
	}

	pr = dfui_progress_new(dfui_info_new(
	    "Executing Commands",
	    "Executing Commands",
	    ""),
	    0);

	if (!dfui_be_progress_begin(a->c, pr))
		abort_backend();

	i = 1;
	for (cmd = cmds->head; cmd != NULL; cmd = cmd->next, i++) {
		result = command_execute(a, pr, cmd);
		if (result == COMMAND_RESULT_CANCELLED) {
			return_val = 0;
			break;
		}
		if (result > 0 && result < 256) {
			return_val = 0;
			if (cmd->failure_mode == COMMAND_FAILURE_ABORT) {
				break;
			}
		}
		dfui_progress_set_amount(pr, (i * 100) / n);
	}

	if (!dfui_be_progress_end(a->c))
		abort_backend();

	dfui_progress_free(pr);

	return(return_val);
}

/*
 * Free the memory allocated for a queue of commands.  This invalidates
 * the pointer passed to it.
 */
void
commands_free(struct commands *cmds)
{
	struct command *cmd, *next;

	cmd = cmds->head;
	while (cmd != NULL) {
		next = cmd->next;
		if (cmd->cmdline != NULL)
			free(cmd->cmdline);
		if (cmd->desc != NULL)
			free(cmd->desc);
		if (cmd->tag != NULL)
			free(cmd->tag);
		AURA_FREE(cmd, command);
		cmd = next;
	}
	AURA_FREE(cmds, commands);
}
