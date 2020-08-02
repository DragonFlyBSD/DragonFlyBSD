/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Aaron LI <aly@aaronly.me>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#)calendar.c  8.3 (Berkeley) 3/25/94
 * $FreeBSD: head/usr.bin/calendar/io.c 327117 2017-12-23 21:04:32Z eadler $
 */

#include <sys/param.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <langinfo.h>
#include <locale.h>
#include <paths.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "calendar.h"
#include "basics.h"
#include "dates.h"
#include "days.h"
#include "gregorian.h"
#include "io.h"
#include "nnames.h"
#include "parsedata.h"
#include "utils.h"


enum { C_NONE, C_LINE, C_BLOCK };
enum { T_NONE, T_TOKEN, T_VARIABLE, T_DATE };

struct cal_entry {
	int   type;		/* type of the read entry */
	char *token;		/* token to process (T_TOKEN) */
	char *variable;		/* variable name (T_VARIABLE) */
	char *value;		/* variable value (T_VARIABLE) */
	char *date;		/* event date (T_DATE) */
	struct cal_desc *description;  /* event description (T_DATE) */
};

struct cal_file {
	FILE	*fp;
	char	*line;		/* line string read from file */
	size_t	 line_cap;	/* capacity of the 'line' buffer */
	char	*nextline;	/* to store the rewinded line */
	size_t	 nextline_cap;	/* capacity of the 'nextline' buffer */
	bool	 rewinded;	/* if 'nextline' has the rewinded line */
};

static struct cal_desc *descriptions = NULL;
static struct node *definitions = NULL;

static FILE	*cal_fopen(const char *file);
static bool	 cal_parse(FILE *in);
static bool	 process_token(char *line, bool *skip);
static void	 send_mail(FILE *fp);
static char	*skip_comment(char *line, int *comment);
static void	 write_mailheader(FILE *fp);

static bool	 cal_readentry(struct cal_file *cfile,
			       struct cal_entry *entry, bool skip);
static char	*cal_readline(struct cal_file *cfile);
static void	 cal_rewindline(struct cal_file *cfile);
static bool	 is_date_entry(char *line, char **content);
static bool	 is_variable_entry(char *line, char **value);

static struct cal_desc *cal_desc_new(struct cal_desc **head);
static void	 cal_desc_freeall(struct cal_desc *head);
static void	 cal_desc_addline(struct cal_desc *desc, const char *line);

/*
 * XXX: Quoted or escaped comment marks are not supported yet.
 */
static char *
skip_comment(char *line, int *comment)
{
	char *p, *pp;

	if (*comment == C_LINE) {
		*line = '\0';
		*comment = C_NONE;
		return line;
	} else if (*comment == C_BLOCK) {
		for (p = line, pp = p + 1; *p; p++, pp = p + 1) {
			if (*p == '*' && *pp == '/') {
				*comment = C_NONE;
				return p + 2;
			}
		}
		*line = '\0';
		return line;
	} else {
		*comment = C_NONE;
		for (p = line, pp = p + 1; *p; p++, pp = p + 1) {
			if (*p == '/' && (*pp == '/' || *pp == '*')) {
				*comment = (*pp == '/') ? C_LINE : C_BLOCK;
				break;
			}
		}
		if (*comment != C_NONE) {
			pp = skip_comment(p, comment);
			if (pp > p)
				memmove(p, pp, strlen(pp) + 1);
		}
		return line;
	}

	return line;
}


static FILE *
cal_fopen(const char *file)
{
	FILE *fp = NULL;
	char fpath[MAXPATHLEN];

	for (size_t i = 0; calendarDirs[i] != NULL; i++) {
		snprintf(fpath, sizeof(fpath), "%s/%s",
			 calendarDirs[i], file);
		if ((fp = fopen(fpath, "r")) != NULL)
			return (fp);
	}

	warnx("Cannot open calendar file: '%s'", file);
	return (NULL);
}

/*
 * NOTE: input 'line' should have trailing comment and whitespace trimmed.
 */
static bool
process_token(char *line, bool *skip)
{
	char *walk;

	if (strcmp(line, "#endif") == 0) {
		*skip = false;
		return true;
	}

	if (*skip)  /* deal with nested #ifndef */
		return true;

	if (string_startswith(line, "#include ") ||
	    string_startswith(line, "#include\t")) {
		walk = triml(line + sizeof("#include"));
		if (*walk == '\0') {
			warnx("Expecting arguments after #include");
			return false;
		}
		if (*walk != '<' && *walk != '\"') {
			warnx("Expecting '<' or '\"' after #include");
			return false;
		}

		char a = *walk;
		char c = walk[strlen(walk) - 1];

		switch(c) {
		case '>':
			if (a != '<') {
				warnx("Unterminated include expecting '\"'");
				return false;
			}
			break;
		case '\"':
			if (a != '\"') {
				warnx("Unterminated include expecting '>'");
				return false;
			}
			break;
		default:
			warnx("Unterminated include expecting '%c'",
			      (a == '<') ? '>' : '\"' );
			return false;
		}

		walk++;
		walk[strlen(walk) - 1] = '\0';

		FILE *fpin = cal_fopen(walk);
		if (fpin == NULL)
			return false;
		if (!cal_parse(fpin)) {
			warnx("Failed to parse calendar files");
			fclose(fpin);
			return false;
		}

		fclose(fpin);
		return true;

	} else if (string_startswith(line, "#define ") ||
	           string_startswith(line, "#define\t")) {
		walk = triml(line + sizeof("#define"));
		if (*walk == '\0') {
			warnx("Expecting arguments after #define");
			return false;
		}

		struct node *new = list_newnode(xstrdup(walk), NULL);
		definitions = list_addfront(definitions, new);

		return true;

	} else if (string_startswith(line, "#ifndef ") ||
	           string_startswith(line, "#ifndef\t")) {
		walk = triml(line + sizeof("#ifndef"));
		if (*walk == '\0') {
			warnx("Expecting arguments after #ifndef");
			return false;
		}

		if (list_lookup(definitions, walk, strcmp, NULL))
			*skip = true;

		return true;
	}

	warnx("Unknown token line: |%s|", line);
	return false;
}

static bool
locale_day_first(void)
{
	char *d_fmt = nl_langinfo(D_FMT);
	DPRINTF("%s: d_fmt=|%s|\n", __func__, d_fmt);
	/* NOTE: BSDs use '%e' in D_FMT while Linux uses '%d' */
	return (strpbrk(d_fmt, "ed") < strchr(d_fmt, 'm'));
}

static bool
cal_parse(FILE *in)
{
	struct cal_file cfile = { 0 };
	struct cal_entry entry = { 0 };
	struct cal_desc *desc;
	struct cal_line *line;
	struct cal_day *cdays[CAL_MAX_REPEAT] = { NULL };
	struct specialday *sday;
	char *extradata[CAL_MAX_REPEAT] = { NULL };
	bool d_first, skip, var_handled;
	bool locale_changed, calendar_changed;
	int flags, count;

	assert(in != NULL);
	cfile.fp = in;
	d_first = locale_day_first();
	skip = false;
	locale_changed = false;
	calendar_changed = false;

	while (cal_readentry(&cfile, &entry, skip)) {
		if (entry.type == T_TOKEN) {
			DPRINTF2("%s: T_TOKEN: |%s|\n",
				 __func__, entry.token);
			if (!process_token(entry.token, &skip)) {
				free(entry.token);
				return false;
			}

			free(entry.token);
			continue;
		}

		if (entry.type == T_VARIABLE) {
			DPRINTF2("%s: T_VARIABLE: |%s|=|%s|\n",
				 __func__, entry.variable, entry.value);
			var_handled = false;

			if (strcasecmp(entry.variable, "LANG") == 0) {
				if (setlocale(LC_ALL, entry.value) == NULL) {
					warnx("Failed to set LC_ALL='%s'",
					      entry.value);
				}
				d_first = locale_day_first();
				set_nnames();
				locale_changed = true;
				DPRINTF("%s: set LC_ALL='%s' (day_first=%s)\n",
					__func__, entry.value,
					d_first ? "true" : "false");
				var_handled = true;
			}

			if (strcasecmp(entry.variable, "CALENDAR") == 0) {
				if (!set_calendar(entry.value)) {
					warnx("Failed to set CALENDAR='%s'",
					      entry.value);
				}
				calendar_changed = true;
				DPRINTF("%s: set CALENDAR='%s'\n",
					__func__, entry.value);
				var_handled = true;
			}

			if (strcasecmp(entry.variable, "SEQUENCE") == 0) {
				set_nsequences(entry.value);
				var_handled = true;
			}

			for (size_t i = 0; specialdays[i].name; i++) {
				sday = &specialdays[i];
				if (strcasecmp(entry.variable, sday->name) == 0) {
					free(sday->n_name);
					sday->n_name = xstrdup(entry.value);
					sday->n_len = strlen(sday->n_name);
					var_handled = true;
					break;
				}
			}

			if (!var_handled) {
				warnx("Unknown variable: |%s|=|%s|",
				      entry.variable, entry.value);
			}

			free(entry.variable);
			free(entry.value);
			continue;
		}

		if (entry.type == T_DATE) {
			desc = entry.description;
			DPRINTF2("----------------\n%s: T_DATE: |%s|\n",
				 __func__, entry.date);
			for (line = desc->firstline; line; line = line->next)
				DPRINTF3("\t|%s|\n", line->str);

			count = parse_cal_date(entry.date, &flags, cdays,
					       extradata);
			if (count < 0) {
				warnx("Cannot parse date |%s| with content |%s|",
				      entry.date, desc->firstline->str);
				continue;
			} else if (count == 0) {
				DPRINTF2("Ignore out-of-range date |%s| "
					 "with content |%s|\n",
					 entry.date, desc->firstline->str);
				continue;
			}

			for (int i = 0; i < count; i++) {
				event_add(cdays[i], d_first,
				          ((flags & F_VARIABLE) != 0),
				          desc, extradata[i]);
				cdays[i] = NULL;
				extradata[i] = NULL;
			}

			free(entry.date);
			continue;
		}

		errx(1, "Invalid calendar entry type: %d", entry.type);
	}

	/*
	 * Reset to the default locale, so that one calendar file that changed
	 * the locale (by defining the "LANG" variable) does not interfere the
	 * following calendar files without the "LANG" definition.
	 */
	if (locale_changed) {
		setlocale(LC_ALL, "");
		set_nnames();
		DPRINTF("%s: reset LC_ALL\n", __func__);
	}

	if (calendar_changed) {
		set_calendar(NULL);
		DPRINTF("%s: reset CALENDAR\n", __func__);
	}

	free(cfile.line);
	free(cfile.nextline);

	return true;
}

static bool
cal_readentry(struct cal_file *cfile, struct cal_entry *entry, bool skip)
{
	char *p, *value, *content;
	int comment;

	memset(entry, 0, sizeof(*entry));
	entry->type = T_NONE;
	comment = C_NONE;

	while ((p = cal_readline(cfile)) != NULL) {
		p = skip_comment(p, &comment);
		p = trimr(p);  /* Need to keep the leading tabs */
		if (*p == '\0')
			continue;

		if (*p == '#') {
			entry->type = T_TOKEN;
			entry->token = xstrdup(p);
			return true;
		}

		if (skip) {
			/* skip entries but tokens (e.g., '#endif') */
			DPRINTF2("%s: skip line: |%s|\n", __func__, p);
			continue;
		}

		if (is_variable_entry(p, &value)) {
			value = triml(value);
			if (*value == '\0') {
				warnx("%s: varaible |%s| has no value",
				      __func__, p);
				continue;
			}

			entry->type = T_VARIABLE;
			entry->variable = xstrdup(p);
			entry->value = xstrdup(value);
			return true;
		}

		if (is_date_entry(p, &content)) {
			content = triml(content);
			if (*content == '\0') {
				warnx("%s: date |%s| has no content",
				      __func__, p);
				continue;
			}

			entry->type = T_DATE;
			entry->date = xstrdup(p);
			entry->description = cal_desc_new(&descriptions);
			cal_desc_addline(entry->description, content);

			/* Continuous description of the event */
			while ((p = cal_readline(cfile)) != NULL) {
				p = trimr(skip_comment(p, &comment));
				if (*p == '\0')
					continue;

				if (*p == '\t') {
					content = triml(p);
					cal_desc_addline(entry->description,
							 content);
				} else {
					cal_rewindline(cfile);
					break;
				}
			}

			return true;
		}

		warnx("%s: unknown line: |%s|", __func__, p);
	}

	return false;
}

static char *
cal_readline(struct cal_file *cfile)
{
	if (cfile->rewinded) {
		cfile->rewinded = false;
		return cfile->nextline;
	}

	if (getline(&cfile->line, &cfile->line_cap, cfile->fp) <= 0)
		return NULL;

	return cfile->line;
}

static void
cal_rewindline(struct cal_file *cfile)
{
	if (cfile->nextline_cap == 0)
		cfile->nextline = xmalloc(cfile->line_cap);
	else if (cfile->nextline_cap < cfile->line_cap)
		cfile->nextline = xrealloc(cfile->nextline, cfile->line_cap);

	memcpy(cfile->nextline, cfile->line, cfile->line_cap);
	cfile->nextline_cap = cfile->line_cap;
	cfile->rewinded = true;
}

static bool
is_variable_entry(char *line, char **value)
{
	char *p, *eq;

	if (line == NULL)
		return false;
	if (!(*line == '_' || isalpha((unsigned int)*line)))
		return false;
	if ((eq = strchr(line, '=')) == NULL)
		return false;
	for (p = line+1; p < eq; p++) {
		if (!isalnum((unsigned int)*p))
			return false;
	}

	*eq = '\0';
	if (value != NULL)
		*value = eq + 1;

	return true;
}

static bool
is_date_entry(char *line, char **content)
{
	char *p;

	if (*line == '\t')
		return false;
	if ((p = strchr(line, '\t')) == NULL)
		return false;

	*p = '\0';
	if (content != NULL)
		*content = p + 1;

	return true;
}


static struct cal_desc *
cal_desc_new(struct cal_desc **head)
{
	struct cal_desc *desc = xcalloc(1, sizeof(*desc));

	if (*head == NULL) {
		*head = desc;
	} else {
		desc->next = *head;
		*head = desc;
	}

	return desc;
}

static void	
cal_desc_freeall(struct cal_desc *head)
{
	struct cal_desc *desc;
	struct cal_line *line;

	while ((desc = head) != NULL) {
		head = head->next;
		while ((line = desc->firstline) != NULL) {
			desc->firstline = desc->firstline->next;
			free(line->str);
			free(line);
		}
		free(desc);
	}
}

static void	
cal_desc_addline(struct cal_desc *desc, const char *line)
{
	struct cal_line *cline;

	cline = xcalloc(1, sizeof(*cline));
	cline->str = xstrdup(line);
	if (desc->lastline != NULL) {
		desc->lastline->next = cline;
		desc->lastline = cline;
	} else {
		desc->firstline = desc->lastline = cline;
	}
}


int
cal(FILE *fpin)
{
	if (!cal_parse(fpin)) {
		warnx("Failed to parse calendar files");
		return 1;
	}

	if (Options.allmode) {
		FILE *fpout;

		/*
		 * Use a temporary output file, so we can skip sending mail
		 * if there is no output.
		 */
		if ((fpout = tmpfile()) == NULL) {
			warn("tmpfile");
			return 1;
		}
		event_print_all(fpout);
		send_mail(fpout);
	} else {
		event_print_all(stdout);
	}

	list_freeall(definitions, free, NULL);
	definitions = NULL;
	cal_desc_freeall(descriptions);
	descriptions = NULL;

	return 0;
}


static void
send_mail(FILE *fp)
{
	int ch, pdes[2];
	FILE *fpipe;

	assert(Options.allmode == true);

	if (fseek(fp, 0L, SEEK_END) == -1 || ftell(fp) == 0) {
		DPRINTF("%s: no events; skip sending mail\n", __func__);
		return;
	}
	if (pipe(pdes) < 0) {
		warnx("pipe");
		return;
	}

	switch (fork()) {
	case -1:
		close(pdes[0]);
		close(pdes[1]);
		goto done;
	case 0:
		/* child -- set stdin to pipe output */
		if (pdes[0] != STDIN_FILENO) {
			dup2(pdes[0], STDIN_FILENO);
			close(pdes[0]);
		}
		close(pdes[1]);
		execl(_PATH_SENDMAIL, "sendmail", "-i", "-t", "-F",
		      "\"Reminder Service\"", (char *)NULL);
		warn(_PATH_SENDMAIL);
		_exit(1);
	}
	/* parent -- write to pipe input */
	close(pdes[0]);

	fpipe = fdopen(pdes[1], "w");
	if (fpipe == NULL) {
		close(pdes[1]);
		goto done;
	}

	write_mailheader(fpipe);
	rewind(fp);
	while ((ch = fgetc(fp)) != EOF)
		fputc(ch, fpipe);
	fclose(fpipe);  /* will also close the underlying fd */

done:
	fclose(fp);
	while (wait(NULL) >= 0)
		;
}

static void
write_mailheader(FILE *fp)
{
	uid_t uid = getuid();
	struct passwd *pw = getpwuid(uid);
	struct date date;
	char dayname[32] = { 0 };
	int dow;

	gregorian_from_fixed(Options.today, &date);
	dow = dayofweek_from_fixed(Options.today);
	sprintf(dayname, "%s, %d %s %d",
		dow_names[dow].f_name, date.day,
		month_names[date.month-1].f_name, date.year);

	fprintf(fp,
		"From: %s (Reminder Service)\n"
		"To: %s\n"
		"Subject: %s's Calendar\n"
		"Precedence: bulk\n"
		"Auto-Submitted: auto-generated\n\n",
		pw->pw_name, pw->pw_name, dayname);
	fflush(fp);
}
