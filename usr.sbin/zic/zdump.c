#ifndef lint
#ifndef NOID
static char	elsieid[] = "@(#)zdump.c	7.28";
#endif /* !defined NOID */
#endif /* !defined lint */

/*
 * @(#)zdump.c	7.28
 * $FreeBSD: src/usr.sbin/zic/zdump.c,v 1.7 1999/08/28 01:21:19 peter Exp $
 * $DragonFly: src/usr.sbin/zic/zdump.c,v 1.4 2004/12/18 22:48:15 swildner Exp $
 */
/*
** This code has been made independent of the rest of the time
** conversion package to increase confidence in the verification it provides.
** You can use this code to help in verifying other implementations.
*/

#include <err.h>
#include <stdio.h>	/* for stdout, stderr */
#include <stdlib.h>	/* for exit, malloc, atoi */
#include <string.h>	/* for strcpy */
#include <sys/types.h>	/* for time_t */
#include <time.h>	/* for struct tm */
#include <unistd.h>

#ifndef MAX_STRING_LENGTH
#define MAX_STRING_LENGTH	1024
#endif /* !defined MAX_STRING_LENGTH */

#ifndef TRUE
#define TRUE		1
#endif /* !defined TRUE */

#ifndef FALSE
#define FALSE		0
#endif /* !defined FALSE */

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS	0
#endif /* !defined EXIT_SUCCESS */

#ifndef EXIT_FAILURE
#define EXIT_FAILURE	1
#endif /* !defined EXIT_FAILURE */

#ifndef SECSPERMIN
#define SECSPERMIN	60
#endif /* !defined SECSPERMIN */

#ifndef MINSPERHOUR
#define MINSPERHOUR	60
#endif /* !defined MINSPERHOUR */

#ifndef SECSPERHOUR
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#endif /* !defined SECSPERHOUR */

#ifndef HOURSPERDAY
#define HOURSPERDAY	24
#endif /* !defined HOURSPERDAY */

#ifndef EPOCH_YEAR
#define EPOCH_YEAR	1970
#endif /* !defined EPOCH_YEAR */

#ifndef TM_YEAR_BASE
#define TM_YEAR_BASE	1900
#endif /* !defined TM_YEAR_BASE */

#ifndef DAYSPERNYEAR
#define DAYSPERNYEAR	365
#endif /* !defined DAYSPERNYEAR */

#ifndef isleap
#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)
#endif /* !defined isleap */

#if HAVE_GETTEXT - 0
#include "locale.h"	/* for setlocale */
#include "libintl.h"
#endif /* HAVE_GETTEXT - 0 */

#ifndef GNUC_or_lint
#ifdef lint
#define GNUC_or_lint
#endif /* defined lint */
#ifndef lint
#ifdef __GNUC__
#define GNUC_or_lint
#endif /* defined __GNUC__ */
#endif /* !defined lint */
#endif /* !defined GNUC_or_lint */

#ifndef INITIALIZE
#ifdef GNUC_or_lint
#define INITIALIZE(x)	((x) = 0)
#endif /* defined GNUC_or_lint */
#ifndef GNUC_or_lint
#define INITIALIZE(x)
#endif /* !defined GNUC_or_lint */
#endif /* !defined INITIALIZE */

/*
** For the benefit of GNU folk...
** `_(MSGID)' uses the current locale's message library string for MSGID.
** The default is to use gettext if available, and use MSGID otherwise.
*/

#ifndef _
#if HAVE_GETTEXT - 0
#define _(msgid) gettext(msgid)
#else /* !(HAVE_GETTEXT - 0) */
#define _(msgid) msgid
#endif /* !(HAVE_GETTEXT - 0) */
#endif /* !defined _ */

#ifndef TZ_DOMAIN
#define TZ_DOMAIN "tz"
#endif /* !defined TZ_DOMAIN */

extern char **	environ;
extern char *	tzname[2];

static char	*abbr(struct tm *tmp);
static long	 delta(struct tm *newp, struct tm *oldp);
static time_t	 hunt(char *name, time_t lot, time_t hit);
static size_t	 longest;
static char	*progname;
static void	 show(char *zone, time_t t, int v);
static void	 usage(void);

int
main(int argc, char *argv[])
{
	int i;
	int c;
	int vflag;
	char *cutoff;
	int cutyear;
	long cuttime;
	char **fakeenv;
	time_t now;
	time_t t;
	time_t newt;
	time_t hibit;
	struct tm tm;
	struct tm newtm;

	INITIALIZE(cuttime);
#if HAVE_GETTEXT - 0
	setlocale(LC_MESSAGES, "");
#ifdef TZ_DOMAINDIR
	bindtextdomain(TZ_DOMAIN, TZ_DOMAINDIR);
#endif /* defined(TEXTDOMAINDIR) */
	textdomain(TZ_DOMAIN);
#endif /* HAVE_GETTEXT - 0 */
	vflag = 0;
	cutoff = NULL;
	while ((c = getopt(argc, argv, "c:v")) == 'c' || c == 'v')
		if (c == 'v')
			vflag = 1;
		else	cutoff = optarg;
	if ((c != EOF && c != -1) ||
		(optind == argc - 1 && strcmp(argv[optind], "=") == 0)) {
			usage();
	}
	if (cutoff != NULL) {
		int	y;

		cutyear = atoi(cutoff);
		cuttime = 0;
		for (y = EPOCH_YEAR; y < cutyear; ++y)
			cuttime += DAYSPERNYEAR + isleap(y);
		cuttime *= SECSPERHOUR * HOURSPERDAY;
	}
	time(&now);
	longest = 0;
	for (i = optind; i < argc; ++i)
		if (strlen(argv[i]) > longest)
			longest = strlen(argv[i]);
	for (hibit = 1; (hibit << 1) != 0; hibit <<= 1)
		continue;
	{
		int from;
		int to;

		for (i = 0;  environ[i] != NULL;  ++i)
			continue;
		fakeenv = (char **) malloc((size_t) ((i + 2) *
			sizeof *fakeenv));
		if (fakeenv == NULL ||
			(fakeenv[0] = (char *) malloc((size_t) (longest +
				4))) == NULL)
					errx(EXIT_FAILURE,
					     _("malloc() failed"));
		to = 0;
		strcpy(fakeenv[to++], "TZ=");
		for (from = 0; environ[from] != NULL; ++from)
			if (strncmp(environ[from], "TZ=", 3) != 0)
				fakeenv[to++] = environ[from];
		fakeenv[to] = NULL;
		environ = fakeenv;
	}
	for (i = optind; i < argc; ++i) {
		static char	buf[MAX_STRING_LENGTH];

		strcpy(&fakeenv[0][3], argv[i]);
		if (!vflag) {
			show(argv[i], now, FALSE);
			continue;
		}
		/*
		** Get lowest value of t.
		*/
		t = hibit;
		if (t > 0)		/* time_t is unsigned */
			t = 0;
		show(argv[i], t, TRUE);
		t += SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
		tm = *localtime(&t);
		strncpy(buf, abbr(&tm), (sizeof buf) - 1);
		for ( ; ; ) {
			if (cutoff != NULL && t >= cuttime)
				break;
			newt = t + SECSPERHOUR * 12;
			if (cutoff != NULL && newt >= cuttime)
				break;
			if (newt <= t)
				break;
			newtm = *localtime(&newt);
			if (delta(&newtm, &tm) != (newt - t) ||
				newtm.tm_isdst != tm.tm_isdst ||
				strcmp(abbr(&newtm), buf) != 0) {
					newt = hunt(argv[i], t, newt);
					newtm = *localtime(&newt);
					strncpy(buf, abbr(&newtm),
						(sizeof buf) - 1);
			}
			t = newt;
			tm = newtm;
		}
		/*
		** Get highest value of t.
		*/
		t = ~((time_t) 0);
		if (t < 0)		/* time_t is signed */
			t &= ~hibit;
		t -= SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
		t += SECSPERHOUR * HOURSPERDAY;
		show(argv[i], t, TRUE);
	}
	if (fflush(stdout) || ferror(stdout))
		errx(EXIT_FAILURE, _("error writing standard output"));
	exit(EXIT_SUCCESS);

	/* gcc -Wall pacifier */
	for ( ; ; )
		continue;
}

static void
usage(void)
{
	fprintf(stderr, _("usage: zdump [-v] [-c cutoff] zonename ...\n"));
	exit(EXIT_FAILURE);
}

static time_t
hunt(char *name, time_t lot, time_t hit)
{
	time_t		t;
	struct tm	lotm;
	struct tm	tm;
	static char	loab[MAX_STRING_LENGTH];

	lotm = *localtime(&lot);
	strncpy(loab, abbr(&lotm), (sizeof loab) - 1);
	while ((hit - lot) >= 2) {
		t = lot / 2 + hit / 2;
		if (t <= lot)
			++t;
		else if (t >= hit)
			--t;
		tm = *localtime(&t);
		if (delta(&tm, &lotm) == (t - lot) &&
			tm.tm_isdst == lotm.tm_isdst &&
			strcmp(abbr(&tm), loab) == 0) {
				lot = t;
				lotm = tm;
		} else	hit = t;
	}
	show(name, lot, TRUE);
	show(name, hit, TRUE);
	return hit;
}

/*
** Thanks to Paul Eggert (eggert@twinsun.com) for logic used in delta.
*/

static long
delta(struct tm *newp, struct tm *oldp)
{
	long	result;
	int	tmy;

	if (newp->tm_year < oldp->tm_year)
		return -delta(oldp, newp);
	result = 0;
	for (tmy = oldp->tm_year; tmy < newp->tm_year; ++tmy)
		result += DAYSPERNYEAR + isleap(tmy + TM_YEAR_BASE);
	result += newp->tm_yday - oldp->tm_yday;
	result *= HOURSPERDAY;
	result += newp->tm_hour - oldp->tm_hour;
	result *= MINSPERHOUR;
	result += newp->tm_min - oldp->tm_min;
	result *= SECSPERMIN;
	result += newp->tm_sec - oldp->tm_sec;
	return result;
}

static void
show(char *zone, time_t t, int v)
{
	struct tm *	tmp;

	printf("%-*s  ", (int) longest, zone);
	if (v)
		printf("%.24s UTC = ", asctime(gmtime(&t)));
	tmp = localtime(&t);
	printf("%.24s", asctime(tmp));
	if (*abbr(tmp) != '\0')
		printf(" %s", abbr(tmp));
	if (v) {
		printf(" isdst=%d", tmp->tm_isdst);
#ifdef TM_GMTOFF
		printf(" gmtoff=%ld", tmp->TM_GMTOFF);
#endif /* defined TM_GMTOFF */
	}
	printf("\n");
}

static char *
abbr(struct tm *tmp)
{
	char * result;
	static char nada;

	if (tmp->tm_isdst != 0 && tmp->tm_isdst != 1)
		return &nada;
	result = tzname[tmp->tm_isdst];
	return (result == NULL) ? &nada : result;
}
