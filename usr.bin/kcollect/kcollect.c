/*
 * Copyright (c) 2017 The DragonFly Project.  All rights reserved.
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

#define DISPLAY_TIME_ONLY "%H:%M:%S"
#define DISPLAY_FULL_DATE "%F %H:%M:%S"
#define HDR_FMT "HEADER0"
#define HDR_TITLE "HEADER1"

static void format_output(uintmax_t value,char fmt,uintmax_t scale, char* ret);
static void dump_text(kcollect_t *ary, size_t count,
			size_t total_count, const char* display_fmt);
static void dump_dbm(kcollect_t *ary, size_t count, const char *datafile);
static int str2unix(const char* str, const char* fmt);
static int rec_comparator(const void *c1, const void *c2);
static void load_dbm(const char *datafile,
			kcollect_t **ret_ary, size_t *counter);
static void dump_fields(kcollect_t *ary);
static void adjust_fields(kcollect_t *ent, const char *fields);
static void restore_headers(kcollect_t *ary, const char *datafile);

FILE *OutFP;
int UseGMT;
int OutputWidth = 1024;
int OutputHeight = 1024;
int SmoothOpt;
int LoadedFromDB = 0;
int Fflag = 0;

int
main(int ac, char **av)
{
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

	kcollect_t *dbmAry = NULL;
	const char *dbmFile = NULL;
	int fromFile = 0;

	OutFP = stdout;

	sysctlbyname("kern.collect_data", NULL, &bytes, NULL, 0);
	if (bytes == 0) {
		fprintf(stderr, "kern.collect_data not available\n");
		exit(1);
	}

	while ((ch = getopt(ac, av, "o:b:d:r:fFlsgt:xw:GW:H:")) != -1) {
		char *suffix;

		switch(ch) {
		case 'o':
			fields = optarg;
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
		 * Snarf as much data as we can.  If we are looping,
		 * snarf less (no point snarfing stuff we already have).
		 */
		bytes = 0;
		sysctlbyname("kern.collect_data", NULL, &bytes, NULL, 0);
		if (cmd == 'l')
			bytes = sizeof(kcollect_t) * 2;

		if (Fflag && loops == 0)
			loops++;

		if (loops) {
			size_t loop_bytes;

			loop_bytes = sizeof(kcollect_t) *
				     (4 + SLEEP_INTERVAL / KCOLLECT_INTERVAL);
			if (bytes > loop_bytes)
				bytes = loop_bytes;
		}

		ary = malloc(bytes);
		sysctlbyname("kern.collect_data", ary, &bytes, NULL, 0);
		count = bytes / sizeof(kcollect_t);

		/*
		 * If we got specified a file to load from: replace the data
		 * array and counter
		 */
		if (fromFile) {
			load_dbm(dbmFile, &dbmAry, &count);
			free(ary);
			ary = dbmAry;

		}
		if (fields)
			adjust_fields(&ary[1], fields);


		/*
		 * Delete duplicate entries when looping
		 */
		if (loops) {
			while (count > 2) {
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
			while (count > 2) {
				if ((int)(ary[0].ticks - ary[count-1].ticks) <
				    maxtime) {
					break;
				}
				--count;
			}
		}

		switch(cmd) {
		case 't':
			if (count > 2) {
				dump_text(ary, count, total_count,
					  (fromFile ? DISPLAY_FULL_DATE :
						      DISPLAY_TIME_ONLY));
			}
			break;
		case 'b':
			if (count > 2)
				dump_dbm(ary, count, datafile);
			break;
		case 'r':
			if (count >= 2)
				restore_headers(ary, datafile);
			break;
		case 'l':
			dump_fields(ary);
			exit(0);
			break;		/* NOT REACHED */
		case 'g':
			if (count > 2)
				dump_gnuplot(ary, count);
			break;
		case 'w':
			if (count >= 2)
				dump_gnuplot(ary, count);
			break;
		case 'x':
			if (count > 2)
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
		last_ticks = ary[2].ticks;
		if (count >= 2)
			total_count += count - 2;

		/*
		 * Loop for incremental aquisition.  When outputting to
		 * gunplot, we have to send the whole data-set again so
		 * do not increment loops in that case.
		 */
		if (cmd != 'g' && cmd != 'x' && cmd != 'w')
			++loops;

		free(ary);
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

	for (i = count - 1; i >= 2; --i) {
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
	char hdr_fmt[] = HDR_FMT;
        char hdr_title[] = HDR_TITLE;
	datum key, value;

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
		key.dptr = hdr_fmt;
		key.dsize = sizeof(HDR_FMT);
		value.dptr = &ary[0].data;
		value.dsize = sizeof(uint64_t) * KCOLLECT_ENTRIES;
		if (dbm_store(db,key,value,DBM_REPLACE) == -1) {
			fprintf(stderr,
				"[ERR] error storing the value in "
				"the database file \"%s\" (%i)\n",
				datafile, errno);
			dbm_close(db);
			exit(EXIT_FAILURE);
		}

		key.dptr = hdr_title;
		key.dsize = sizeof(HDR_FMT);
		value.dptr = &ary[1].data;
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
	        char hdr_fmt[] = HDR_FMT;
	        char hdr_title[] = HDR_TITLE;

		for (i = 0; i < (count); ++i) {
			/* first 2 INFO records are special and get 0|1 */

			if (i < 2) {
				if (i == 0)
					key.dptr = hdr_fmt;
				else
					key.dptr = hdr_title;
				key.dsize = sizeof(HDR_FMT);
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
			if (dbm_store(db, key, value, DBM_INSERT) == -1) {
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
 * Loads the ckollect records from a dbm DB database specified in datafile.
 * returns the resulting array in ret_ary and the array counter in counter
 */
static
void
load_dbm(const char* datafile, kcollect_t **ret_ary,
	 size_t *counter)
{
	DBM * db = dbm_open(datafile,(O_RDONLY),(S_IRUSR|S_IRGRP));
	datum key;
	datum value;
	size_t recCounter = 0;
	int headersFound = 0;

	if (db == NULL) {
		fprintf(stderr,
			"[ERR] opening our database \"%s\" produced "
			"an error! (%i)\n",
			datafile, errno);
		exit(EXIT_FAILURE);
	} else {
		/* counting loop */
		for (key = dbm_firstkey(db); key.dptr; key = dbm_nextkey(db)) {
			value = dbm_fetch(db, key);
			if (value.dptr != NULL)
				recCounter++;
		}

		/* with the count allocate enough memory */
		if (*ret_ary)
			free(*ret_ary);
		*ret_ary = malloc(sizeof(kcollect_t) * recCounter);
		bzero(*ret_ary, sizeof(kcollect_t) * recCounter);
		if (*ret_ary == NULL) {
			fprintf(stderr,
				"[ERR] failed to allocate enough memory to "
				"hold the database! Aborting.\n");
			dbm_close(db);
			exit(EXIT_FAILURE);
		} else {
			uint c;
			uint sc;
			/*
			 * Actual data retrieval  but only of recCounter
			 * records
			 */
			c = 2;
			key = dbm_firstkey(db);
			while (key.dptr && c < recCounter) {
				value = dbm_fetch(db, key);
				if (value.dptr != NULL) {
					if (!strcmp(key.dptr, HDR_FMT)) {
						sc = 0;
						headersFound |= 1;
					}
					else if (!strcmp(key.dptr, HDR_TITLE)) {
						sc = 1;
						headersFound |= 2;
					}
					else {
						sc = c;
						c++;
					}

					memcpy((*ret_ary)[sc].data,
					       value.dptr,
					   sizeof(uint64_t) * KCOLLECT_ENTRIES);
					(*ret_ary)[sc].realtime.tv_sec =
					    str2unix(key.dptr,
						     DISPLAY_FULL_DATE);
				}
				key = dbm_nextkey(db);
			}
		}
	}

	/*
	 * Set the counter,
	 * and sort the non-header records.
	 */
	*counter = recCounter;
        qsort(&(*ret_ary)[2], recCounter - 2, sizeof(kcollect_t), rec_comparator);
	dbm_close(db);

	if (headersFound != 3) {
		fprintf(stderr, "We could not retrieve all necessary headers, might be the database file is corrupted? (%i)\n", headersFound);
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
