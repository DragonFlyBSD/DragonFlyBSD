/*
 * $Id: lang.c,v 1.5 2005/02/12 00:31:46 cpressey Exp $
 */

#include <err.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lang.h"

struct _langset {
	const char *name;
	const char *font8x8;
	const char *font8x14;
	const char *font8x16;
	const char *keymap;
	const char *scrnmap;
	const char *language;
	const char *charset;
	const char *term;
} langset[] = {
	{ "ru", "cp866-8x8", "cp866-8x14", "cp866-8x16", "ru.koi8-r", "koi8-r2cp866", "ru_RU.KOI8-R", "KOI8-R", "cons25r" }
};

#define langcount (sizeof(langset) / sizeof(struct _langset))

static int	get_lang_num(const char *langname);
static int	system_fmt(const char *fmt, ...);


static int
get_lang_num(const char *langname)
{
	size_t i;

	for(i =0; i < langcount; i++)
		if(strcmp(langset[i].name, langname) == 0)
			return i;

	return -1;
}

static int
system_fmt(const char *fmt, ...)
{
	char *command;
        va_list args;

        va_start(args, fmt);
        vasprintf(&command, fmt, args);
        va_end(args);

#ifdef DEBUG
	fprintf(stderr, "%s\n", command);
#endif

        return(system(command));
}

/* do this once */
int
set_lang_syscons(const char *id)
{
	int lang_num;

	lang_num = get_lang_num(id);

	if(lang_num < 0)
		return(0);

#define kbddev "/dev/ttyv0"
#define viddev "/dev/ttyv0"
#define kbdcontrol "/usr/sbin/kbdcontrol"
#define vidcontrol "/usr/sbin/vidcontrol"

	if (
	system_fmt("%s < %s -l %s", kbdcontrol, kbddev, langset[lang_num].keymap) != 0 ||
	system_fmt("%s < %s -l %s", vidcontrol, viddev, langset[lang_num].scrnmap) != 0 ||
	system_fmt("%s < %s -f 8x8 %s", vidcontrol, viddev, langset[lang_num].font8x8) != 0 ||
	system_fmt("%s < %s -f 8x14 %s", vidcontrol, viddev, langset[lang_num].font8x14) != 0 ||
	system_fmt("%s < %s -f 8x16 %s", vidcontrol, viddev, langset[lang_num].font8x16) != 0)
		return(0);

	return(1);
}

/* do this for each side (backend, frontend) */
int
set_lang_envars(const char *id)
{
	char *term;

	int lang_num;

	lang_num = get_lang_num(id);

	if(lang_num < 0)
		return(0);

	/* gettext recommended setting */
	setenv("LANGUAGE", langset[lang_num].name, 1);

	/* also should be set */
	setenv("LANG", langset[lang_num].language, 1);

	/* set this too for completeness */
	setenv("MM_CHARSET", langset[lang_num].charset, 1);

	/* TERM must be set for some encodings */
	term = getenv("TERM");
	if((strcmp(term,"cons25") == 0) &&
		langset[lang_num].term != NULL)
		setenv("TERM", langset[lang_num].term, 1);

	return(1);
}
