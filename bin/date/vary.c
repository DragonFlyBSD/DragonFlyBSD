/*-
 * Copyright (c) 1997 Brian Somers <brian@Awfulhak.org>
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
 * $FreeBSD: src/bin/date/vary.c,v 1.16 2004/08/09 13:43:39 yar Exp $
 * $DragonFly: src/bin/date/vary.c,v 1.4 2005/07/20 06:10:51 cpressey Exp $
 */

#include <err.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include "vary.h"

struct trans {
  long val;
  const char *str;
};

static struct trans trans_mon[] = {
  { 1, "january" }, { 2, "february" }, { 3, "march" }, { 4, "april" },
  { 5, "may"}, { 6, "june" }, { 7, "july" }, { 8, "august" },
  { 9, "september" }, { 10, "october" }, { 11, "november" }, { 12, "december" },
  { -1, NULL }
};

static struct trans trans_wday[] = {
  { 0, "sunday" }, { 1, "monday" }, { 2, "tuesday" }, { 3, "wednesday" },
  { 4, "thursday" }, { 5, "friday" }, { 6, "saturday" },
  { -1, NULL }
};

static int adjhour(struct tm *, char, long, int);

static int
domktime(struct tm *t, char type)
{
  time_t ret;

  while ((ret = mktime(t)) == -1 && t->tm_year > 68 && t->tm_year < 138)
    /* While mktime() fails, adjust by an hour */
    adjhour(t, type == '-' ? type : '+', 1, 0);

  return ret;
}

static int
trans(const struct trans t[], const char *arg)
{
  int f;

  for (f = 0; t[f].val != -1; f++)
    if (!strncasecmp(t[f].str, arg, 3) ||
        !strncasecmp(t[f].str, arg, strlen(t[f].str)))
      return t[f].val;

  return -1;
}

struct vary *
vary_append(struct vary *v, char *arg)
{
  struct vary *result, **nextp;

  if (v) {
    result = v;
    while (v->next)
      v = v->next;
    nextp = &v->next;
  } else
    nextp = &result;

  if ((*nextp = (struct vary *)malloc(sizeof(struct vary))) == NULL)
    err(1, "malloc");
  (*nextp)->arg = arg;
  (*nextp)->next = NULL;
  return result;
}

static int mdays[12] = { 31, 0, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static int
daysinmonth(const struct tm *t)
{
  int year;

  year = t->tm_year + 1900;

  if (t->tm_mon == 1)
    if (!(year % 400))
      return 29;
    else if (!(year % 100))
      return 28;
    else if (!(year % 4))
      return 29;
    else
      return 28;
  else if (t->tm_mon >= 0 && t->tm_mon < 12)
    return mdays[t->tm_mon];

  return 0;
}


static int
adjyear(struct tm *t, char type, long val, int mk)
{
  switch (type) {
    case '+':
      t->tm_year += val;
      break;
    case '-':
      t->tm_year -= val;
      break;
    default:
      t->tm_year = val;
      if (t->tm_year < 69)
      	t->tm_year += 100;		/* as per date.c */
      else if (t->tm_year > 1900)
        t->tm_year -= 1900;             /* struct tm holds years since 1900 */
      break;
  }
  return !mk || domktime(t, type) != -1;
}

static int
adjmon(struct tm *t, char type, long val, int istext, int mk)
{
  int lmdays;
  if (val < 0)
    return 0;

  switch (type) {
    case '+':
      if (istext) {
        if (val <= t->tm_mon)
          val += 11 - t->tm_mon;	/* early next year */
        else
          val -= t->tm_mon + 1;		/* later this year */
      }
      if (val) {
        if (!adjyear(t, '+', (t->tm_mon + val) / 12, 0))
          return 0;
        val %= 12;
        t->tm_mon += val;
        if (t->tm_mon > 11)
          t->tm_mon -= 12;
      }
      break;

    case '-':
      if (istext) {
        if (val-1 > t->tm_mon)
          val = 13 - val + t->tm_mon;	/* later last year */
        else
          val = t->tm_mon - val + 1;	/* early this year */
      }
      if (val) {
        if (!adjyear(t, '-', val / 12, 0))
          return 0;
        val %= 12;
        if (val > t->tm_mon) {
          if (!adjyear(t, '-', 1, 0))
            return 0;
          val -= 12;
        }
        t->tm_mon -= val;
      }
      break;

    default:
      if (val > 12 || val < 1)
        return 0;
      t->tm_mon = --val;
  }

  /* e.g., -v-1m on March, 31 is the last day of February in common sense */
  lmdays = daysinmonth(t);
  if (t->tm_mday > lmdays)
    t->tm_mday = lmdays;

  return !mk || domktime(t, type) != -1;
}

static int
adjday(struct tm *t, char type, long val, int mk)
{
  int daycount;

  switch (type) {
    case '+':
      while (val) {
        daycount = daysinmonth(t);
        if (val > daycount - t->tm_mday) {
          val -= daycount - t->tm_mday + 1;
          t->tm_mday = 1;
          if (!adjmon(t, '+', 1, 0, 0))
            return 0;
        } else {
          t->tm_mday += val;
          val = 0;
        }
      }
      break;
    case '-':
      while (val)
        if (val >= t->tm_mday) {
          val -= t->tm_mday;
          t->tm_mday = 1;
          if (!adjmon(t, '-', 1, 0, 0))
            return 0;
          t->tm_mday = daysinmonth(t);
        } else {
          t->tm_mday -= val;
          val = 0;
        }
      break;
    default:
      if (val > 0 && val <= daysinmonth(t))
        t->tm_mday = val;
      else
        return 0;
      break;
  }

  return !mk || domktime(t, type) != -1;
}

static int
adjwday(struct tm *t, char type, long val, int istext, int mk)
{
  if (val < 0)
    return 0;

  switch (type) {
    case '+':
      if (istext)
        if (val < t->tm_wday)
          val = 7 - t->tm_wday + val;  /* early next week */
        else
          val -= t->tm_wday;           /* later this week */
      else
        val *= 7;                      /* "-v+5w" == "5 weeks in the future" */
      return !val || adjday(t, '+', val, mk);
    case '-':
      if (istext) {
        if (val > t->tm_wday)
          val = 7 - val + t->tm_wday;  /* later last week */
        else
          val = t->tm_wday - val;      /* early this week */
      } else
        val *= 7;                      /* "-v-5w" == "5 weeks ago" */
      return !val || adjday(t, '-', val, mk);
    default:
      if (val < t->tm_wday)
        return adjday(t, '-', t->tm_wday - val, mk);
      else if (val > 6)
        return 0;
      else if (val > t->tm_wday)
        return adjday(t, '+', val - t->tm_wday, mk);
  }
  return 1;
}

static int
adjhour(struct tm *t, char type, long val, int mk)
{
  if (val < 0)
    return 0;

  switch (type) {
    case '+':
      if (val) {
        int days;

        days = (t->tm_hour + val) / 24;
        val %= 24;
        t->tm_hour += val;
        t->tm_hour %= 24;
        if (!adjday(t, '+', days, 0))
          return 0;
      }
      break;

    case '-':
      if (val) {
        int days;

        days = val / 24;
        val %= 24;
        if (val > t->tm_hour) {
          days++;
          val -= 24;
        }
        t->tm_hour -= val;
        if (!adjday(t, '-', days, 0))
          return 0;
      }
      break;

    default:
      if (val > 23)
        return 0;
      t->tm_hour = val;
  }

  return !mk || domktime(t, type) != -1;
}

static int
adjmin(struct tm *t, char type, long val, int mk)
{
  if (val < 0)
    return 0;

  switch (type) {
    case '+':
      if (val) {
        if (!adjhour(t, '+', (t->tm_min + val) / 60, 0))
          return 0;
        val %= 60;
        t->tm_min += val;
        if (t->tm_min > 59)
          t->tm_min -= 60;
      }
      break;

    case '-':
      if (val) {
        if (!adjhour(t, '-', val / 60, 0))
          return 0;
        val %= 60;
        if (val > t->tm_min) {
          if (!adjhour(t, '-', 1, 0))
            return 0;
          val -= 60;
        }
        t->tm_min -= val;
      }
      break;

    default:
      if (val > 59)
        return 0;
      t->tm_min = val;
  }

  return !mk || domktime(t, type) != -1;
}

static int
adjsec(struct tm *t, char type, long val, int mk)
{
  if (val < 0)
    return 0;

  switch (type) {
    case '+':
      if (val) {
        if (!adjmin(t, '+', (t->tm_sec + val) / 60, 0))
          return 0;
        val %= 60;
        t->tm_sec += val;
        if (t->tm_sec > 59)
          t->tm_sec -= 60;
      }
      break;

    case '-':
      if (val) {
        if (!adjmin(t, '-', val / 60, 0))
          return 0;
        val %= 60;
        if (val > t->tm_sec) {
          if (!adjmin(t, '-', 1, 0))
            return 0;
          val -= 60;
        }
        t->tm_sec -= val;
      }
      break;

    default:
      if (val > 59)
        return 0;
      t->tm_sec = val;
  }

  return !mk || domktime(t, type) != -1;
}

/*
 * WARNING!  Severely deficient relative to gnu date's -d option
 *
 * - timezone handling is all wrong (see also TZ= test in date.c)
 * - doesn't check for 'Z' suffix
 * - doesn't handle a multitude of formats that gnu date handles
 * - is generally a mess
 */

const struct vary *
vary_apply(const struct vary *vb, time_t tval, struct tm *t)
{
  const struct vary *v;
  char type;
  char *arg;
  char *tmp;
  size_t len;
  long val;

  *t = *localtime(&tval);

  for (v = vb; v; v = v->next) {
    //int domonth = 1;
    //int doday = 1;
    int dohms = 1;
    int doyear = 1;
    int dotzone = 1;

    arg = v->arg;

    type = *arg;

    /*
     * Unix time stamp
     */
    if (type == '@') {
      time_t tt;
      tt = strtoul(arg + 1, NULL, 10);
      *t = *gmtime(&tt);
      continue;
    }

    /*
     * Delta verses absolute
     */
    if (type == '+' || type == '-') {
      arg++;
    } else if (strncmp(arg, "next", 4) == 0) {
      type = '+';
      arg += 4;
    } else if (strncmp(arg, "last", 4) == 0) {
      type = '-';
      arg += 4;
    } else {
      type = '\0';
    }

    /*
     * At least 2 chars
     */
    while (isspace(arg[0]))
	++arg;
    len = strlen(arg);
    if (len < 2)
      return v;

    /*
     * Reset dst calculation for absolute specifications so
     * it gets recalculated.
     */
    if (type == '\0')
      t->tm_isdst = -1;

    /*
     * N{S,M,H,m,d,y,w}
     */
    val = strtoul(arg, &tmp, 10);
    if (tmp != arg && isalpha(tmp[0])) {
      if (strcmp(tmp, "S") == 0 || strncmp(tmp, "sec", 3) == 0) {
	  if (!adjsec(t, type, val, 1))
	    return v;
      } else if (strcmp(tmp, "M") == 0 || strncmp(tmp, "min", 3) == 0) {
	  if (!adjmin(t, type, val, 1))
	    return v;
      } else if (strcmp(tmp, "H") == 0 || strncmp(tmp, "hour", 4) == 0) {
	  if (!adjhour(t, type, val, 1))
	    return v;
      } else if (strcmp(tmp, "d") == 0 || strncmp(tmp, "day", 3) == 0) {
	  t->tm_isdst = -1;
	  if (!adjday(t, type, val, 1))
	    return v;
      } else if (strcmp(tmp, "w") == 0 || strncmp(tmp, "week", 4) == 0) {
	  t->tm_isdst = -1;
	  if (!adjwday(t, type, val, 0, 1))
	    return v;
      } else if (strcmp(tmp, "m") == 0 || strncmp(tmp, "mon", 3) == 0) {
	  t->tm_isdst = -1;
	  if (!adjmon(t, type, val, 0, 1))
	    return v;
      } else if (strcmp(tmp, "y") == 0 || strncmp(tmp, "year", 4) == 0) {
	  t->tm_isdst = -1;
	  if (!adjyear(t, type, val, 1))
	    return v;
      } else {
	  return v;
      }
      continue;
    }

    /*
     * wdayname
     * monthname
     * [wdayname[,]] monthname day h:m:s tzone yyyy
     * [wdayname[,]] monthname day h:m:s YYYY tzone
     * year-month-day h:m:s[Z]
     */

    /*
     * Weekday_string
     */
    if ((val = trans(trans_wday, arg)) != -1) {
	if (!adjwday(t, type, val, 1, 1))
	  return v;
	while (isalpha(*arg))
	  ++arg;
	while (isspace(arg[0]))
	    ++arg;
    }

    /*
     * Month_string [day ]
     */
    if ((val = trans(trans_mon, arg)) != -1) {
	if (!adjmon(t, type, val, 1, 1))
	  return v;
	//domonth = 0;
	while (isalpha(*arg))
	  ++arg;
	while (isspace(*arg))
	  ++arg;

	val = strtoul(arg, &tmp, 10);
	if (tmp != arg && (isspace(*tmp) || *tmp == 0)) {
	  t->tm_isdst = -1;
	  if (!adjday(t, 0, val, 1))
	    return v;
	  arg = tmp;
	  //doday = 0;
	  while (isspace(arg[0]))
	    ++arg;
	}
    }

    /*
     * h:m:s or year
     * year or h:m:s
     */
    if (doyear) {
      val = strtol(arg, &tmp, 10);
      if (tmp != arg && (*tmp == 0 || isspace(*tmp))) {
	if (!adjyear(t, type, val, 1))
	  return v;
	arg = tmp;
	doyear = 0;
	while (isspace(arg[0]))
	    ++arg;
      }
    }

    val = strtol(arg, &tmp, 10);
    if (dohms && tmp != arg && *tmp == ':') {
      int hr = -1;
      int mi = -1;
      int se = -1;
      char zflag = 0;

      sscanf(arg, "%d:%d:%d%c", &hr, &mi, &se, &zflag);
      if (hr >= 0) {
	if (!adjhour(t, type, hr, 1))
	  return v;
      }
      if (mi >= 0) {
	if (!adjmin(t, type, mi, 1))
	  return v;
      }
      if (se >= 0) {
	if (!adjsec(t, type, se, 1))
	  return v;
      }
      arg = tmp;
      dohms = 0;
      if (zflag) {
	/* XXX */
      }
      while (arg[0] && !isspace(arg[0]))
	  ++arg;
      while (isspace(arg[0]))
	  ++arg;
    }

    if (doyear) {
      val = strtol(arg, &tmp, 10);
      if (tmp != arg && *tmp == 0) {
	if (!adjyear(t, type, val, 1))
	  return v;
	arg = tmp;
	doyear = 0;
	while (isspace(arg[0]))
	    ++arg;
      }
    }

    if (dotzone) {
	/* XXX */
    }


#if 0
    /*
     * Date adjustment
     */
    val = strtol(arg, NULL, 10);
    which = arg[len-1];
#endif

  }
  return 0;
}

void
vary_destroy(struct vary *v)
{
  struct vary *n;

  while (v) {
    n = v->next;
    free(v);
    v = n;
  }
}
