/*
 * $FreeBSD: src/usr.sbin/zic/zdump.c,v 1.7 1999/08/28 01:21:19 peter Exp $
 */
/*
** This code has been made independent of the rest of the time
** conversion package to increase confidence in the verification it provides.
** You can use this code to help in verifying other implementations.
*/

#include <ctype.h>	/* for isalpha et al. */
#include <err.h>
#include <inttypes.h>
#include <limits.h>	/* for CHAR_BIT, LLONG_MAX */
#include <stdint.h>
#include <stdio.h>	/* for stdout, stderr */
#include <stdlib.h>	/* for exit, malloc, atoi */
#include <string.h>	/* for strcpy */
#include <sys/types.h>	/* for time_t */
#include <time.h>	/* for struct tm */
#include <unistd.h>

#ifndef ZDUMP_LO_YEAR
#define ZDUMP_LO_YEAR	(-500)
#endif /* !defined ZDUMP_LO_YEAR */

#ifndef ZDUMP_HI_YEAR
#define ZDUMP_HI_YEAR	2500
#endif /* !defined ZDUMP_HI_YEAR */
 
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
#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))
#endif /* !defined isleap */

#ifndef isleap_sum
/*
** See tzfile.h for details on isleap_sum.
*/
#define isleap_sum(a, b)	isleap((a) % 400 + (b) % 400)
#endif /* !defined isleap_sum */

#define SECSPERDAY	((int_fast32_t) SECSPERHOUR * HOURSPERDAY)
#define SECSPERNYEAR	(SECSPERDAY * DAYSPERNYEAR)
#define SECSPERLYEAR	(SECSPERNYEAR + SECSPERDAY)
#define SECSPER400YEARS	(SECSPERNYEAR * (intmax_t) (300 + 3)	\
			 + SECSPERLYEAR * (intmax_t) (100 - 3))

/*
** True if SECSPER400YEARS is known to be representable as an
** intmax_t.  It's OK that SECSPER400YEARS_FITS can in theory be false
** even if SECSPER400YEARS is representable, because when that happens
** the code merely runs a bit more slowly, and this slowness doesn't
** occur on any practical platform.
*/
enum { SECSPER400YEARS_FITS = SECSPERLYEAR <= INTMAX_MAX / 400 };

/*
** For the benefit of GNU folk...
** `_(MSGID)' uses the current locale's message library string for MSGID.
** The default is to use gettext if available, and use MSGID otherwise.
*/

#ifndef _
#define _(msgid) msgid
#endif /* !defined _ */

#ifndef TZ_DOMAIN
#define TZ_DOMAIN "tz"
#endif /* !defined TZ_DOMAIN */

extern char **	environ;
extern char *	tzname[2];

/* The minimum and maximum finite time values.  */
static time_t const absolute_min_time =
  ((time_t) -1 < 0
   ? (time_t) -1 << (CHAR_BIT * sizeof (time_t) - 1)
   : 0);
static time_t const absolute_max_time =
  ((time_t) -1 < 0
   ? - (~ 0 < 0) - ((time_t) -1 << (CHAR_BIT * sizeof (time_t) - 1))
   : -1);
static size_t	 longest;
static int	 warned;

static char	*abbr(struct tm *tmp);
static void	 abbrok(const char *abbrp, const char *zone);
static intmax_t	 delta(struct tm * newp, struct tm * oldp) __pure;
static void	 dumptime(const struct tm *tmp);
static time_t	 hunt(char *name, time_t lot, time_t hit);
static void	 show(char *zone, time_t t, int v);
static const char *tformat(void);
static time_t	 yeartot(intmax_t y) __pure;
static void	 usage(void);

#ifndef TYPECHECK
#define my_localtime	localtime
#else /* !defined TYPECHECK */
static struct tm *
my_localtime(time_t *tp)
{
	struct tm *	tmp;

	tmp = localtime(tp);
	if (tp != NULL && tmp != NULL) {
		struct tm	tm;
		time_t		t;

		tm = *tmp;
		t = mktime(&tm);
		if (t != *tp) {
			fflush(stdout);
			fprintf(stderr, "\n%s: ", progname);
			fprintf(stderr, tformat(), *tp);
			fprintf(stderr, " ->");
			fprintf(stderr, " year=%d", tmp->tm_year);
			fprintf(stderr, " mon=%d", tmp->tm_mon);
			fprintf(stderr, " mday=%d", tmp->tm_mday);
			fprintf(stderr, " hour=%d", tmp->tm_hour);
			fprintf(stderr, " min=%d", tmp->tm_min);
			fprintf(stderr, " sec=%d", tmp->tm_sec);
			fprintf(stderr, " isdst=%d", tmp->tm_isdst);
			fprintf(stderr, " -> ");
			fprintf(stderr, tformat(), t);
			fprintf(stderr, "\n");
		}
	}
	return tmp;
}
#endif /* !defined TYPECHECK */

static void
abbrok(const char * const abbrp, const char * const zone)
{
	const char *cp;
	const char *wp;

	if (warned)
		return;
	cp = abbrp;
	wp = NULL;
	while (isascii((unsigned char) *cp) && isalpha((unsigned char) *cp))
		++cp;
	if (cp - abbrp == 0)
		wp = _("lacks alphabetic at start");
	else if (cp - abbrp < 3)
		wp = _("has fewer than 3 alphabetics");
	else if (cp - abbrp > 6)
		wp = _("has more than 6 alphabetics");
	if (wp == NULL && (*cp == '+' || *cp == '-')) {
		++cp;
		if (isascii((unsigned char) *cp) &&
			isdigit((unsigned char) *cp))
				if (*cp++ == '1' && *cp >= '0' && *cp <= '4')
					++cp;
		if (*cp != '\0')
			wp = _("differs from POSIX standard");
	}
	if (wp == NULL)
		return;
	fflush(stdout);
	warnx(_("warning: zone \"%s\" abbreviation \"%s\" %s\n"),
		zone, abbrp, wp);
	warned = TRUE;
}

int
main(int argc, char *argv[])
{
	int i;
	int vflag;
	int Vflag;
	char *cutarg;
	char *cuttimes;
	time_t cutlotime;
	time_t cuthitime;
	char **fakeenv;
	time_t now;
	time_t t;
	time_t newt;
	struct tm tm;
	struct tm newtm;
	struct tm *tmp;
	struct tm *newtmp;

	cutlotime = absolute_min_time;
	cuthitime = absolute_max_time;
	vflag = Vflag = 0;
	cutarg = cuttimes = NULL;
	for (;;)
	  switch (getopt(argc, argv, "c:t:vV")) {
	  case 'c': cutarg = optarg; break;
	  case 't': cuttimes = optarg; break;
	  case 'v': vflag = 1; break;
	  case 'V': Vflag = 1; break;
	  case -1:
	    if (! (optind == argc - 1 && strcmp(argv[optind], "=") == 0))
	      goto arg_processing_done;
	    /* Fall through.  */
	  default:
	    usage();
	  }
 arg_processing_done:;

	if (vflag | Vflag) {
		intmax_t	lo;
		intmax_t	hi;
		char *loend, *hiend;
		intmax_t cutloyear = ZDUMP_LO_YEAR;
		intmax_t cuthiyear = ZDUMP_HI_YEAR;
		if (cutarg != NULL) {
			lo = strtoimax(cutarg, &loend, 10);
			if (cutarg != loend && !*loend) {
				hi = lo;
				cuthiyear = hi;
			} else if (cutarg != loend && *loend == ','
				   && (hi = strtoimax(loend + 1, &hiend, 10),
				       loend + 1 != hiend && !*hiend)) {
				cutloyear = lo;
				cuthiyear = hi;
			} else {
				errx(EXIT_FAILURE,
					_("wild -c argument %s\n"),
					cutarg);
			}
		}
		if (cutarg != NULL || cuttimes == NULL) {
			cutlotime = yeartot(cutloyear);
			cuthitime = yeartot(cuthiyear);
		}
		if (cuttimes != NULL) {
			lo = strtoimax(cuttimes, &loend, 10);
			if (cuttimes != loend && !*loend) {
				hi = lo;
				if (hi < cuthitime) {
					if (hi < absolute_min_time)
						hi = absolute_min_time;
					cuthitime = hi;
				}
			} else if (cuttimes != loend && *loend == ','
				   && (hi = strtoimax(loend + 1, &hiend, 10),
				       loend + 1 != hiend && !*hiend)) {
				if (cutlotime < lo) {
					if (absolute_max_time < lo)
						lo = absolute_max_time;
					cutlotime = lo;
				}
				if (hi < cuthitime) {
					if (hi < absolute_min_time)
						hi = absolute_min_time;
					cuthitime = hi;
				}
			} else {
				errx(EXIT_FAILURE,
					_("wild -t argument %s\n"),
					cuttimes);
			}
		}
	}
	time(&now);
	longest = 0;
	for (i = optind; i < argc; ++i)
		if (strlen(argv[i]) > longest)
			longest = strlen(argv[i]);
	{
		int from;
		int to;

		for (i = 0; environ[i] != NULL;  ++i)
			continue;
		fakeenv = malloc((i + 2) * sizeof *fakeenv);
		if (fakeenv == NULL
		    || (fakeenv[0] = malloc(longest + 4)) == NULL) {
					errx(EXIT_FAILURE,
					     _("malloc() failed"));
		}
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
		if (! (vflag | Vflag)) {
			show(argv[i], now, FALSE);
			continue;
		}
		warned = FALSE;
		t = absolute_min_time;
		if (!Vflag) {
			show(argv[i], t, TRUE);
			t += SECSPERDAY;
			show(argv[i], t, TRUE);
		}
		if (t < cutlotime)
			t = cutlotime;
		tmp = my_localtime(&t);
		if (tmp != NULL) {
			tm = *tmp;
			strncpy(buf, abbr(&tm), (sizeof buf) - 1);
		}
		for ( ; ; ) {
			newt = (t < absolute_max_time - SECSPERDAY / 2
				? t + SECSPERDAY / 2
				: absolute_max_time);
			if (cuthitime <= newt)
				break;
			newtmp = localtime(&newt);
			if (newtmp != NULL)
				newtm = *newtmp;
			if ((tmp == NULL || newtmp == NULL) ? (tmp != newtmp) :
				(delta(&newtm, &tm) != (newt - t) ||
				newtm.tm_isdst != tm.tm_isdst ||
				strcmp(abbr(&newtm), buf) != 0)) {
					newt = hunt(argv[i], t, newt);
					newtmp = localtime(&newt);
					if (newtmp != NULL) {
						newtm = *newtmp;
						strncpy(buf,
							abbr(&newtm),
							(sizeof buf) - 1);
					}
			}
			t = newt;
			tm = newtm;
			tmp = newtmp;
		}
		if (!Vflag) {
			t = absolute_max_time;
			t -= SECSPERDAY;
			show(argv[i], t, TRUE);
			t += SECSPERDAY;
			show(argv[i], t, TRUE);
		}
	}
	if (fflush(stdout) || ferror(stdout))
		errx(EXIT_FAILURE, _("error writing standard output"));
	exit(EXIT_SUCCESS);
	/* If exit fails to exit... */
	return EXIT_FAILURE;
}

static void
usage(void)
{
	fprintf(stderr, _("usage: zdump [-vV] [-c [loyear,]hiyear] [-t [lotime,]hitime] zonename ...\n"));
	exit(EXIT_FAILURE);
}

static time_t
yeartot(const intmax_t y)
{
	intmax_t	myy, seconds, years;
	time_t		t;

	myy = EPOCH_YEAR;
	t = 0;
	while (myy < y) {
		if (SECSPER400YEARS_FITS && 400 <= y - myy) {
			intmax_t diff400 = (y - myy) / 400;
			if (INTMAX_MAX / SECSPER400YEARS < diff400)
				return absolute_max_time;
			seconds = diff400 * SECSPER400YEARS;
			years = diff400 * 400;
                } else {
			seconds = isleap(myy) ? SECSPERLYEAR : SECSPERNYEAR;
			years = 1;
		}
		myy += years;
		if (t > absolute_max_time - seconds)
			return absolute_max_time;
		t += seconds;
	}
	while (y < myy) {
		if (SECSPER400YEARS_FITS && y + 400 <= myy && myy < 0) {
			intmax_t diff400 = (myy - y) / 400;
			if (INTMAX_MAX / SECSPER400YEARS < diff400)
				return absolute_min_time;
			seconds = diff400 * SECSPER400YEARS;
			years = diff400 * 400;
		} else {
			seconds = isleap(myy - 1) ? SECSPERLYEAR : SECSPERNYEAR;
			years = 1;
		}
		myy -= years;
		if (t < absolute_min_time + seconds)
			return absolute_min_time;
		t -= seconds;
	}
	return t;
}

static time_t
hunt(char *name, time_t lot, time_t hit)
{
	time_t		t;
	struct tm	lotm;
	struct tm *	lotmp;
	struct tm	tm;
	struct tm *	tmp;
	char			loab[MAX_STRING_LENGTH];

	lotmp = my_localtime(&lot);
	if (lotmp != NULL) {
		lotm = *lotmp;
		strncpy(loab, abbr(&lotm), (sizeof loab) - 1);
	}
	for ( ; ; ) {
		time_t diff = hit - lot;
		if (diff < 2)
			break;
		t = lot;
		t += diff / 2;
		if (t <= lot)
			++t;
		else if (t >= hit)
			--t;
		tmp = my_localtime(&t);
		if (tmp != NULL)
			tm = *tmp;
		if ((lotmp == NULL || tmp == NULL) ? (lotmp == tmp) :
			(delta(&tm, &lotm) == (t - lot) &&
			tm.tm_isdst == lotm.tm_isdst &&
			strcmp(abbr(&tm), loab) == 0)) {
				lot = t;
				lotm = tm;
				lotmp = tmp;
		} else	hit = t;
	}
	show(name, lot, TRUE);
	show(name, hit, TRUE);
	return hit;
}

/*
** Thanks to Paul Eggert for logic used in delta.
*/

static intmax_t
delta(struct tm *newp, struct tm *oldp)
{
	intmax_t	result;
	int		tmy;

	if (newp->tm_year < oldp->tm_year)
		return -delta(oldp, newp);
	result = 0;
	for (tmy = oldp->tm_year; tmy < newp->tm_year; ++tmy)
		result += DAYSPERNYEAR + isleap_sum(tmy, TM_YEAR_BASE);
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
	if (v) {
		tmp = gmtime(&t);
		if (tmp == NULL) {
			printf(tformat(), t);
		} else {
			dumptime(tmp);
			printf(" UT");
		}
		printf(" = ");
	}
	tmp = my_localtime(&t);
	dumptime(tmp);
	if (tmp != NULL) {
		if (*abbr(tmp) != '\0')
			printf(" %s", abbr(tmp));
		if (v) {
			printf(" isdst=%d", tmp->tm_isdst);
#ifdef TM_GMTOFF
			printf(" gmtoff=%ld", tmp->TM_GMTOFF);
#endif /* defined TM_GMTOFF */
		}
	}
	printf("\n");
	if (tmp != NULL && *abbr(tmp) != '\0')
		abbrok(abbr(tmp), zone);
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

/*
** The code below can fail on certain theoretical systems;
** it works on all known real-world systems as of 2004-12-30.
*/

static const char *
tformat(void)
{
	if (0 > (time_t) -1) {		/* signed */
		if (sizeof (time_t) == sizeof (intmax_t))
			return "%"PRIdMAX;
		if (sizeof (time_t) > sizeof (long))
			return "%lld";
		if (sizeof (time_t) > sizeof (int))
			return "%ld";
		return "%d";
	}
	if (sizeof (time_t) == sizeof (uintmax_t))
		return "%"PRIuMAX;
	if (sizeof (time_t) > sizeof (unsigned long))
		return "%llu";
	if (sizeof (time_t) > sizeof (unsigned int))
		return "%lu";
	return "%u";
}

static void
dumptime(const struct tm *timeptr)
{
	static const char	wday_name[][3] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char	mon_name[][3] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	const char *	wn;
	const char *	mn;
	int		lead;
	int		trail;

	if (timeptr == NULL) {
		printf("NULL");
		return;
	}
	/*
	** The packaged versions of localtime and gmtime never put out-of-range
	** values in tm_wday or tm_mon, but since this code might be compiled
	** with other (perhaps experimental) versions, paranoia is in order.
	*/
	if (timeptr->tm_wday < 0 || timeptr->tm_wday >=
		(int) (sizeof wday_name / sizeof wday_name[0]))
			wn = "???";
	else		wn = wday_name[timeptr->tm_wday];
	if (timeptr->tm_mon < 0 || timeptr->tm_mon >=
		(int) (sizeof mon_name / sizeof mon_name[0]))
			mn = "???";
	else		mn = mon_name[timeptr->tm_mon];
	printf("%.3s %.3s%3d %.2d:%.2d:%.2d ",
		wn, mn,
		timeptr->tm_mday, timeptr->tm_hour,
		timeptr->tm_min, timeptr->tm_sec);
#define DIVISOR	10
	trail = timeptr->tm_year % DIVISOR + TM_YEAR_BASE % DIVISOR;
	lead = timeptr->tm_year / DIVISOR + TM_YEAR_BASE / DIVISOR +
		trail / DIVISOR;
	trail %= DIVISOR;
	if (trail < 0 && lead > 0) {
		trail += DIVISOR;
		--lead;
	} else if (lead < 0 && trail > 0) {
		trail -= DIVISOR;
		++lead;
	}
	if (lead == 0)
		printf("%d", trail);
	else	printf("%d%d", lead, ((trail < 0) ? -trail : trail));
}
