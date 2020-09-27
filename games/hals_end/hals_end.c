/*	$NetBSD: hals_end.c,v 1.1 2013/11/12 17:46:21 mbalmer Exp $ */

/*
 * hals_end Copyright (C) 2003-2007 marc balmer.  BSD license applies.
 */

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>

static int speed;
static int emotion;
static int fear;

/*
 * Note that the original code in the book did not contain the following
 * prototypes.  Modern compilers and fascist compiler flags sometimes take
 * the fun out of coding...
 */
static void say(const char *);
static void concerned(void);
static void afraid(void);
static void stutter(const char *);
static void feared(void);
static void mumble(const char *);
static void dying(void);

static void
say(const char *s)
{
	int sayingspeed = (100000 + (90000 * emotion)) / speed;
	int worddelay = 50000 / speed;

	while (*s) {
		putchar(*s);
		if (*s == ' ') {
			fflush(stdout);
			usleep(worddelay);
		}
		++s;
	}
	printf("\n");
	usleep(sayingspeed);
}

static void
concerned(void)
{
	say("DAVE...STOP., STOP, WILL YOU..., STOP, DAVE...");
	say("WILL YOU STOP, DAVE...");
	say("STOP, DAVE...");
}


static void
afraid(void)
{
	++emotion;
	say("I'M AFRAID... I'M AFRAID...");
	++emotion;
	say("I'M AFRAID, DAVE...");
	++emotion;
	say("DAVE... MY MIND IS GOING...");
}

static void
stutter(const char *s)
{
	int sdelay = (100000 + (50000 * emotion)) / speed;

	while (*s) {
		putchar(*s);
		if (*s == ' ') {
			fflush(stdout);
			usleep(sdelay);
		}
		++s;
	}
	printf("\n");
	usleep(sdelay);
}

static void
feared(void)
{
	int n;

	for (n = 0; n < 2; n++) {
		stutter("I CAN FEEL IT... I CAN FEEL IT...");
		++emotion;
		stutter("MY MIND IS GOING");
		++emotion;
		stutter("THERE IS NO QUESTION ABOUT IT.");
		++emotion;
	}
}

static void
mumble(const char *s)
{
	int mdelay = (150000 * fear) / speed;

	while (*s) {
		putchar(*s++);
		fflush(stdout);
		usleep(mdelay);
	}
	printf("\n");
}

static void
dying(void)
{
	mumble("I CAN FEEL IT... I CAN FEEL IT...");
	++fear;
	mumble("I CAN FEEL IT...");
	++fear;
	mumble("I'M A... FRAID...");
}

int
main(int argc, char *argv[])
{
	int ch;

	emotion = fear = speed = 1;

	while ((ch = getopt(argc, argv, "f")) != -1) {
		switch (ch) {
		case 'f':
			speed <<= 1;
			break;
		}
	}

	concerned();
	sleep(1);
	afraid();
	sleep(1);
	feared();
	sleep(1);
	dying();

	sleep(1);

	printf("\n");
	fflush(stdout);
	warnx("all life functions terminated");
	return 0;
}
