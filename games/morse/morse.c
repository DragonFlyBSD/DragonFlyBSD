/*-
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
 * @(#) Copyright (c) 1988, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)morse.c	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/games/morse/morse.c,v 1.12.2.2 2002/03/12 17:45:15 phantom Exp $
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
	{'!', "-.-.--"},	/* KW */
	{'/', "-..-."},
	{'-', "-....-"},
	{'_', "..--.."},
	{'=', "-...-"},		/* BT */
	{':', "---..."},
	{';', "-.-.-."},
	{'(', "-.--."},		/* KN */
	{')', "-.--.-"},
	{'$', "...-..-"},
	{'+', ".-.-."},		/* AR */
	{'\'', ".----."},
	{'"', ".-..-."},
	{'@', ".--.-."},	/* AC */

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
void		morse(char, int);
void            show(const char *, int);
void		play(const char *, int);
void		ttyout(const char *, int);
void		sighandler(int);

#define GETOPTOPTS "d:ef:lopP:sw:W:"
#define USAGE \
"usage: morse [-els] [-p | -o] [-P device] [-d device] [-w speed] [-W speed] [-f frequency] [string ...]\n"

static int      lflag, oflag, pflag, sflag, eflag;
static int      wpm = 20;	/* words per minute */
static int	farnsworth = -1;
#define FREQUENCY 600
static int      freq = FREQUENCY;
static char	*device;	/* for tty-controlled generator */

static struct tone_data tone_dot, tone_dash, tone_silence, tone_letter_silence;
#define DSP_RATE 44100
static const char *snddev = NULL;

#define DASH_LEN 3
#define CHAR_SPACE 3
#define WORD_SPACE (7 - CHAR_SPACE)
static float    dot_clock, word_clock;
int             spkr, line;
struct termios	otty, ntty;
int		olflags;

static const struct morsetab *hightab;

int
main(int argc, char **argv)
{
	int    ch, lflags;
	int    prosign;
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
		case 'l':
			lflag = 1;
			break;
		case 'o':
			oflag = 1;
			/* FALLTHROUGH */
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
		case 'W':
			farnsworth = atoi(optarg);
			break;
		case '?':
		default:
			fputs(USAGE, stderr);
			exit(1);
		}
	if (sflag && lflag) {
		fputs("morse: only one of -l and -s allowed\n", stderr);
		exit(1);
	}
	if (pflag + !!device + sflag + lflag > 1) {
		fputs("morse: only one of -o, -p, -d and -l, -s allowed\n", stderr);
		exit(1);
	}
	if ((pflag || device) && ((wpm < 1) || (wpm > 60) || (farnsworth > 60))) {
		fputs("morse: insane speed\n", stderr);
		exit(1);
	}
	if ((pflag || device) && (freq == 0))
		freq = FREQUENCY;
	if (pflag || device) {
		/*
		 * A note on how to get to this magic 1.2:
		 * x WPM = 50*x dits per minute (norm word "PARIS").
		 * dits per sec = dits per minute / 60, thus
		 * dits per sec = 50 * x / 60 = x / (60 / 50) = x / 1.2
		 */
		dot_clock = wpm / 1.2;		/* dots/sec */
		dot_clock = 1 / dot_clock;	/* duration of a dot */

		word_clock = dot_clock;

		/*
		 * This is how to get to this formula:
		 * PARIS = 22 dit (symbols) + 9 symbol spaces = 31 symbol times
		 *       + 19 space times.
		 *
		 * The symbol times are in dot_clock, so the spaces have to
		 * make up to reach the farnsworth time.
		 */
		if (farnsworth > 0)
			word_clock = (60.0 / farnsworth - 31 * dot_clock) / 19;
	}
	if (snddev == NULL) {
		if (oflag)
			snddev = "-";
		else /* only pflag */
			snddev = "/dev/dsp";
	}

	if (pflag) {
		snd_chan_param param;

		if (oflag && strcmp(snddev, "-") == 0)
			spkr = STDOUT_FILENO;
		else
			spkr = open(snddev, O_WRONLY, 0);
		if (spkr == -1)
			err(1, "%s", snddev);
		param.play_rate = DSP_RATE;
		param.play_format = AFMT_S16_NE;
		param.rec_rate = 0;
		param.rec_format = 0;
		if (!oflag && ioctl(spkr, AIOSFMT, &param) != 0)
			err(1, "%s: set format", snddev);
		alloc_soundbuf(&tone_dot, dot_clock, 1);
		alloc_soundbuf(&tone_dash, DASH_LEN * dot_clock, 1);
		alloc_soundbuf(&tone_silence, dot_clock, 0);
		alloc_soundbuf(&tone_letter_silence, word_clock, 0);
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

	if (lflag)
		printf("m");
	if (*argv) {
		do {
			prosign = 0;
			for (p = *argv; *p; ++p) {
				if (eflag)
					putchar(*p);
				if (*p == '<' || *p == '>') {
					prosign = *p == '<';
					continue;
				}
				if (strchr("> \r\n", *(p + 1)) != NULL)
					prosign = 0;
				morse(*p, prosign);
			}
			if (eflag)
				putchar(' ');
			morse(' ', 0);
		} while (*++argv);
	} else {
		prosign = 0;
		while ((ch = getchar()) != EOF) {
			if (eflag)
				putchar(ch);
			if (ch == '<') {
				prosign = 1;
				continue;
			}
			if (prosign) {
				int tch;

				tch = getchar();
				if (strchr("> \r\n", tch) != NULL)
					prosign = 0;
				if (tch != '>')
					ungetc(tch, stdin);
			}
			morse(ch, prosign);
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

#define FILTER_SAMPLES (DSP_RATE * 8 / 1000)	/* 8 ms ramp time */
		if (i < FILTER_SAMPLES || i > samples - FILTER_SAMPLES) {
			int fi = i;

			if (i > FILTER_SAMPLES)
				fi = samples - i;
#if defined(TRIANGLE_FILTER)
			/*
			 * Triangle envelope
			 */
			filter = (double)fi / FILTER_SAMPLES;
#elif defined(GAUSS_FILTER)
			/*
			 * Gauss envelope
			 */
			filter = exp(-4.0 *
				     pow((double)(FILTER_SAMPLES - fi) /
					 FILTER_SAMPLES, 2));
#else
			/*
			 * Cosine envelope
			 */
			filter = (1 + cos(M_PI * (FILTER_SAMPLES - fi) / FILTER_SAMPLES)) / 2;
#endif
		}
		tone->data[i] = 32767 * sin((double)i / samples * len * freq * 2 * M_PI) *
		    filter;
	}
}

void
morse(char c, int prosign)
{
	const struct morsetab *m;

	if (isalpha((unsigned char)c))
		c = tolower((unsigned char)c);
	if ((c == '\r') || (c == '\n'))
		c = ' ';
	if (c == ' ') {
		if (pflag) {
			play(" ", 0);
			return;
		} else if (device) {
			ttyout(" ", 0);
			return;
		} else if (lflag) {
			printf("\n");
		} else {
			show("", 0);
			return;
		}
	}
	for (m = ((unsigned char)c < 0x80? mtab: hightab);
	     m != NULL && m->inchar != '\0';
	     m++) {
		if (m->inchar == c) {
			if (pflag) {
				play(m->morse, prosign);
			} else if (device) {
				ttyout(m->morse, prosign);
			} else
				show(m->morse, prosign);
		}
	}
}

void
show(const char *s, int prosign)
{
	if (lflag) {
		printf("%s ", s);
		return;
	} else if (sflag)
		printf(" %s", s);
	else
		for (; *s; ++s)
			printf(" %s", *s == '.' ? "dit" : "dah");
	if (!prosign)
		printf("\n");
}

void
play(const char *s, int prosign)
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
			tone = &tone_letter_silence;
			break;
		default:
			errx(1, "invalid morse digit");
		}
		while (duration-- > 0)
			write(spkr, tone->data, tone->len);
		/* Only space within a symbol */
		if (c[1] != '\0' || prosign)
			write(spkr, tone_silence.data, tone_silence.len);
	}
	if (prosign)
		return;
	duration = CHAR_SPACE;
	while (duration-- > 0)
		write(spkr, tone_letter_silence.data, tone_letter_silence.len);

	/* Sync out the audio data with other output */
	if (!oflag)
		ioctl(spkr, SNDCTL_DSP_SYNC, NULL);
}

void
ttyout(const char *s, int prosign)
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
			duration = word_clock * WORD_SPACE;
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
		/* Only space within a symbol */
		if (c[1] != '\0' || prosign)
			usleep(duration);
	}
	if (!prosign) {
		duration = word_clock * CHAR_SPACE * 1000000;
		usleep(duration);
	}
}

void
sighandler(int signo)
{

	ioctl(line, TIOCMSET, &olflags);
	tcsetattr(line, TCSANOW, &otty);

	signal(signo, SIG_DFL);
	(void)kill(getpid(), signo);
}
