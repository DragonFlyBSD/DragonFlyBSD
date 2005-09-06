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
 * $DragonFly: src/sbin/jscan/jscan.c,v 1.5 2005/09/06 06:42:44 dillon Exp $
 */

#include "jscan.h"

static void usage(const char *av0);

int jmodes;
int fsync_opt;
off_t record_size = 100 * 1024 * 1024;
off_t trans_count;
static enum jdirection jdirection = JD_FORWARDS;

static void jscan_do_output(struct jfile *, const char *, int64_t);
static void jscan_do_mirror(struct jfile *, const char *, int64_t);
static void jscan_do_record(struct jfile *, const char *, int64_t);
static void fork_subprocess(struct jfile *,
		void (*)(struct jfile *, const char *, int64_t),
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
    enum jdirection direction;
    char *ptr;
    int64_t mirror_transid;
    int64_t output_transid;
    int64_t record_transid;
    int64_t transid;
    int input_fd;
    struct stat st;
    struct jfile *jf;
    struct jdata *jd;
    int bytes;
    int error;
    int ch;

    while ((ch = getopt(ac, av, "2dm:o:s:uw:D:O:W:F")) != -1) {
	switch(ch) {
	case '2':
	    jmodes |= JMODEF_INPUT_FULL;
	    break;
	case 'c':
	    trans_count = strtoll(optarg, &ptr, 0);
	    switch(*ptr) {
	    case 't':
		record_size *= 1024;
		/* fall through */
	    case 'g':
		record_size *= 1024;
		/* fall through */
	    case 'm':
		record_size *= 1024;
		/* fall through */
	    case 'k':
		record_size *= 1024;
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
	    record_size = strtoll(optarg, &ptr, 0);
	    switch(*ptr) {
	    case 't':
		record_size *= 1024;
		/* fall through */
	    case 'g':
		record_size *= 1024;
		/* fall through */
	    case 'm':
		record_size *= 1024;
		/* fall through */
	    case 'k':
		record_size *= 1024;
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
	jf = jopen_fd(input_fd, jdirection);
    } else if (stat(av[optind], &st) == 0 && S_ISREG(st.st_mode)) {
	input_prefix = av[optind];
	if ((input_fd = open(av[optind], O_RDONLY)) != NULL) {
	    jf = jopen_fd(input_fd, jdirection);
	} else {
	    jf = NULL;
	}
    } else {
	input_prefix = av[optind];
	jf = jopen_prefix(input_prefix, jdirection, 0);
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
			    mirror_directory, mirror_transid);
	    /* XXX ack stream for temporary record file removal */
	}
	if (jmodes & JMODEF_OUTPUT) {
	    fork_subprocess(jf, jscan_do_output, record_prefix,
			    NULL, output_transid);
	    /* XXX ack stream for temporary record file removal */
	}
	jscan_do_record(jf, record_prefix, record_transid);
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
			    mirror_directory, mirror_transid);
	    jscan_do_output(jf, NULL, output_transid);
	} else if (jmodes & JMODEF_OUTPUT) {
	    jscan_do_output(jf, NULL, output_transid);
	} else if (jmodes & JMODEF_MIRROR) {
	    jscan_do_mirror(jf, mirror_directory, mirror_transid);
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
	jseek(jf, transid, jdirection);
    jmodes |= JMODEF_MEMORY_TRACKING;

    while ((error = jread(jf, &jd, jdirection)) == 0) {
	if (jmodes & JMODEF_DEBUG)
	    dump_debug(jf, jd, transid);
	if (jmodes & JMODEF_OUTPUT)
	    dump_output(jf, jd, output_transid);
	if (jmodes & JMODEF_MIRROR)
	    dump_mirror(jf, jd, mirror_transid);
	jfree(jf, jd);
    }
    jclose(jf);
    exit(error ? 1 : 0);
}

static
void
fork_subprocess(struct jfile *jftoclose,
		void (*func)(struct jfile *, const char *, int64_t),
		const char *input_prefix, const char *info, int64_t transid)
{
    pid_t pid;
    struct jfile *jf;

    if ((pid = fork()) == 0) {
	jmodes &= ~JMODEF_DEBUG;
	jclose(jftoclose);
	jf = jopen_prefix(input_prefix, jdirection, 0);
	jmodes |= JMODEF_INPUT_PREFIX;
	func(jf, info, transid);
	jclose(jf);
	exit(0);
    } else if (pid < 0) {
	fprintf(stderr, "fork(): %s\n", strerror(errno));
	exit(1);
    }
}

static
void
jscan_do_output(struct jfile *jf, const char *info, int64_t transid)
{
    struct jdata *jd;
    int error;

    if ((jmodes & JMODEF_OUTPUT_TRANSID_GOOD) && !(jmodes & JMODEF_INPUT_PIPE))
	jseek(jf, transid, jdirection);
    while ((error = jread(jf, &jd, jdirection)) == 0) {
	if (jmodes & JMODEF_DEBUG)
	    dump_debug(jf, jd, transid);
	dump_output(jf, jd, transid);
	jfree(jf, jd);
    }
}

static
void
jscan_do_mirror(struct jfile *jf, const char *info, int64_t transid)
{
    struct jdata *jd;
    int error;

    if ((jmodes & JMODEF_MIRROR_TRANSID_GOOD) && !(jmodes & JMODEF_INPUT_PIPE))
	jseek(jf, transid, jdirection);
    while ((error = jread(jf, &jd, jdirection)) == 0) {
	if (jmodes & JMODEF_DEBUG)
	    dump_debug(jf, jd, transid);
	dump_mirror(jf, jd, transid);
	jfree(jf, jd);
    }
}

static
void
jscan_do_record(struct jfile *jf, const char *info, int64_t transid)
{
    struct jdata *jd;
    int error;

    if ((jmodes & JMODEF_RECORD_TRANSID_GOOD) && !(jmodes & JMODEF_INPUT_PIPE))
	jseek(jf, transid, jdirection);
    while ((error = jread(jf, &jd, jdirection)) == 0) {
	if (jmodes & JMODEF_DEBUG)
	    dump_debug(jf, jd, transid);
	dump_record(jf, jd, transid);
	jfree(jf, jd);
    }
}

static void
usage(const char *av0)
{
    fprintf(stderr, 
	"%s [-2duF] [-D dir] [-m mirror_transid_file/none]\n"
	"\t[-o/O output_trnasid_file/none]\n"
	"\t[-s size[kmgt]] -w/W record_prefix] [input_file/input_prefix]\n",
	av0);
    exit(1);
}

