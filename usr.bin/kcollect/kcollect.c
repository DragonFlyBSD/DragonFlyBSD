/*
 * Copyright (c) 2017-2019 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "kcollect.h"

#include <ndbm.h>
#include <fcntl.h>
#include <errno.h>

#define SLEEP_INTERVAL	60	/* minimum is KCOLLECT_INTERVAL */
#define DATA_BASE_INDEX	8	/* up to 8 headers */

#define DISPLAY_TIME_ONLY "%H:%M:%S"
#define DISPLAY_FULL_DATE "%F %H:%M:%S"
#define HDR_BASE	"HEADER"
#define HDR_STRLEN	6

#define HDR_FMT_INDEX	0
#define HDR_FMT_TITLE	1
#define HDR_FMT_HOST	2

#define HOST_NAME_MAX sysconf(_SC_HOST_NAME_MAX)

static void format_output(uintmax_t, char, uintmax_t, char *);
static void dump_text(kcollect_t *, size_t, size_t, const char *);
static const char *get_influx_series(const char *);
static void dump_influxdb(kcollect_t *, size_t, size_t, const char *);

static void (*dumpfn)(kcollect_t *, size_t, size_t, const char *);

static void dump_dbm(kcollect_t *, size_t, const char *);
static void load_dbm(const char *datafile, kcollect_t **, size_t *);
static int rec_comparator(const void *, const void *);
static void dump_fields(kcollect_t *);
static void adjust_fields(kcollect_t *, const char *);
static void restore_headers(kcollect_t *, const char *);
static int str2unix(const char *, const char*);
static kcollect_t *load_kernel(kcollect_t *, kcollect_t *, size_t *);

FILE *OutFP;
int UseGMT;
int OutputWidth = 1024;
int OutputHeight = 1024;
int SmoothOpt;
int LoadedFromDB;
int HostnameMismatch;
int Fflag;

int
main(int ac, char **av)
{
	kcollect_t *ary_base;
	kcollect_t *ary;
	size_t bytes = 0;
	size_t count;
	size_t total_count;
	const char *datafile = NULL;
	const char *fields = NULL;
	int cmd = 't';
	int ch;
	int keepalive = 0;
	int last_ticks;
	int loops = 0;
	int maxtime = 0;
	const char *dbmFile = NULL;
	int fromFile = 0;

	OutFP = stdout;
	dumpfn = dump_text;

	sysctlbyname("kern.collect_data", NULL, &bytes, NULL, 0);
	if (bytes == 0) {
		fprintf(stderr, "kern.collect_data not available\n");
		exit(1);
	}

	while ((ch = getopt(ac, av, "o:O:b:d:r:fFlsgt:xw:GW:H:")) != -1) {
		char *suffix;

		switch(ch) {
		case 'o':
			fields = optarg;
			break;
		case 'O':
			if ((strncasecmp("influxdb", optarg, 16) == 0)) {
				dumpfn = dump_influxdb;
			} else if (strncasecmp("text", optarg, 16) == 0) {
				dumpfn = dump_text;
			} else {
				fprintf(stderr, "Bad output text format %s\n", optarg);
				exit(1);
			}
			break;
		case 'b':
			datafile = optarg;
			cmd = 'b';
			break;
		case 'd':
			dbmFile = optarg;
			fromFile = 1;
			break;
		case 'r':
			datafile = optarg;
			cmd = 'r';
			break;
		case 'f':
			keepalive = 1;
			break;
		case 'F':
			Fflag = 1;
			keepalive = 1;
			break;
		case 'l':
			cmd = 'l';
			break;
		case 's':
			SmoothOpt = 1;
			break;
		case 'w':
			datafile = optarg;
			cmd = 'w';
			break;
		case 'g':
			cmd = 'g';
			break;
		case 'x':
			cmd = 'x';
			break;
		case 't':
			maxtime = strtol(optarg, &suffix, 0);
			switch(*suffix) {
			case 'd':
				maxtime *= 24;
				/* fall through */
			case 'h':
				maxtime *= 60;
				/* fall through */
			case 'm':
				maxtime *= 60;
				break;
			case 0:
				break;
			default:
				fprintf(stderr,
					"Illegal suffix in -t option\n");
				exit(1);
			}
			break;
		case 'G':
			UseGMT = 1;
			break;
		case 'W':
			OutputWidth = strtol(optarg, NULL, 0);
			break;
		case 'H':
			OutputHeight = strtol(optarg, NULL, 0);
			break;
		default:
			fprintf(stderr, "Unknown option %c\n", ch);
			exit(1);
			/* NOT REACHED */
		}
	}
	if (cmd != 'x' && ac != optind) {
		fprintf(stderr, "Unknown argument %s\n", av[optind]);
		exit(1);
		/* NOT REACHED */
	}

	total_count = 0;
	last_ticks = 0;

	if (cmd == 'x' || cmd == 'w')
		start_gnuplot(ac - optind, av + optind, datafile);

	do {
		/*
		 * We do not allow keepalive if there is a hostname
		 * mismatch, there is no point in showing data for the
		 * current host after dumping the data from another one.
		 */
		if (HostnameMismatch) {
			fprintf(stderr,
			    "Hostname mismatch, can't show live data\n");
			exit(1);
		}

		/*
		 * Snarf as much data as we can.  If we are looping,
		 * snarf less (no point snarfing stuff we already have).
		 */
		bytes = 0;
		sysctlbyname("kern.collect_data", NULL, &bytes, NULL, 0);
		if (cmd == 'l')
			bytes = sizeof(kcollect_t) * 2;

		/* Skip to the newest entries */
		if (Fflag && loops == 0)
			loops++;

		if (loops) {
			size_t loop_bytes;

			loop_bytes = sizeof(kcollect_t) *
				     (4 + SLEEP_INTERVAL / KCOLLECT_INTERVAL);
			if (bytes > loop_bytes)
				bytes = loop_bytes;
		}

		/*
		 * If we got specified a file to load from: replace the data
		 * array and counter
		 */
		if (fromFile) {
			kcollect_t *dbmAry = NULL;

			load_dbm(dbmFile, &dbmAry, &count);
			ary = ary_base = dbmAry;
		} else {
			kcollect_t scaleid[2];

			ary_base = malloc(bytes +
					  DATA_BASE_INDEX * sizeof(kcollect_t));
			ary = ary_base + DATA_BASE_INDEX;
			sysctlbyname("kern.collect_data", ary, &bytes, NULL, 0);
			count = bytes / sizeof(kcollect_t);
			if (count < 2) {
				fprintf(stderr,
					"[ERR] kern.collect_data failed\n");
				exit(1);
			}
			scaleid[0] = ary[0];
			scaleid[1] = ary[1];
			count -= 2;
			ary = load_kernel(scaleid, ary + 2, &count);
		}
		if (fields)
			adjust_fields(&ary[1], fields);


		/*
		 * Delete duplicate entries when looping
		 */
		if (loops) {
			while (count > DATA_BASE_INDEX) {
				if ((int)(ary[count-1].ticks - last_ticks) > 0)
					break;
				--count;
			}
		}

		/*
		 * Delete any entries beyond the time limit
		 */
		if (maxtime) {
			maxtime *= ary[0].hz;
			while (count > DATA_BASE_INDEX) {
				if ((int)(ary[0].ticks - ary[count-1].ticks) <
				    maxtime) {
					break;
				}
				--count;
			}
		}

		switch(cmd) {
		case 't':
			if (count > DATA_BASE_INDEX) {
				dumpfn(ary, count, total_count,
					  (fromFile ? DISPLAY_FULL_DATE :
						      DISPLAY_TIME_ONLY));
			}
			break;
		case 'b':
			if (HostnameMismatch) {
				fprintf(stderr,
				    "Hostname mismatch, cannot save to DB\n");
				exit(1);
			} else {
				if (count > DATA_BASE_INDEX)
					dump_dbm(ary, count, datafile);
			}
			break;
		case 'r':
			if (count >= DATA_BASE_INDEX)
				restore_headers(ary, datafile);
			break;
		case 'l':
			dump_fields(ary);
			exit(0);
			break;		/* NOT REACHED */
		case 'g':
			if (count > DATA_BASE_INDEX)
				dump_gnuplot(ary, count);
			break;
		case 'w':
			if (count >= DATA_BASE_INDEX)
				dump_gnuplot(ary, count);
			break;
		case 'x':
			if (count > DATA_BASE_INDEX)
				dump_gnuplot(ary, count);
			break;
		}
		if (keepalive && !fromFile) {
			fflush(OutFP);
			fflush(stdout);
			switch(cmd) {
			case 't':
				sleep(1);
				break;
			case 'x':
			case 'g':
			case 'w':
				sleep(60);
				break;
			default:
				sleep(10);
				break;
			}
		}
		last_ticks = ary[DATA_BASE_INDEX].ticks;
		if (count >= DATA_BASE_INDEX)
			total_count += count - DATA_BASE_INDEX;

		/*
		 * Loop for incremental aquisition.  When outputting to
		 * gunplot, we have to send the whole data-set again so
		 * do not increment loops in that case.
		 */
		if (cmd != 'g' && cmd != 'x' && cmd != 'w')
			++loops;

		free(ary_base);
	} while (keepalive);

	if (cmd == 'x')
		pclose(OutFP);
}

static
void
format_output(uintmax_t value,char fmt,uintmax_t scale, char* ret)
{
	char buf[9];

	switch(fmt) {
	case '2':
		/*
		 * fractional x100
		 */
		sprintf(ret, "%5ju.%02ju",
			value / 100, value % 100);
		break;
	case 'p':
		/*
		 * Percentage fractional x100 (100% = 10000)
		 */
		sprintf(ret,"%4ju.%02ju%%",
			value / 100, value % 100);
		break;
	case 'm':
		/*
		 * Megabytes
		 */
		humanize_number(buf, sizeof(buf), value, "",
				2,
				HN_FRACTIONAL |
				HN_NOSPACE);
		sprintf(ret,"%8.8s", buf);
		break;
	case 'c':
		/*
		 * Raw count over period (this is not total)
		 */
		humanize_number(buf, sizeof(buf), value, "",
				HN_AUTOSCALE,
				HN_FRACTIONAL |
				HN_NOSPACE |
				HN_DIVISOR_1000);
		sprintf(ret,"%8.8s", buf);
		break;
	case 'b':
		/*
		 * Total bytes (this is a total), output
		 * in megabytes.
		 */
		if (scale > 100000000) {
			humanize_number(buf, sizeof(buf),
					value, "",
					3,
					HN_FRACTIONAL |
					HN_NOSPACE);
		} else {
			humanize_number(buf, sizeof(buf),
					value, "",
					2,
					HN_FRACTIONAL |
					HN_NOSPACE);
		}
		sprintf(ret,"%8.8s", buf);
		break;
	default:
		sprintf(ret,"%s","        ");
		break;
	}
}

static
const char *
get_influx_series(const char *val)
{
	/* cpu values (user, idle, syst) */
	if ((strncmp("user", val, 8) == 0) ||
	    (strncmp("idle", val, 8) == 0 ) ||
	    (strncmp("syst", val, 8) == 0))
		return "cpu_value";

	/* load value (load) */
	if (strncmp("load", val, 8) == 0)
		return "load_value";

	/* swap values (swapuse, swapano, swapcac) */
	if ((strncmp("swapuse", val, 8) == 0) ||
	    (strncmp("swapano", val, 8) == 0 ) ||
	    (strncmp("swapcac", val, 8) == 0))
		return "swap_value";

	/* vm values (fault, cow, zfill) */
	if ((strncmp("fault", val, 8) == 0) ||
	    (strncmp("cow", val, 8) == 0 ) ||
	    (strncmp("zfill", val, 8) == 0))
		return "vm_value";

	/* memory values (fault, cow, zfill) */
	if ((strncmp("cache", val, 8) == 0) ||
	    (strncmp("inact", val, 8) == 0 ) ||
	    (strncmp("act", val, 8) == 0) ||
	    (strncmp("wired", val, 8) == 0) ||
	    (strncmp("free", val, 8) == 0))
		return "memory_value";

	/* misc values (syscalls, nlookup, intr, ipi, timer) */
	if ((strncmp("syscalls", val, 8) == 0) ||
	    (strncmp("nlookup", val, 8) == 0 ) ||
	    (strncmp("intr", val, 8) == 0) ||
	    (strncmp("ipi", val, 8) == 0) ||
	    (strncmp("timer", val, 8) == 0))
		return "misc_value";

	return "misc_value";

}

static
void
dump_influxdb(kcollect_t *ary, size_t count, size_t total_count,
	  __unused const char* display_fmt)
{
	int j;
	int i;
	uintmax_t value;
	size_t ts_nsec;
	char hostname[HOST_NAME_MAX];
	char *colname;

	if (LoadedFromDB) {
		snprintf(hostname, HOST_NAME_MAX, "%s", (char *)ary[2].data);
	} else {
		if (gethostname(hostname, HOST_NAME_MAX) != 0) {
			fprintf(stderr, "Failed to get hostname\n");
			exit(1);
		}
	}

	for (i = count - 1; i >= DATA_BASE_INDEX; --i) {
		/*
		 * Timestamp
		 */
		ts_nsec = (ary[i].realtime.tv_sec
		    * 1000 /* ms */
		    * 1000 /* usec */
		    * 1000 /* nsec */
		    + 123  /* add a few ns since due to missing precision */);
		ts_nsec += (ary[i].realtime.tv_usec * 1000);

		for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
			if (ary[1].data[j] == 0)
				continue;

			/*
			 * NOTE: kernel does not have to provide the scale
			 *	 (that is, the highest likely value), nor
			 *	 does it make sense in all cases.
			 *
			 *       But should we since we're using raw values?
			 */
			value = ary[i].data[j];
			colname = (char *)&ary[1].data[j];

			printf("%s,host=%s,type=%.8s value=%jd %jd\n",
			    get_influx_series(colname),
			    hostname, colname, value, ts_nsec);
		}
		printf("\n");
		++total_count;
	}

}

static
void
dump_text(kcollect_t *ary, size_t count, size_t total_count,
	  const char* display_fmt)
{
	int j;
	int i;
	uintmax_t scale;
	uintmax_t value;
	char fmt;
	char sbuf[20];
	struct tm *tmv;
	time_t t;

	for (i = count - 1; i >= DATA_BASE_INDEX; --i) {
		if ((total_count & 15) == 0) {
			if (!strcmp(display_fmt, DISPLAY_FULL_DATE)) {
				printf("%20s", "timestamp ");
			} else {
				printf("%8.8s", "time");
			}
			for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
				if (ary[1].data[j]) {
					printf(" %8.8s",
						(char *)&ary[1].data[j]);
				}
			}
			printf("\n");
		}

		/*
		 * Timestamp
		 */
		t = ary[i].realtime.tv_sec;
		if (UseGMT)
			tmv = gmtime(&t);
		else
			tmv = localtime(&t);
		strftime(sbuf, sizeof(sbuf), display_fmt, tmv);
		printf("%8s", sbuf);

		for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
			if (ary[1].data[j] == 0)
				continue;

			/*
			 * NOTE: kernel does not have to provide the scale
			 *	 (that is, the highest likely value), nor
			 *	 does it make sense in all cases.
			 *
			 *	 Example scale - kernel provides total amount
			 *	 of memory available for memory related
			 *	 statistics in the scale field.
			 */
			value = ary[i].data[j];
			scale = KCOLLECT_GETSCALE(ary[0].data[j]);
			fmt = KCOLLECT_GETFMT(ary[0].data[j]);

			printf(" ");

			format_output(value, fmt, scale, sbuf);
			printf("%s",sbuf);
		}

		printf("\n");
		++total_count;
	}
}

/* restores the DBM database header records to current machine */
static
void
restore_headers(kcollect_t *ary, const char *datafile)
{
	DBM *db;
	char hdr[32];
	datum key, value;
	int i;

	db = dbm_open(datafile, (O_RDWR),
		      (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP));

	if (db == NULL) {
		switch (errno) {
		case EACCES:
			fprintf(stderr,
				"[ERR] database file \"%s\" is read-only, "
				"check permissions. (%i)\n",
				datafile, errno);
			break;
		default:
			fprintf(stderr,
				"[ERR] opening our database file \"%s\" "
				"produced an error. (%i)\n",
				datafile, errno);
		}
		exit(EXIT_FAILURE);
	} else {
		for (i = 0; i < DATA_BASE_INDEX; ++i) {
			snprintf(hdr, sizeof(hdr), "%s%d", HDR_BASE, i);
			key.dptr = hdr;
			key.dsize = strlen(key.dptr) + 1;
			value.dptr = &ary[i].data;
			value.dsize = sizeof(uint64_t) * KCOLLECT_ENTRIES;
			if (dbm_store(db,key,value,DBM_REPLACE) == -1) {
				fprintf(stderr,
					"[ERR] error storing the value in "
					"the database file \"%s\" (%i)\n",
					datafile, errno);
				dbm_close(db);
				exit(EXIT_FAILURE);
			}
		}
	}
	dbm_close(db);
}


/*
 * Store the array of kcollect_t records in a dbm db database,
 * path passed in datafile
 */
static
void
dump_dbm(kcollect_t *ary, size_t count, const char *datafile)
{
	DBM * db;

	db = dbm_open(datafile, (O_RDWR | O_CREAT),
		      (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP));
	if (db == NULL) {
		switch (errno) {
		case EACCES:
			fprintf(stderr,
				"[ERR] database file \"%s\" is read-only, "
				"check permissions. (%i)\n",
				datafile, errno);
			break;
		default:
			fprintf(stderr,
				"[ERR] opening our database file \"%s\" "
				"produced an error. (%i)\n",
				datafile, errno);
		}
		exit(EXIT_FAILURE);
	} else {
		struct tm *tmv;
		char buf[20];
		datum key;
		datum value;
		time_t t;
		uint i;
		int cmd;
		char hdr[32];

		for (i = 0; i < count; ++i) {
			/*
			 * The first DATA_BASE_INDEX records are special.
			 */
			cmd = DBM_INSERT;
			if (i < DATA_BASE_INDEX) {
				snprintf(hdr, sizeof(hdr), "%s%d", HDR_BASE, i);
				key.dptr = hdr;
				key.dsize = strlen(hdr) + 1;

				value = dbm_fetch(db, key);
				if (value.dptr == NULL ||
				    bcmp(ary[i].data, value.dptr,
					 sizeof(uint64_t) * KCOLLECT_ENTRIES)) {
					cmd = DBM_REPLACE;
					if (value.dptr != NULL) {
						fprintf(stderr,
							"Header %d changed "
							"in database, "
							"updating\n",
							i);
					}
				}
			} else {
				t = ary[i].realtime.tv_sec;
				if (LoadedFromDB)
					tmv = localtime(&t);
				else
					tmv = gmtime(&t);
				strftime(buf, sizeof(buf),
					 DISPLAY_FULL_DATE, tmv);
				key.dptr = buf;
				key.dsize = sizeof(buf);
			}
			value.dptr = ary[i].data;
			value.dsize = sizeof(uint64_t) * KCOLLECT_ENTRIES;
			if (dbm_store(db, key, value, cmd) == -1) {
				fprintf(stderr,
					"[ERR] error storing the value in "
					"the database file \"%s\" (%i)\n",
					datafile, errno);
				dbm_close(db);
				exit(EXIT_FAILURE);
			}

		}
		dbm_close(db);
	}
}

/*
 * Transform a string (str) matching a format string (fmt) into a unix
 * timestamp and return it used by load_dbm()
 */
static
int
str2unix(const char* str, const char* fmt){
	struct tm tm;
	time_t ts;

	/*
	 * Reset all the fields because strptime only sets what it
	 * finds, which may lead to undefined members
	 */
	memset(&tm, 0, sizeof(struct tm));
	strptime(str, fmt, &tm);
	ts = timegm(&tm);

	return (int)ts;
}

/*
 * Sorts the ckollect_t records by time, to put youngest first,
 * so desc by timestamp used by load_dbm()
 */
static
int
rec_comparator(const void *c1, const void *c2)
{
	const kcollect_t *k1 = (const kcollect_t*)c1;
	const kcollect_t *k2 = (const kcollect_t*)c2;

	if (k1->realtime.tv_sec < k2->realtime.tv_sec)
		return -1;
	if (k1->realtime.tv_sec > k2->realtime.tv_sec)
		return 1;
	return 0;
}

/*
 * Normalizes kcollect records from the kernel.  We reserve the first
 * DATA_BASE_INDEX elements for specific headers.
 */
static
kcollect_t *
load_kernel(kcollect_t *scaleid, kcollect_t *ary, size_t *counter)
{
	ary -= DATA_BASE_INDEX;
	bzero(ary, sizeof(*ary) * DATA_BASE_INDEX);
	ary[0] = scaleid[0];
	ary[1] = scaleid[1];

	/*
	 * Add host field
	 */
	gethostname((char *)ary[2].data,
		    sizeof(uint64_t) * KCOLLECT_ENTRIES - 1);

	*counter += DATA_BASE_INDEX;

	return ary;
}

/*
 * Loads the kcollect records from a dbm DB database specified in datafile.
 * returns the resulting array in ret_ary and the array counter in counter
 */
static
void
load_dbm(const char* datafile, kcollect_t **ret_ary,
	 size_t *counter)
{
	char hostname[sizeof(uint64_t) * KCOLLECT_ENTRIES];
	DBM *db = dbm_open(datafile,(O_RDONLY),(S_IRUSR|S_IRGRP));
	datum key;
	datum value;
	size_t recCounter = DATA_BASE_INDEX;
	int headersFound = 0;
	uint c;
	uint sc;

	if (db == NULL) {
		fprintf(stderr,
			"[ERR] opening our database \"%s\" produced "
			"an error! (%i)\n",
			datafile, errno);
		exit(EXIT_FAILURE);
	}

	/* counting loop */
	for (key = dbm_firstkey(db); key.dptr; key = dbm_nextkey(db)) {
		value = dbm_fetch(db, key);
		if (value.dptr == NULL)
			continue;
		if (strncmp(key.dptr, HDR_BASE, HDR_STRLEN) == 0)
			continue;
		recCounter++;
	}

	/* with the count allocate enough memory */
	if (*ret_ary)
		free(*ret_ary);

	*ret_ary = malloc(sizeof(kcollect_t) * recCounter);

	if (*ret_ary == NULL) {
		fprintf(stderr,
			"[ERR] failed to allocate enough memory to "
			"hold the database! Aborting.\n");
		dbm_close(db);
		exit(EXIT_FAILURE);
	}
	bzero(*ret_ary, sizeof(kcollect_t) * recCounter);

	/*
	 * Actual data retrieval  but only of recCounter
	 * records
	 */
	c = DATA_BASE_INDEX;
	key = dbm_firstkey(db);
	while (key.dptr && c < recCounter) {
		value = dbm_fetch(db, key);
		if (value.dptr == NULL) {
			key = dbm_nextkey(db);
			continue;
		}
		if (!strncmp(key.dptr, HDR_BASE, HDR_STRLEN)) {
			/*
			 * Ignore unsupported header indices
			 */
			sc = strtoul((char *)key.dptr +
				     HDR_STRLEN, NULL, 10);
			if (sc >= DATA_BASE_INDEX) {
				key = dbm_nextkey(db);
				continue;
			}
			headersFound |= 1 << sc;
		} else {
			sc = c++;
			(*ret_ary)[sc].realtime.tv_sec =
				str2unix(key.dptr,
					 DISPLAY_FULL_DATE);
		}
		memcpy((*ret_ary)[sc].data,
		       value.dptr,
		       sizeof(uint64_t) * KCOLLECT_ENTRIES);

		key = dbm_nextkey(db);
	}

	/*
	 * HEADER2 - hostname (must match)
	 */
	if ((headersFound & 4) && *(char *)(*ret_ary)[2].data == 0)
		headersFound &= ~4;

	bzero(hostname, sizeof(hostname));
	gethostname(hostname, sizeof(hostname) - 1);

	if (headersFound & 0x0004) {
		if (*(char *)(*ret_ary)[2].data &&
		    strcmp(hostname, (char *)(*ret_ary)[2].data) != 0) {
			HostnameMismatch = 1;	/* Disable certain options */
		}
	}

	/*
	 * Set the counter,
	 * and sort the non-header records.
	 */
	*counter = recCounter;
        qsort(&(*ret_ary)[DATA_BASE_INDEX], recCounter - DATA_BASE_INDEX,
	      sizeof(kcollect_t), rec_comparator);
	dbm_close(db);

	if ((headersFound & 3) != 3) {
		fprintf(stderr, "We could not retrieve all necessary headers, "
			"might be the database file is corrupted? (%i)\n",
			headersFound);
		exit(EXIT_FAILURE);
	}
	LoadedFromDB = 1;
}

static void
dump_fields(kcollect_t *ary)
{
	int j;

	for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
		if (ary[1].data[j] == 0)
			continue;
		printf("%8.8s %c\n",
		       (char *)&ary[1].data[j],
		       KCOLLECT_GETFMT(ary[0].data[j]));
	}
}

static void
adjust_fields(kcollect_t *ent, const char *fields)
{
	char *copy = strdup(fields);
	char *word;
	int selected[KCOLLECT_ENTRIES];
	int j;

	bzero(selected, sizeof(selected));

	word = strtok(copy, ", \t");
	while (word) {
		for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
			if (strncmp(word, (char *)&ent->data[j], 8) == 0) {
				selected[j] = 1;
				break;
			}
		}
		word = strtok(NULL, ", \t");
	}
	free(copy);
	for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
		if (!selected[j])
			ent->data[j] = 0;
	}
}
