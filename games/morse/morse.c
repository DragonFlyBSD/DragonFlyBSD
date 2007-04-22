/*
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * @(#) Copyright (c) 1988, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)morse.c	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/games/morse/morse.c,v 1.12.2.2 2002/03/12 17:45:15 phantom Exp $
 * $DragonFly: src/games/morse/morse.c,v 1.4 2007/04/22 10:22:32 corecode Exp $
 */

/*
 * Taught to send *real* morse by Lyndon Nerenberg (VE7TCP/VE6BBM)
 * <lyndon@orthanc.com>
 */

#include <sys/time.h>
#include <sys/soundcard.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

struct morsetab {
	char            inchar;
	const char      *morse;
};

static const struct morsetab mtab[] = {

	/* letters */

	{'a', ".-"},
	{'b', "-..."},
	{'c', "-.-."},
	{'d', "-.."},
	{'e', "."},
	{'f', "..-."},
	{'g', "--."},
	{'h', "...."},
	{'i', ".."},
	{'j', ".---"},
	{'k', "-.-"},
	{'l', ".-.."},
	{'m', "--"},
	{'n', "-."},
	{'o', "---"},
	{'p', ".--."},
	{'q', "--.-"},
	{'r', ".-."},
	{'s', "..."},
	{'t', "-"},
	{'u', "..-"},
	{'v', "...-"},
	{'w', ".--"},
	{'x', "-..-"},
	{'y', "-.--"},
	{'z', "--.."},

	/* digits */

	{'0', "-----"},
	{'1', ".----"},
	{'2', "..---"},
	{'3', "...--"},
	{'4', "....-"},
	{'5', "....."},
	{'6', "-...."},
	{'7', "--..."},
	{'8', "---.."},
	{'9', "----."},

	/* punctuation */

	{',', "--..--"},
	{'.', ".-.-.-"},
	{'?', "..--.."},
	{'/', "-..-."},
	{'-', "-....-"},
	{'=', "-...-"},		/* BT */
	{':', "---..."},
	{';', "-.-.-."},
	{'(', "-.--."},		/* KN */
	{')', "-.--.-"},
	{'$', "...-..-"},
	{'+', ".-.-."},		/* AR */

	/* prosigns without already assigned values */

	{'#', ".-..."},		/* AS */
	{'@', "...-.-"},	/* SK */
	{'*', "...-."},		/* VE */
	{'%', "-...-.-"},	/* BK */

	{'\0', ""}
};


static const struct morsetab iso8859tab[] = {
	{'á', ".--.-"},
	{'à', ".--.-"},
	{'â', ".--.-"},
	{'ä', ".-.-"},
	{'ç', "-.-.."},
	{'é', "..-.."},
	{'è', "..-.."},
	{'ê', "-..-."},
	{'ö', "---."},
	{'ü', "..--"},

	{'\0', ""}
};

static const struct morsetab koi8rtab[] = {
	/*
	 * the cyrillic alphabet; you'll need a KOI8R font in order
	 * to see the actual characters
	 */
	{'Á', ".-"},		/* a */
	{'Â', "-..."},	/* be */
	{'×', ".--"},	/* ve */
	{'Ç', "--."},	/* ge */
	{'Ä', "-.."},	/* de */
	{'Å', "."},		/* ye */
	{'£', "."},         	/* yo, the same as ye */
	{'Ö', "...-"},	/* she */
	{'Ú', "--.."},	/* ze */
	{'É', ".."},		/* i */
	{'Ê', ".---"},	/* i kratkoye */
	{'Ë', "-.-"},	/* ka */
	{'Ì', ".-.."},	/* el */
	{'Í', "--"},		/* em */
	{'Î', "-."},		/* en */
	{'Ï', "---"},	/* o */
	{'Ð', ".--."},	/* pe */
	{'Ò', ".-."},	/* er */
	{'Ó', "..."},	/* es */
	{'Ô', "-"},		/* te */
	{'Õ', "..-"},	/* u */
	{'Æ', "..-."},	/* ef */
	{'È', "...."},	/* kha */
	{'Ã', "-.-."},	/* ce */
	{'Þ', "---."},	/* che */
	{'Û', "----"},	/* sha */
	{'Ý', "--.-"},	/* shcha */
	{'Ù', "-.--"},	/* yi */
	{'Ø', "-..-"},	/* myakhkij znak */
	{'Ü', "..-.."},	/* ae */
	{'À', "..--"},	/* yu */
	{'Ñ', ".-.-"},	/* ya */

	{'\0', ""}
};

struct tone_data {
	int16_t	*data;
	size_t	len;
};

void		alloc_soundbuf(struct tone_data *, double, int);
void            show(const char *), play(const char *), morse(char);
void		ttyout(const char *);
void		sighandler(int);

#define GETOPTOPTS "d:ef:pP:sw:"
#define USAGE \
"usage: morse [-s] [-e] [-p] [-P device] [-d device] [-w speed] [-f frequency] [string ...]\n"

static int      pflag, sflag, eflag;
static int      wpm = 20;	/* words per minute */
#define FREQUENCY 600
static int      freq = FREQUENCY;
static char	*device;	/* for tty-controlled generator */

static struct tone_data tone_dot, tone_dash, tone_silence;
#define DSP_RATE 44100
static const char *snddev = "/dev/dsp";

#define DASH_LEN 3
#define CHAR_SPACE 3
#define WORD_SPACE (7 - CHAR_SPACE)
static float    dot_clock;
int             spkr, line;
struct termios	otty, ntty;
int		olflags;

static const struct morsetab *hightab;

int
main(int argc, char **argv)
{
	int    ch, lflags;
	char  *p, *codeset;

	while ((ch = getopt(argc, argv, GETOPTOPTS)) != -1)
		switch ((char) ch) {
		case 'd':
			device = optarg;
			break;
		case 'e':
			eflag = 1;
			setvbuf(stdout, 0, _IONBF, 0);
			break;
		case 'f':
			freq = atoi(optarg);
			break;
		case 'p':
			pflag = 1;
			break;
		case 'P':
			snddev = optarg;
			break;
		case 's':
			sflag = 1;
			break;
		case 'w':
			wpm = atoi(optarg);
			break;
		case '?':
		default:
			fputs(USAGE, stderr);
			exit(1);
		}
	if ((pflag || device) && sflag) {
		fputs("morse: only one of -p, -d and -s allowed\n", stderr);
		exit(1);
	}
	if ((pflag || device) && ((wpm < 1) || (wpm > 60))) {
		fputs("morse: insane speed\n", stderr);
		exit(1);
	}
	if ((pflag || device) && (freq == 0))
		freq = FREQUENCY;
	if (pflag || device) {
		dot_clock = wpm / 2.4;		/* dots/sec */
		dot_clock = 1 / dot_clock;	/* duration of a dot */
		dot_clock = dot_clock / 2;	/* dot_clock runs at twice */
						/* the dot rate */
	}

	if (pflag) {
		snd_chan_param param;

		if ((spkr = open(snddev, O_WRONLY, 0)) == -1)
			err(1, "%s", snddev);
		param.play_rate = DSP_RATE;
		param.play_format = AFMT_S16_NE;
		param.rec_rate = 0;
		param.rec_format = 0;
		if (ioctl(spkr, AIOSFMT, &param) != 0)
			err(1, "%s: set format", snddev);
		alloc_soundbuf(&tone_dot, dot_clock, 1);
		alloc_soundbuf(&tone_dash, DASH_LEN * dot_clock, 1);
		alloc_soundbuf(&tone_silence, dot_clock, 0);
	} else
	if (device) {
		if ((line = open(device, O_WRONLY | O_NONBLOCK)) == -1) {
			perror("open tty line");
			exit(1);
		}
		if (tcgetattr(line, &otty) == -1) {
			perror("tcgetattr() failed");
			exit(1);
		}
		ntty = otty;
		ntty.c_cflag |= CLOCAL;
		tcsetattr(line, TCSANOW, &ntty);
		lflags = fcntl(line, F_GETFL);
		lflags &= ~O_NONBLOCK;
		fcntl(line, F_SETFL, &lflags);
		ioctl(line, TIOCMGET, &lflags);
		lflags &= ~TIOCM_RTS;
		olflags = lflags;
		ioctl(line, TIOCMSET, &lflags);
		(void)signal(SIGHUP, sighandler);
		(void)signal(SIGINT, sighandler);
		(void)signal(SIGQUIT, sighandler);
		(void)signal(SIGTERM, sighandler);
	}

	argc -= optind;
	argv += optind;

	if (setlocale(LC_CTYPE, "") != NULL &&
	    *(codeset = nl_langinfo(CODESET)) != '\0') {
		if (strcmp(codeset, "KOI8-R") == 0)
			hightab = koi8rtab;
		else if (strcmp(codeset, "ISO8859-1") == 0 ||
			 strcmp(codeset, "ISO8859-15") == 0)
			hightab = iso8859tab;
	}

	if (*argv) {
		do {
			for (p = *argv; *p; ++p) {
				if (eflag)
					putchar(*p);
				morse(*p);
			}
			if (eflag)
				putchar(' ');
			morse(' ');
		} while (*++argv);
	} else {
		while ((ch = getchar()) != EOF) {
			if (eflag)
				putchar(ch);
			morse(ch);
		}
	}
	if (device)
		tcsetattr(line, TCSANOW, &otty);
	exit(0);
}

void
alloc_soundbuf(struct tone_data *tone, double len, int on)
{
	int samples, i;

	samples = DSP_RATE * len;
	tone->len = samples * sizeof(*tone->data);
	tone->data = malloc(tone->len);
	if (tone->data == NULL)
		err(1, NULL);
	if (!on) {
		bzero(tone->data, tone->len);
		return;
	}

	/*
	 * We create a sinus with the specified frequency and smooth
	 * the edges to reduce key clicks.
	 */
	for (i = 0; i < samples; i++) {
		double filter = 1;

#define FILTER_SAMPLES 100
		if (i < FILTER_SAMPLES || i > samples - FILTER_SAMPLES) {
			/*
			 * Gauss window
			 */
#if 0
			int fi = i;

			if (i > FILTER_SAMPLES)
				fi = samples - i;
			filter = exp(-0.5 *
				     pow((double)(fi - FILTER_SAMPLES) /
					 (0.4 * FILTER_SAMPLES), 2));
#else
			/*
			 * Triangle window
			 */
			if (i < FILTER_SAMPLES)
				filter = (double)i / FILTER_SAMPLES;
			else
				filter = (double)(samples - i) / FILTER_SAMPLES;
#endif
		}
		tone->data[i] = 32767 * sin((double)i / samples * len * freq * 2 * M_PI) *
		    filter;
	}
}

void
morse(char c)
{
	const struct morsetab *m;

	if (isalpha((unsigned char)c))
		c = tolower((unsigned char)c);
	if ((c == '\r') || (c == '\n'))
		c = ' ';
	if (c == ' ') {
		if (pflag) {
			play(" ");
			return;
		} else if (device) {
			ttyout(" ");
			return;
		} else {
			show("");
			return;
		}
	}
	for (m = ((unsigned char)c < 0x80? mtab: hightab);
	     m != NULL && m->inchar != '\0';
	     m++) {
		if (m->inchar == c) {
			if (pflag) {
				play(m->morse);
			} else if (device) {
				ttyout(m->morse);
			} else
				show(m->morse);
		}
	}
}

void
show(const char *s)
{
	if (sflag)
		printf(" %s", s);
	else
		for (; *s; ++s)
			printf(" %s", *s == '.' ? "dit" : "dah");
	printf("\n");
}

void
play(const char *s)
{
	const char *c;
	int duration;
	struct tone_data *tone;

	/*
	 * We don't need to usleep() here, as the sound device blocks.
	 */
	for (c = s; *c != '\0'; c++) {
		switch (*c) {
		case '.':
			duration = 1;
			tone = &tone_dot;
			break;
		case '-':
			duration = 1;
			tone = &tone_dash;
			break;
		case ' ':
			duration = WORD_SPACE;
			tone = &tone_silence;
			break;
		default:
			errx(1, "invalid morse digit");
		}
		while (duration-- > 0)
			write(spkr, tone->data, tone->len);
		write(spkr, tone_silence.data, tone_silence.len);
	}
	duration = CHAR_SPACE - 1;  /* we already waited 1 after the last symbol */
	while (duration-- > 0)
		write(spkr, tone_silence.data, tone_silence.len);
}

void
ttyout(const char *s)
{
	const char *c;
	int duration, on, lflags;

	for (c = s; *c != '\0'; c++) {
		switch (*c) {
		case '.':
			on = 1;
			duration = dot_clock;
			break;
		case '-':
			on = 1;
			duration = dot_clock * DASH_LEN;
			break;
		case ' ':
			on = 0;
			duration = dot_clock * WORD_SPACE;
			break;
		default:
			on = 0;
			duration = 0;
		}
		if (on) {
			ioctl(line, TIOCMGET, &lflags);
			lflags |= TIOCM_RTS;
			ioctl(line, TIOCMSET, &lflags);
		}
		duration *= 1000000;
		if (duration)
			usleep(duration);
		ioctl(line, TIOCMGET, &lflags);
		lflags &= ~TIOCM_RTS;
		ioctl(line, TIOCMSET, &lflags);
		duration = dot_clock * 1000000;
		usleep(duration);
	}
	duration = dot_clock * CHAR_SPACE * 1000000;
	usleep(duration);
}

void
sighandler(int signo)
{

	ioctl(line, TIOCMSET, &olflags);
	tcsetattr(line, TCSANOW, &otty);

	signal(signo, SIG_DFL);
	(void)kill(getpid(), signo);
}
