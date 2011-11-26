/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
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
 * 
 * $DragonFly: src/sbin/jscan/jscan.c,v 1.13 2008/06/05 18:06:30 swildner Exp $
 */

#include "jscan.h"

static int donecheck(enum jdirection direction, struct jdata *jd,
		     int64_t transid);
static void usage(const char *av0);

int jmodes;
int fsync_opt;
int verbose_opt;
off_t prefix_file_size = 100 * 1024 * 1024;
off_t trans_count;
static enum jdirection jdirection = JD_FORWARDS;

static void jscan_do_output(struct jfile *, const char *, 
			    const char *, int64_t);
static void jscan_do_mirror(struct jfile *, const char *,
			    const char *, int64_t);
static void jscan_do_record(struct jfile *, const char *,
			    const char *, int64_t);
static void jscan_do_debug(struct jfile *, const char *,
			    const char *, int64_t);
static void fork_subprocess(struct jfile *,
			    void (*)(struct jfile *, const char *,
				     const char *, int64_t),
			    const char *,
			    const char *, const char *, int64_t);

int
main(int ac, char **av)
{
    const char *input_prefix = NULL;
    char *output_transid_file = NULL;
    char *mirror_transid_file = NULL;
    const char *mirror_directory = ".";
    char *record_prefix = NULL;
    char *record_transid_file = NULL;
    struct jsession jsdebug;
    struct jsession jsoutput;
    struct jsession jsmirror;
    char *ptr;
    int64_t mirror_transid;
    int64_t output_transid;
    int64_t record_transid;
    int64_t transid;
    int input_fd;
    struct stat st;
    struct jfile *jf;
    struct jdata *jd;
    int ch;

    while ((ch = getopt(ac, av, "2c:dfm:o:s:uvw:D:O:W:F")) != -1) {
	switch(ch) {
	case '2':
	    jmodes |= JMODEF_INPUT_FULL;
	    break;
	case 'c':
	    trans_count = strtoll(optarg, &ptr, 0);
	    switch(*ptr) {
	    case 't':
		trans_count *= 1024;
		/* fall through */
	    case 'g':
		trans_count *= 1024;
		/* fall through */
	    case 'm':
		trans_count *= 1024;
		/* fall through */
	    case 'k':
		trans_count *= 1024;
		break;
	    case 0:
		break;
	    default:
		fprintf(stderr, "Bad suffix for value specified with -c, use 'k', 'm', 'g', 't', or nothing\n");
		usage(av[0]);
	    }
	    break;
	case 'd':
	    jmodes |= JMODEF_DEBUG;
	    break;
	case 'f':
	    jmodes |= JMODEF_LOOP_FOREVER;
	    break;
	case 'v':
	    ++verbose_opt;
	    break;
	case 'm':
	    jmodes |= JMODEF_MIRROR;
	    if (strcmp(optarg, "none") != 0)
		mirror_transid_file = optarg;
	    break;
	case 'O':
	    jmodes |= JMODEF_OUTPUT_FULL;
	    /* fall through */
	case 'o':
	    jmodes |= JMODEF_OUTPUT;
	    if (strcmp(optarg, "none") != 0)
		output_transid_file = optarg;
	    break;
	case 's':
	    prefix_file_size = strtoll(optarg, &ptr, 0);
	    switch(*ptr) {
	    case 't':
		prefix_file_size *= 1024;
		/* fall through */
	    case 'g':
		prefix_file_size *= 1024;
		/* fall through */
	    case 'm':
		prefix_file_size *= 1024;
		/* fall through */
	    case 'k':
		prefix_file_size *= 1024;
		break;
	    case 0:
		break;
	    default:
		fprintf(stderr, "Bad suffix for value specified with -s, use 'k', 'm', 'g', 't', or nothing\n");
		usage(av[0]);
	    }
	    break;
	case 'u':
	    jdirection = JD_BACKWARDS;
	    break;
	case 'W':
	    jmodes |= JMODEF_RECORD_TMP;
	    /* fall through */
	case 'w':
	    jmodes |= JMODEF_RECORD;
	    record_prefix = optarg;
	    asprintf(&record_transid_file, "%s.transid", record_prefix);
	    break;
	case 'D':
	    mirror_directory = optarg;
	    break;
	case 'F':
	    ++fsync_opt;
	    break;
	default:
	    fprintf(stderr, "unknown option: -%c\n", optopt);
	    usage(av[0]);
	}
    }

    /*
     * Sanity checks
     */
    if ((jmodes & JMODEF_COMMAND_MASK) == 0)
	usage(av[0]);
    if (optind > ac + 1)  {
	fprintf(stderr, "Only one input file or prefix may be specified,\n"
			"or zero if stdin is to be the input.\n");
	usage(av[0]);
    }
    if (strcmp(mirror_directory, ".") != 0) {
	struct stat sb;
	if (stat(mirror_directory, &sb) != 0) {
	    perror ("Could not stat mirror directory");
	    usage(av[0]);
	}
	if (!S_ISDIR(sb.st_mode))
	{
	    fprintf (stderr, "Mirror directory '%s' is not a directory\n", mirror_directory);
	    usage(av[0]);
	}
    }
    if (jdirection == JD_BACKWARDS && (jmodes & (JMODEF_RECORD|JMODEF_OUTPUT))) {
	fprintf(stderr, "Undo mode is only good in mirroring mode and "
			"cannot be mixed with other modes.\n");
	exit(1);
    }

    /*
     * STEP1 - OPEN INPUT
     *
     * The input will either be a pipe, a regular file, or a journaling 
     * file prefix.
     */
    jf = NULL;
    if (optind == ac) {
	input_prefix = "<stdin>";
	input_fd = 0;
	if (fstat(0, &st) < 0 || !S_ISREG(st.st_mode)) {
	    jmodes |= JMODEF_INPUT_PIPE;
	    if (jdirection == JD_BACKWARDS) {
		fprintf(stderr, "Cannot scan journals on pipes backwards\n");
		usage(av[0]);
	    }
	}
	jf = jopen_fd(input_fd);
    } else if (stat(av[optind], &st) == 0 && S_ISREG(st.st_mode)) {
	input_prefix = av[optind];
	if ((input_fd = open(av[optind], O_RDONLY)) != 0) {
	    jf = jopen_fd(input_fd);
	} else {
	    jf = NULL;
	}
    } else {
	input_prefix = av[optind];
	jf = jopen_prefix(input_prefix, 0);
	jmodes |= JMODEF_INPUT_PREFIX;
    }
    if (jf == NULL) {
	fprintf(stderr, "Unable to open input %s: %s\n", 
		input_prefix, strerror(errno));
	exit(1);
    }

    /*
     * STEP 1 - SYNCHRONIZING THE INPUT STREAM
     *
     * Figure out the starting point for our various output modes.  Figure
     * out the earliest transaction id and try to seek to that point,
     * otherwise we might have to scan through terrabytes of data.
     *
     * Invalid transid's will be set to 0, but it should also be noted
     * that 0 is also a valid transid.
     */
    get_transid_from_file(output_transid_file, &output_transid,
			  JMODEF_OUTPUT_TRANSID_GOOD);
    get_transid_from_file(mirror_transid_file, &mirror_transid, 
			  JMODEF_MIRROR_TRANSID_GOOD);
    get_transid_from_file(record_transid_file, &record_transid, 
			  JMODEF_RECORD_TRANSID_GOOD);
    transid = LLONG_MAX;
    if ((jmodes & JMODEF_OUTPUT_TRANSID_GOOD) && output_transid < transid)
	transid = output_transid;
    if ((jmodes & JMODEF_MIRROR_TRANSID_GOOD) && mirror_transid < transid)
	transid = mirror_transid;
    if ((jmodes & JMODEF_RECORD_TRANSID_GOOD) && record_transid < transid)
	transid = record_transid;
    if ((jmodes & JMODEF_TRANSID_GOOD_MASK) == 0)
	transid = 0;
    if (verbose_opt) {
	if (jmodes & JMODEF_OUTPUT) {
	    fprintf(stderr, "Starting transid for OUTPUT: %016jx\n",
		    (uintmax_t)output_transid);
	}
	if (jmodes & JMODEF_MIRROR) {
	    fprintf(stderr, "Starting transid for MIRROR: %016jx\n",
		    (uintmax_t)mirror_transid);
	}
	if (jmodes & JMODEF_RECORD) {
	    fprintf(stderr, "Starting transid for RECORD: %016jx\n",
		    (uintmax_t)record_transid);
	}
    }

    if (strcmp(mirror_directory, ".") != 0) {
	if (chdir (mirror_directory) != 0) {
	    perror ("Could not enter mirror directory");
	    exit (1);
	}
    }

    /*
     * Now it gets more difficult.  If we are recording then the input
     * could be representative of continuing data and not have any
     * prior, older data that the output or mirror modes might need.  Those
     * modes must work off the recording data even as we write to it.
     * In that case we fork and have the sub-processes work off the
     * record output.
     *
     * Then we take the input and start recording.
     */
    if (jmodes & JMODEF_RECORD) {
	if (jrecord_init(record_prefix) < 0) {
	    fprintf(stderr, "Unable to initialize file set for: %s\n", 
		    record_prefix);
	    exit(1);
	}
	if (jmodes & JMODEF_MIRROR) {
	    fork_subprocess(jf, jscan_do_mirror, record_prefix, 
			    mirror_transid_file,
			    mirror_directory, mirror_transid);
	    /* XXX ack stream for temporary record file removal */
	}
	if (jmodes & JMODEF_OUTPUT) {
	    fork_subprocess(jf, jscan_do_output, record_prefix,
			    record_transid_file,
			    NULL, output_transid);
	    /* XXX ack stream for temporary record file removal */
	}
	jscan_do_record(jf, record_transid_file, record_prefix, record_transid);
	exit(0);
    }

    /*
     * If the input is a prefix set we can just pass it to the appropriate
     * jscan_do_*() function.  If we are doing both output and mirroring
     * we fork the mirror and do the output in the foreground since that
     * is going to stdout.
     */
    if (jmodes & JMODEF_INPUT_PREFIX) {
	if ((jmodes & JMODEF_OUTPUT) && (jmodes & JMODEF_MIRROR)) {
	    fork_subprocess(jf, jscan_do_mirror, input_prefix, 
			    mirror_transid_file,
			    mirror_directory, mirror_transid);
	    jscan_do_output(jf, output_transid_file, NULL, output_transid);
	} else if (jmodes & JMODEF_OUTPUT) {
	    jscan_do_output(jf, output_transid_file, NULL, output_transid);
	} else if (jmodes & JMODEF_MIRROR) {
	    jscan_do_mirror(jf, mirror_transid_file, mirror_directory,
			    mirror_transid);
	} else if (jmodes & JMODEF_DEBUG) {
	    jscan_do_debug(jf, NULL, NULL, 0);
	}
	exit(0);
    }

    /*
     * The input is not a prefix set and we are not recording, which means
     * we have to transfer the data on the input pipe to the output and
     * mirroring code on the fly.  This also means that we must keep track
     * of meta-data records in-memory.  However, if the input is a regular
     * file we *CAN* try to optimize where we start reading.
     *
     * NOTE: If the mirroring code encounters a transaction record that is
     * not marked begin, and it does not have the begin record, it will
     * attempt to locate the begin record if the input is not a pipe, then
     * seek back.
     */
    if ((jmodes & JMODEF_TRANSID_GOOD_MASK) && !(jmodes & JMODEF_INPUT_PIPE))
	jd = jseek(jf, transid, jdirection);
    else
	jd = jread(jf, NULL, jdirection);
    jmodes |= JMODEF_MEMORY_TRACKING;

    jsession_init(&jsdebug, jf, jdirection,
		  NULL, 0);
    jsession_init(&jsoutput, jf, jdirection, 
		  output_transid_file, output_transid);
    jsession_init(&jsmirror, jf, jdirection,
		  mirror_transid_file, mirror_transid);
    jsmirror.ss_mirror_directory = mirror_directory;

    while (jd != NULL) {
	if ((jmodes & JMODEF_DEBUG) && jsession_check(&jsdebug, jd))
	    dump_debug(&jsdebug, jd);
	if ((jmodes & JMODEF_OUTPUT) && jsession_check(&jsoutput, jd))
	    dump_output(&jsoutput, jd);
	if ((jmodes & JMODEF_MIRROR) && jsession_check(&jsmirror, jd))
	    dump_mirror(&jsmirror, jd);
	if (donecheck(jdirection, jd, transid)) {
	    jfree(jf, jd);
	    break;
	}
	jd = jread(jf, jd, jdirection);
    }
    jclose(jf);
    jsession_term(&jsdebug);
    jsession_term(&jsoutput);
    jsession_term(&jsmirror);
    return(0);
}

/*
 * Returns one if we need to break out of our scanning loop, zero otherwise.
 */
static int
donecheck(enum jdirection direction, struct jdata *jd, int64_t transid)
{
    if (direction == JD_FORWARDS) {
	if (jd->jd_transid > transid && trans_count && --trans_count == 0)
	    return(1);
    } else {
	if (jd->jd_transid <= transid && trans_count && --trans_count == 0)
	    return(1);
    }
    return(0);
}

/*
 * When we have multiple commands and are writing to a prefix set, we can
 * 'background' the output and/or mirroring command and have the background
 * processes feed off the prefix set the foreground process is writing to.
 */
static void
fork_subprocess(struct jfile *jftoclose,
	void (*func)(struct jfile *, const char *, const char *, int64_t),
	const char *input_prefix, const char *transid_file, const char *info,
	int64_t transid)
{
    pid_t pid;
    struct jfile *jf;

    if ((pid = fork()) == 0) {
	jmodes &= ~(JMODEF_DEBUG | JMODEF_INPUT_PIPE);
	jmodes |= JMODEF_LOOP_FOREVER;	/* keep checking for new input */
	jclose(jftoclose);
	jf = jopen_prefix(input_prefix, 0);
	jmodes |= JMODEF_INPUT_PREFIX;
	func(jf, transid_file, info, transid);
	jclose(jf);
	exit(0);
    } else if (pid < 0) {
	fprintf(stderr, "fork(): %s\n", strerror(errno));
	exit(1);
    }
}

static void
jscan_do_output(struct jfile *jf, const char *output_transid_file, const char *dummy __unused, int64_t transid)
{
    struct jdata *jd;
    struct jsession jsdebug;
    struct jsession jsoutput;

    jsession_init(&jsdebug, jf, jdirection,
		  NULL, 0);
    jsession_init(&jsoutput, jf, jdirection,
		  output_transid_file, transid);

    if ((jmodes & JMODEF_OUTPUT_TRANSID_GOOD) && !(jmodes & JMODEF_INPUT_PIPE))
	jd = jseek(jf, transid, jdirection);
    else
	jd = jread(jf, NULL, jdirection);
    while (jd != NULL) {
	if ((jmodes & JMODEF_DEBUG) && jsession_check(&jsdebug, jd))
	    dump_debug(&jsdebug, jd);
	if (jsession_check(&jsoutput, jd))
	    dump_output(&jsoutput, jd);
	if (donecheck(jdirection, jd, transid)) {
	    jfree(jf, jd);
	    break;
	}
	jd = jread(jf, jd, jdirection);
    }
    jsession_term(&jsdebug);
    jsession_term(&jsoutput);
}

static void
jscan_do_mirror(struct jfile *jf, const char *mirror_transid_file, const char *mirror_directory, int64_t transid)
{
    struct jsession jsdebug;
    struct jsession jsmirror;
    struct jdata *jd;

    jsession_init(&jsdebug, jf, jdirection,
		  NULL, 0);
    jsession_init(&jsmirror, jf, jdirection,
		  mirror_transid_file, transid);
    jsmirror.ss_mirror_directory = mirror_directory;

    if ((jmodes & JMODEF_MIRROR_TRANSID_GOOD) && !(jmodes & JMODEF_INPUT_PIPE))
	jd = jseek(jf, transid, jdirection);
    else
	jd = jread(jf, NULL, jdirection);
    while (jd != NULL) {
	if ((jmodes & JMODEF_DEBUG) && jsession_check(&jsdebug, jd))
	    dump_debug(&jsdebug, jd);
	if (jsession_check(&jsmirror, jd))
	    dump_mirror(&jsmirror, jd);
	if (donecheck(jdirection, jd, transid)) {
	    jfree(jf, jd);
	    break;
	}
	jd = jread(jf, jd, jdirection);
    }
    jsession_term(&jsdebug);
    jsession_term(&jsmirror);
}

static void
jscan_do_record(struct jfile *jfin, const char *record_transid_file, const char *prefix, int64_t transid)
{
    struct jsession jsdebug;
    struct jsession jsrecord;
    struct jdata *jd;

    jsession_init(&jsdebug, jfin, jdirection,
		  NULL, 0);
    jsession_init(&jsrecord, jfin, jdirection,
		  record_transid_file, transid);

    assert(jdirection == JD_FORWARDS);
    jsrecord.ss_jfout = jopen_prefix(prefix, 1);
    if (jsrecord.ss_jfout == NULL) {
	fprintf(stderr, "Unable to open prefix set for writing: %s\n", prefix);
	exit(1);
    }
    if ((jmodes & JMODEF_RECORD_TRANSID_GOOD) && !(jmodes & JMODEF_INPUT_PIPE))
	jd = jseek(jfin, transid, jdirection);
    else
	jd = jread(jfin, NULL, jdirection);
    while (jd != NULL) {
	if ((jmodes & JMODEF_DEBUG) && jsession_check(&jsdebug, jd))
	    dump_debug(&jsdebug, jd);
	if (jsession_check(&jsrecord, jd))
	    dump_record(&jsrecord, jd);
	if (donecheck(jdirection, jd, transid)) {
	    jfree(jfin, jd);
	    break;
	}
	jd = jread(jfin, jd, jdirection);
    }
    jclose(jsrecord.ss_jfout);
    jsrecord.ss_jfout = NULL;
    jsession_term(&jsdebug);
    jsession_term(&jsrecord);
}

static void
jscan_do_debug(struct jfile *jf, const char *dummy1 __unused,
	       const char *dummy __unused, int64_t transid __unused)
{
    struct jsession jsdebug;
    struct jdata *jd;

    jsession_init(&jsdebug, jf, jdirection,
		  NULL, 0);
    jd = NULL;
    while ((jd = jread(jf, jd, jdirection)) != NULL) {
	if (jsession_check(&jsdebug, jd))
	    dump_debug(&jsdebug, jd);
	if (donecheck(jdirection, jd, transid)) {
	    jfree(jf, jd);
	    break;
	}
    }
    jsession_term(&jsdebug);
}

static void
usage(const char *av0)
{
    fprintf(stderr, 
	"%s [-2dfuvF] [-D dir] [-m mirror_transid_file/none]\n"
	"\t[-o/O output_transid_file/none]\n"
	"\t[-s size[kmgt]] -w/W record_prefix] [input_file/input_prefix]\n",
	av0);
    exit(1);
}

