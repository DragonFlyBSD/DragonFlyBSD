/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Muffy Barkocy.
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
 * $FreeBSD: src/games/fish/fish.c,v 1.9 1999/12/10 16:21:50 billf Exp $
 * $DragonFly: src/games/fish/fish.c,v 1.4 2005/07/31 20:40:26 swildner Exp $
 *
 * @(#) Copyright (c) 1990, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)fish.c	8.1 (Berkeley) 5/31/93
 */

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "pathnames.h"

#define	RANKS		13
#define	HANDSIZE	7
#define	CARDS		4

#define	USER		1
#define	COMPUTER	0
#define	OTHER(a)	(1 - (a))

const char *cards[] = {
	"A", "2", "3", "4", "5", "6", "7",
	"8", "9", "10", "J", "Q", "K", NULL,
};
#define	PRC(card)	printf(" %s", cards[card])

int promode;
int asked[RANKS], comphand[RANKS], deck[RANKS];
int userasked[RANKS], userhand[RANKS];

static void chkwinner(int, int *);
static int compmove(void);
static int countbooks(int *);
static int countcards(int *);
static int drawcard(int, int *);
static int gofish(int, int, int *);
static void goodmove(int, int, int *, int *);
static void init(void);
static void instructions(void);
static int nrandom(int);
static void printhand(int *);
static void printplayer(int);
static int promove(void);
static void usage(void);
static int usermove(void);

int
main(int argc, char **argv)
{
	int ch, move;

	while ((ch = getopt(argc, argv, "p")) != -1)
		switch(ch) {
		case 'p':
			promode = 1;
			break;
		case '?':
		default:
			usage();
		}

	srandomdev();
	instructions();
	init();

	if (nrandom(2) == 1) {
		printplayer(COMPUTER);
		printf("get to start.\n");
		goto istart;
	}
	printplayer(USER);
	printf("get to start.\n");

	for (;;) {
		move = usermove();
		if (!comphand[move]) {
			if (gofish(move, USER, userhand))
				continue;
		} else {
			goodmove(USER, move, userhand, comphand);
			continue;
		}

istart:		for (;;) {
			move = compmove();
			if (!userhand[move]) {
				if (!gofish(move, COMPUTER, comphand))
					break;
			} else
				goodmove(COMPUTER, move, comphand, userhand);
		}
	}
	/* NOTREACHED */
	return (EXIT_FAILURE);
}

static int
usermove(void)
{
	int n;
	const char **p;
	char buf[256];

	printf("\nYour hand is:");
	printhand(userhand);

	for (;;) {
		printf("You ask me for: ");
		fflush(stdout);
		if (fgets(buf, sizeof(buf), stdin) == NULL)
			exit(0);
		if (buf[0] == '\0')
			continue;
		if (buf[0] == '\n') {
			printf("%d cards in my hand, %d in the pool.\n",
			    countcards(comphand), countcards(deck));
			printf("My books:");
			countbooks(comphand);
			continue;
		}
		buf[strlen(buf) - 1] = '\0';
		if (!strcasecmp(buf, "p") && !promode) {
			promode = 1;
			printf("Entering pro mode.\n");
			continue;
		}
		if (!strcasecmp(buf, "quit"))
			exit(0);
		for (p = cards; *p; ++p)
			if (!strcasecmp(*p, buf))
				break;
		if (!*p) {
			printf("I don't understand!\n");
			continue;
		}
		n = p - cards;
		if (userhand[n] <= 3) {
			userasked[n] = 1;
			return(n);
		}
		if (userhand[n] == 4) {
			printf("You already have all of those.\n");
			continue;
		}

		if (nrandom(3) == 1)
			printf("You don't have any of those!\n");
		else
			printf("You don't have any %s's!\n", cards[n]);
		if (nrandom(4) == 1)
			printf("No cheating!\n");
		printf("Guess again.\n");
	}
	/* NOTREACHED */
}

static int
compmove(void)
{
	static int lmove;

	if (promode)
		lmove = promove();
	else {
		do {
			lmove = (lmove + 1) % RANKS;
		} while (!comphand[lmove] || comphand[lmove] == CARDS);
	}
	asked[lmove] = 1;

	printf("I ask you for: %s.\n", cards[lmove]);
	return(lmove);
}

static int
promove(void)
{
	int i, max;

	for (i = 0; i < RANKS; ++i)
		if (userasked[i] && comphand[i] > 0 && comphand[i] < CARDS) {
			userasked[i] = 0;
			return(i);
		}
	if (nrandom(3) == 1) {
		for (i = 0;; ++i)
			if (comphand[i] && comphand[i] != CARDS) {
				max = i;
				break;
			}
		while (++i < RANKS)
			if (comphand[i] != CARDS && comphand[i] > comphand[max])
				max = i;
		return(max);
	}
	if (nrandom(1024) == 0723) {
		for (i = 0; i < RANKS; ++i)
			if (userhand[i] && comphand[i])
				return(i);
	}
	for (;;) {
		for (i = 0; i < RANKS; ++i)
			if (comphand[i] && comphand[i] != CARDS && !asked[i])
				return(i);
		for (i = 0; i < RANKS; ++i)
			asked[i] = 0;
	}
	/* NOTREACHED */
}

static int
drawcard(int player, int *hand)
{
	int card;

	while (deck[card = nrandom(RANKS)] == 0)
		; /* nothing */
	++hand[card];
	--deck[card];
	if (player == USER || hand[card] == CARDS) {
		printplayer(player);
		printf("drew %s", cards[card]);
		if (hand[card] == CARDS) {
			printf(" and made a book of %s's!\n", cards[card]);
			chkwinner(player, hand);
		} else
			printf(".\n");
	}
	return(card);
}

static int
gofish(int askedfor, int player, int *hand)
{
	printplayer(OTHER(player));
	printf("say \"GO FISH!\"\n");
	if (askedfor == drawcard(player, hand)) {
		printplayer(player);
		printf("drew the guess!\n");
		printplayer(player);
		printf("get to ask again!\n");
		return(1);
	}
	return(0);
}

static void
goodmove(int player, int move, int *hand, int *opphand)
{
	printplayer(OTHER(player));
	printf("have %d %s%s.\n",
	    opphand[move], cards[move], opphand[move] == 1 ? "": "'s");

	hand[move] += opphand[move];
	opphand[move] = 0;

	if (hand[move] == CARDS) {
		printplayer(player);
		printf("made a book of %s's!\n", cards[move]);
		chkwinner(player, hand);
	}

	chkwinner(OTHER(player), opphand);

	printplayer(player);
	printf("get another guess!\n");
}

static void
chkwinner(int player, int *hand)
{
	int cb, i, ub;

	for (i = 0; i < RANKS; ++i)
		if (hand[i] > 0 && hand[i] < CARDS)
			return;
	printplayer(player);
	printf("don't have any more cards!\n");
	printf("My books:");
	cb = countbooks(comphand);
	printf("Your books:");
	ub = countbooks(userhand);
	printf("\nI have %d, you have %d.\n", cb, ub);
	if (ub > cb) {
		printf("\nYou win!!!\n");
		if (nrandom(1024) == 0723)
			printf("Cheater, cheater, pumpkin eater!\n");
	} else if (cb > ub) {
		printf("\nI win!!!\n");
		if (nrandom(1024) == 0723)
			printf("Hah!  Stupid peasant!\n");
	} else
		printf("\nTie!\n");
	exit(0);
}

static void
printplayer(int player)
{
	switch (player) {
	case COMPUTER:
		printf("I ");
		break;
	case USER:
		printf("You ");
		break;
	}
}

static void
printhand(int *hand)
{
	int book, i, j;

	for (book = i = 0; i < RANKS; i++)
		if (hand[i] < CARDS)
			for (j = hand[i]; --j >= 0;)
				PRC(i);
		else
			++book;
	if (book) {
		printf(" + Book%s of", book > 1 ? "s" : "");
		for (i = 0; i < RANKS; i++)
			if (hand[i] == CARDS)
				PRC(i);
	}
	putchar('\n');
}

static int
countcards(int *hand)
{
	int i, count;

	for (count = i = 0; i < RANKS; i++)
		count += *hand++;
	return(count);
}

static int
countbooks(int *hand)
{
	int i, count;

	for (count = i = 0; i < RANKS; i++)
		if (hand[i] == CARDS) {
			++count;
			PRC(i);
		}
	if (!count)
		printf(" none");
	putchar('\n');
	return(count);
}

static void
init(void)
{
	int i, rank;

	for (i = 0; i < RANKS; ++i)
		deck[i] = CARDS;
	for (i = 0; i < HANDSIZE; ++i) {
		while (!deck[rank = nrandom(RANKS)])
			; /* nothing */
		++userhand[rank];
		--deck[rank];
	}
	for (i = 0; i < HANDSIZE; ++i) {
		while (!deck[rank = nrandom(RANKS)])
			; /* nothing */
		++comphand[rank];
		--deck[rank];
	}
}

static int
nrandom(int n)
{

	return((int)random() % n);
}

static void
instructions(void)
{
	int input;
	char buf[1024];

	printf("Would you like instructions (y or n)? ");
	input = getchar();
	while (getchar() != '\n');
	if (input != 'y')
		return;

	sprintf(buf, "%s %s", _PATH_MORE, _PATH_INSTR);
	system(buf);
	printf("Hit return to continue...\n");
	while ((input = getchar()) != EOF && input != '\n');
}

static void
usage(void)
{
	fprintf(stderr, "usage: fish [-p]\n");
	exit(1);
}
