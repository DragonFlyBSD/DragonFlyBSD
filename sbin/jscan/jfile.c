/*
 * Copyright (c) 2004,2005 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/jscan/jfile.c,v 1.10 2005/09/07 07:20:23 dillon Exp $
 */

#include "jscan.h"
#include <dirent.h>

static void jalign(struct jfile *jf, enum jdirection direction);
static int jreadbuf(struct jfile *jf, enum jdirection direction,
		    void *buf, int bytes);

/*
 * Open a file descriptor for journal record access. 
 *
 * NOTE: only seekable descriptors are supported for backwards scans.
 */
struct jfile *
jopen_fd(int fd)
{
    struct jfile *jf;

    jf = malloc(sizeof(struct jfile));
    bzero(jf, sizeof(struct jfile));
    jf->jf_fd = fd;
    jf->jf_write_fd = -1;
    jf->jf_open_flags = O_RDONLY;
    jf->jf_pos = 0;
    return(jf);
}

/*
 * Open a prefix set.  <prefix>.nnnnnnnnn files or a <prefix>.transid file
 * must exist to succeed.  No file descriptor is actually opened but
 * the sequence number is initialized to the beginning or end of the set.
 */
struct jfile *
jopen_prefix(const char *prefix, int rw)
{
    struct jfile *jf;
    struct jdata *jd;
    unsigned int seq_beg = -1;
    unsigned int seq_end = -1;
    unsigned int seq;
    struct stat st;
    const char *dirname;
    struct dirent *den;
    DIR *dir;
    char *basename;
    char *data;
    char *ptr;
    int hastransid;
    int baselen;
    int fd;

    dirname = data = strdup(prefix);
    if ((basename = strrchr(dirname, '/')) != NULL) {
	*basename++ = 0;
    } else {
	basename = data;
	dirname = "./";
    }
    baselen = strlen(basename);
    if ((dir = opendir(dirname)) != NULL) {
	while ((den = readdir(dir)) != NULL) {
	    if (strncmp(den->d_name, basename, baselen) == 0 && 
		den->d_name[baselen] == '.'
	    ) {
		seq = strtoul(den->d_name + baselen + 1, &ptr, 16);
		if (*ptr == 0 && seq != UINT_MAX) {
		    if (seq_beg == (unsigned int)-1 || seq_beg > seq)
			seq_beg = seq;
		    if (seq_end == (unsigned int)-1 || seq_end < seq)
			seq_end = seq;
		}
	    }
	}
	closedir(dir);
    }
    free(data);

    hastransid = 0;
    asprintf(&data, "%s.transid", prefix);
    if (stat(data, &st) == 0)
	hastransid = 1;
    free(data);

    if (seq_beg != (unsigned int)-1 || hastransid) {
	if (seq_beg == (unsigned int)-1) {
	    seq_beg = 0;
	    seq_end = 0;
	    if (rw) {
		asprintf(&data, "%s.%08x", prefix, 0);
		if ((fd = open(data, O_RDWR|O_CREAT, 0666)) >= 0)
		    close(fd);
		free(data);
	    }
	}
	jf = malloc(sizeof(struct jfile));
	bzero(jf, sizeof(struct jfile));
	jf->jf_fd = -1;
	jf->jf_write_fd = -1;
	jf->jf_prefix = strdup(prefix);
	jf->jf_seq = seq_beg;
	jf->jf_seq_beg = seq_beg;
	jf->jf_seq_end = seq_end;
	jf->jf_open_flags = rw ? (O_RDWR|O_CREAT) : O_RDONLY;
	if (verbose_opt)
	    fprintf(stderr, "Open prefix set %08x-%08x\n", seq_beg, seq_end);
	if ((jd = jread(jf, NULL, JD_BACKWARDS)) != NULL) {
	    jf->jf_last_transid = jd->jd_transid;
	    jfree(jf, jd);
	}
    } else {
	jf = NULL;
    }
    return(jf);
}

/*
 * Get a prefix set ready for append.
 */
int
jrecord_init(const char *prefix)
{
    struct jfile *jf;
    struct stat st;
    char *data;
    int hasseqspace;
    int fd;

    /*
     * Determine whether we already have a prefix set or whether we need
     * to create one.
     */
    jf = jopen_prefix(prefix, 0);
    hasseqspace = 0;
    if (jf) {
	if (jf->jf_seq_beg != (unsigned int)-1)
	    hasseqspace = 1;
	jclose(jf);
    }
    asprintf(&data, "%s.transid", prefix);

    /*
     * If the sequence exists the transid file must ALREADY exist for us
     * to be able to safely 'append' to the space.  Locked-down sequence
     * spaces do not have a transid file.
     */
    if (hasseqspace) {
	fd = open(data, O_RDWR, 0666);
    } else {
	fd = open(data, O_RDWR|O_CREAT, 0666);
    }
    free(data);
    if (fd < 0)
	return(-1);
    if (fstat(fd, &st) == 0 && st.st_size == 0)
	write(fd, "0000000000000000\n", 17);	/* starting transid in hex */
    close(fd);
    return(0);
}

/*
 * Close a previously opened journal, clean up any side allocations.
 */
void
jclose(struct jfile *jf)
{
    if (jf->jf_fd >= 0) {
	close(jf->jf_fd);
	jf->jf_fd = -1;
    }
    if (jf->jf_write_fd >= 0) {
	close(jf->jf_write_fd);
	jf->jf_write_fd = -1;
    }
    free(jf);
}

/*
 * Locate the next (or previous) raw record given a jfile, current record,
 * and direction.  If the current record is NULL then the first or last
 * record for the current sequence number is returned.
 *
 * PAD RECORD SPECIAL CASE.  Pad records can be 16 bytes long, which means
 * that that rawrecend overlaps the transid field of the rawrecbeg.  Because
 * the transid is garbage, we must skip and cannot return pad records.
 */
struct jdata *
jread(struct jfile *jf, struct jdata *jd, enum jdirection direction)
{
    struct journal_rawrecbeg head;
    struct journal_rawrecbeg *headp;
    struct journal_rawrecend tail;
    struct journal_rawrecend *tailp;
    struct stat st;
    char *filename;
    int allocsize;
    int recsize;
    int search;
    int error;
    int n;

    if (jd) {
	/*
	 * Handle the next/previous record case.  If running in the forwards
	 * direction we position the file just after jd.  If running in the
	 * backwards direction we position the file at the base of jd so
	 * the backwards read gets the previous record.
	 *
	 * In prefix mode we have to get the right descriptor open and
	 * position the file, since the fall through code resets to the
	 * beginning or end if it has to open a descriptor.
	 */
	assert(direction != JD_SEQFIRST && direction != JD_SEQLAST);
	if (jf->jf_prefix) {
	    if (jf->jf_fd >= 0 && jf->jf_seq != jd->jd_seq) {
		close(jf->jf_fd);
		jf->jf_fd = -1;
	    }
	    jf->jf_seq = jd->jd_seq;
	    if (jf->jf_fd < 0) {
		asprintf(&filename, "%s.%08x", jf->jf_prefix, jf->jf_seq);
		jf->jf_fd = open(filename, O_RDONLY);
		if (verbose_opt > 1)
		    fprintf(stderr, "Open %s fd %d\n", filename, jf->jf_fd);
		free(filename);
	    }
	}
	if ((jmodes & JMODEF_INPUT_PIPE) == 0) {
	    if (direction == JD_FORWARDS) {
		jf->jf_pos = jd->jd_pos + jd->jd_size;
		lseek(jf->jf_fd, jf->jf_pos, 0);
	    } else {
		jf->jf_pos = jd->jd_pos;
		/* lseek(jf->jf_fd, jf->jf_pos, 0); not needed */
	    }
	} else {
	    assert(direction == JD_FORWARDS && jf->jf_prefix == NULL);
	    assert(jf->jf_pos == jd->jd_pos + jd->jd_size);
	}
	jfree(jf, jd);
    } else {
	/*
	 * Handle the first/last record case.  In the prefix case we only
	 * need to set jf_seq and close the file handle and fall through.
	 * The SEQ modes maintain the current jf_seq (kinda a hack).
	 */
	if (jf->jf_prefix) {
	    if (jf->jf_fd >= 0) {
		close(jf->jf_fd);
		jf->jf_fd = -1;
	    }
	    switch(direction) {
	    case JD_FORWARDS:
		jf->jf_seq = jf->jf_seq_beg;
		break;
	    case JD_BACKWARDS:
		jf->jf_seq = jf->jf_seq_end;
		break;
	    case JD_SEQFIRST:
		direction = JD_FORWARDS;
		break;
	    case JD_SEQLAST:
		direction = JD_BACKWARDS;
		break;
	    }
	} else if ((jmodes & JMODEF_INPUT_PIPE) == 0) {
	    switch(direction) {
	    case JD_SEQFIRST:
		direction = JD_FORWARDS;
		/* fall through */
	    case JD_FORWARDS:
		jf->jf_pos = lseek(jf->jf_fd, 0L, SEEK_SET);
		break;
	    case JD_SEQLAST:
		direction = JD_BACKWARDS;
		/* fall through */
	    case JD_BACKWARDS:
		jf->jf_pos = lseek(jf->jf_fd, 0L, SEEK_END);
		break;
	    }
	} else {
	    if (direction == JD_SEQFIRST)
		direction = JD_FORWARDS;
	    assert(jf->jf_pos == 0 && direction == JD_FORWARDS);
	}
    }

top:
    /*
     * If we are doing a prefix scan and the descriptor is not open,
     * open the file based on jf_seq and position it to the beginning
     * or end based on the direction.  This is how we iterate through
     * the prefix set.
     */
    if (jf->jf_fd < 0) {
	asprintf(&filename, "%s.%08x", jf->jf_prefix, jf->jf_seq);
	jf->jf_fd = open(filename, O_RDONLY);
	if (verbose_opt > 1)
	    fprintf(stderr, "Open %s fd %d\n", filename, jf->jf_fd);
	free(filename);
	if (direction == JD_FORWARDS)
	    jf->jf_pos = lseek(jf->jf_fd, 0L, SEEK_SET);
	else
	    jf->jf_pos = lseek(jf->jf_fd, 0L, SEEK_END);
    }

    /*
     * Get the current offset and make sure it is 16-byte aligned.  If it
     * isn't, align it and enter search mode.
     */
    if (jf->jf_pos & 15) {
	jf_warn(jf, "realigning bad offset and entering search mode");
	jalign(jf, direction);
	search = 1;
    } else {
	search = 0;
    }

    error = 0;
    if (direction == JD_FORWARDS) {
	/*
	 * Scan the journal forwards.  Note that the file pointer might not
	 * be seekable.
	 */
	while ((error = jreadbuf(jf, direction, &head, sizeof(head))) == sizeof(head)) {
	    if (head.begmagic != JREC_BEGMAGIC) {
		if (search == 0)
		    jf_warn(jf, "bad beginmagic, searching for new record");
		search = 1;
		jalign(jf, direction);
		continue;
	    }

	    /*
	     * The actual record is 16-byte aligned.  head.recsize contains
	     * the unaligned record size.
	     */
	    recsize = (head.recsize + 15) & ~15;
	    if (recsize < JREC_MINRECSIZE || recsize > JREC_MAXRECSIZE) {
		if (search == 0)
		    jf_warn(jf, "bad recordsize: %d\n", recsize);
		search = 1;
		jalign(jf, direction);
		continue;
	    }
	    allocsize = offsetof(struct jdata, jd_data[recsize]);
	    allocsize = (allocsize + 255) & ~255;
	    jd = malloc(allocsize);
	    bzero(jd, offsetof(struct jdata, jd_data[0]));
	    bcopy(&head, jd->jd_data, sizeof(head));
	    n = jreadbuf(jf, direction, jd->jd_data + sizeof(head), 
			 recsize - sizeof(head));
	    if (n != (int)(recsize - sizeof(head))) {
		if (search == 0)
		    jf_warn(jf, "Incomplete stream record\n");
		search = 1;
		jalign(jf, direction);
		free(jd);
		continue;
	    }

	    tailp = (void *)(jd->jd_data + recsize - sizeof(*tailp));
	    if (tailp->endmagic != JREC_ENDMAGIC) {
		if (search == 0)
		    jf_warn(jf, "bad endmagic, searching for new record");
		search = 1;
		jalign(jf, direction);
		free(jd);
		continue;
	    }

	    /*
	     * Skip pad records.
	     */
	    if (head.streamid == JREC_STREAMID_PAD) {
		free(jd);
		continue;
	    }

	    /*
	     * note: recsize is aligned (the actual record size),
	     * head.recsize is unaligned (the actual payload size).
	     */
	    jd->jd_transid = head.transid;
	    jd->jd_alloc = allocsize;
	    jd->jd_size = recsize;
	    jd->jd_seq = jf->jf_seq;
	    jd->jd_pos = jf->jf_pos - recsize;
	    jd->jd_refs = 1;
	    return(jd);
	}
    } else {
	/*
	 * Scan the journal backwards.  Note that jread()'s reverse-seek and
	 * read.  The data read will be forward ordered, however.
	 */
	while ((error = jreadbuf(jf, direction, &tail, sizeof(tail))) == sizeof(tail)) {
	    if (tail.endmagic != JREC_ENDMAGIC) {
		if (search == 0)
		    jf_warn(jf, "bad endmagic, searching for new record");
		search = 1;
		jalign(jf, direction);
		continue;
	    }

	    /*
	     * The actual record is 16-byte aligned.  head.recsize contains
	     * the unaligned record size.
	     */
	    recsize = (tail.recsize + 15) & ~15;
	    if (recsize < JREC_MINRECSIZE || recsize > JREC_MAXRECSIZE) {
		if (search == 0)
		    jf_warn(jf, "bad recordsize: %d\n", recsize);
		search = 1;
		jalign(jf, direction);
		continue;
	    }
	    allocsize = offsetof(struct jdata, jd_data[recsize]);
	    allocsize = (allocsize + 255) & ~255;
	    jd = malloc(allocsize);
	    bzero(jd, offsetof(struct jdata, jd_data[0]));
	    bcopy(&tail, jd->jd_data + recsize - sizeof(tail), sizeof(tail));
	    n = jreadbuf(jf, direction, jd->jd_data, recsize - sizeof(tail));
	    if (n != (int)(recsize - sizeof(tail))) {
		if (search == 0)
		    jf_warn(jf, "Incomplete stream record\n");
		search = 1;
		jalign(jf, direction);
		free(jd);
		continue;
	    }

	    headp = (void *)jd->jd_data;
	    if (headp->begmagic != JREC_BEGMAGIC) {
		if (search == 0)
		    jf_warn(jf, "bad begmagic, searching for new record");
		search = 1;
		jalign(jf, direction);
		free(jd);
		continue;
	    }

	    /*
	     * Skip pad records.
	     */
	    if (head.streamid == JREC_STREAMID_PAD) {
		free(jd);
		continue;
	    }

	    /*
	     * note: recsize is aligned (the actual record size),
	     * head.recsize is unaligned (the actual payload size).
	     */
	    jd->jd_transid = headp->transid;
	    jd->jd_alloc = allocsize;
	    jd->jd_size = recsize;
	    jd->jd_seq = jf->jf_seq;
	    jd->jd_pos = jf->jf_pos;
	    jd->jd_refs = 1;
	    return(jd);
	}
    }

    /*
     * If reading in prefix mode and there is no more data, close the 
     * current descriptor, adjust the sequence number, and loop.
     *
     * If we hit the end of the sequence space and were asked to loop,
     * check for the next sequence number and adjust jf_seq_end.  Leave
     * the current descriptor open so we do not loose track of its seek
     * position, and also to catch a race where another jscan may have
     * written more data to the current sequence number before rolling
     * the next sequence number.
     */
    if (error == 0 && jf->jf_prefix) {
	if (direction == JD_FORWARDS) {
	    if (jf->jf_seq < jf->jf_seq_end) {
		++jf->jf_seq;
		if (verbose_opt)
		    fprintf(stderr, "jread: roll to seq %08x\n", jf->jf_seq);
		if (jf->jf_fd >= 0) {
		    close(jf->jf_fd);
		    jf->jf_fd = -1;
		}
		goto top;
	    }
	    if (jmodes & JMODEF_LOOP_FOREVER) {
		asprintf(&filename, "%s.%08x", jf->jf_prefix, jf->jf_seq + 1);
		if (stat(filename, &st) == 0) {
		    ++jf->jf_seq_end;
		    if (verbose_opt)
			fprintf(stderr, "jread: roll seq_end to %08x\n",
					 jf->jf_seq_end);
		} else {
		    sleep(5);
		}
		goto top;
	    }
	} else {
	    if (jf->jf_seq > jf->jf_seq_beg) {
		--jf->jf_seq;
		if (verbose_opt)
		    fprintf(stderr, "jread: roll to seq %08x\n", jf->jf_seq);
		if (jf->jf_fd >= 0) {
		    close(jf->jf_fd);
		    jf->jf_fd = -1;
		}
		goto top;
	    }
	}
    }

    /*
     * If we hit EOF and were asked to loop forever on the input, leave
     * the current descriptor open, sleep, and loop.
     *
     * We have already handled the prefix case.  This feature only works
     * when doing forward scans and the input is not a pipe.
     */
    if (error == 0 && jf->jf_prefix == NULL &&
	(jmodes & JMODEF_LOOP_FOREVER) &&
	!(jmodes & JMODEF_INPUT_PIPE) && 
	direction == JD_FORWARDS
    ) {
	sleep(5);
	goto top;
    }

    /*
     * Otherwise there are no more records and we are done.
     */
    return(NULL);
}

/*
 * Write a record out.  If this is a prefix set and the file would
 * exceed record_size, we rotate into a new sequence number.
 */
void
jwrite(struct jfile *jf, struct jdata *jd)
{
    struct stat st;
    char *path;
    int n;

    assert(jf->jf_prefix);

again:
    /*
     * Open/create a new file in the prefix set
     */
    if (jf->jf_write_fd < 0) {
	asprintf(&path, "%s.%08x", jf->jf_prefix, jf->jf_seq_end);
	jf->jf_write_fd = open(path, O_RDWR|O_CREAT, 0666);
	if (jf->jf_write_fd < 0 || fstat(jf->jf_write_fd, &st) != 0) {
	    fprintf(stderr, "Unable to open/create %s\n", path);
	    exit(1);
	}
	jf->jf_write_pos = st.st_size;
	lseek(jf->jf_write_fd, jf->jf_write_pos, 0);
	free(path);
    }

    /*
     * Each file must contain at least one raw record, even if it exceeds
     * the user-requested record-size.  Apart from that, we cycle to the next
     * file when its size would exceed the user-specified 
     */
    if (jf->jf_write_pos > 0 && 
	jf->jf_write_pos + jd->jd_size > prefix_file_size
    ) {
	close(jf->jf_write_fd);
	jf->jf_write_fd = -1;
	++jf->jf_seq_end;
	goto again;
    }

    /*
     * Terminate if a failure occurs (for now).
     */
    n = write(jf->jf_write_fd, jd->jd_data, jd->jd_size);
    if (n != jd->jd_size) {
	ftruncate(jf->jf_write_fd, jf->jf_write_pos);
	fprintf(stderr, "jwrite: failed %s\n", strerror(errno));
	exit(1);
    }
    jf->jf_write_pos += n;
    jf->jf_last_transid = jd->jd_transid;
}

/*
 * Attempt to locate and return the record specified by the transid.  The
 * returned record may be inexact.
 *
 * If scanning forwards this function guarentees that no record prior
 * to the returned record is >= transid.
 *
 * If scanning backwards this function guarentees that no record after
 * the returned record is <= transid.
 */
struct jdata *
jseek(struct jfile *jf, int64_t transid, enum jdirection direction)
{
    unsigned int seq;
    struct jdata *jd = NULL;

    /*
     * If the input is a pipe we can't seek.
     */
    if (jmodes & JMODEF_INPUT_PIPE) {
	assert(direction == JD_FORWARDS);
	return (jread(jf, NULL, direction));
    }

    if (jf->jf_prefix) {
	/*
	 * If we have a prefix set search the sequence space backwards until
	 * we find the file most likely to contain the transaction id.
	 */
	if (verbose_opt > 2) {
	    fprintf(stderr, "jseek prefix set %s %08x-%08x\n", jf->jf_prefix,
		    jf->jf_seq_beg, jf->jf_seq_end);
	}
	jd = NULL;
	for (seq = jf->jf_seq_end; seq != jf->jf_seq_beg - 1; --seq) {
	    if (verbose_opt > 2)
		fprintf(stderr, "try seq %08x\n", seq);
	    jf->jf_seq = seq;
	    if ((jd = jread(jf, NULL, JD_SEQFIRST)) != NULL) {
		if (jd->jd_transid == transid)
		    return(jd);
		if (jd->jd_transid < transid) {
		    jfree(jf, jd);
		    break;
		}
		jfree(jf, jd);
	    }
	}

	/*
	 * if transid is less the first file in the sequence space we
	 * return NULL if scanning backwards, indicating no records are
	 * available, or the first record in the sequence space if we
	 * are scanning forwards.
	 */
	if (seq == jf->jf_seq_beg - 1) {
	    if (direction == JD_BACKWARDS)
		return(NULL);
	    else
		return(jread(jf, NULL, JD_FORWARDS));
	}
	if (verbose_opt > 1)
	    fprintf(stderr, "jseek input prefix set to seq %08x\n", seq);
    }

    /*
     * Position us to the end of the current record, then scan backwards
     * looking for the requested transid.
     */
    jd = jread(jf, NULL, JD_SEQLAST);
    while (jd != NULL) {
	if (jd->jd_transid <= transid) {
	    if (jd->jd_transid < transid) {
		if (direction == JD_FORWARDS)
		    jd =jread(jf, jd, JD_FORWARDS);
	    }
	    if (verbose_opt > 1) {
		fprintf(stderr, "jseek returning seq %08x offset 0x%08jx\n",
			jd->jd_seq, (uintmax_t)jd->jd_pos);
	    }
	    return(jd);
	}
	jd = jread(jf, jd, JD_BACKWARDS);
    }

    /*
     * We scanned the whole file with no luck, all the transid's are
     * greater then the requested transid.  If the intended read 
     * direction is backwards there are no records and we return NULL.
     * If it is forwards we return the first record.
     */
    if (direction == JD_BACKWARDS)
	return(NULL);
    else
	return(jread(jf, NULL, JD_FORWARDS));
}

/*
 * Data returned by jread() is persistent until released.
 */
struct jdata *
jref(struct jdata *jd)
{
    ++jd->jd_refs;
    return(jd);
}

void
jfree(struct jfile *jf __unused, struct jdata *jd)
{
    if (--jd->jd_refs == 0)
	free(jd);
}

/*
 * Align us to the next 16 byte boundary.  If scanning forwards we align
 * forwards if not already aligned.  If scanning backwards we align
 * backwards if not already aligned.  We only have to synchronize the
 * seek position with the file seek position for forward scans.
 */
static void
jalign(struct jfile *jf, enum jdirection direction)
{
    char dummy[16];
    int bytes;

    if ((int)jf->jf_pos & 15) {
	if (direction == JD_FORWARDS) {
	    bytes = 16 - ((int)jf->jf_pos & 15);
	    jreadbuf(jf, direction, dummy, bytes);
	} else {
	    jf->jf_pos = jf->jf_pos & ~(off_t)15;
	}
    }
}

/*
 * Read the next raw journal record forwards or backwards and return a
 * pointer to it.  Note that the file pointer's actual seek position does
 * not match jf_pos in the reverse direction case.
 */
static int
jreadbuf(struct jfile *jf, enum jdirection direction, void *buf, int bytes)
{
    int ttl = 0;
    int n;

    if (jf->jf_fd < 0)
	return(0);

    if (direction == JD_FORWARDS) {
	while (ttl != bytes) {
	    n = read(jf->jf_fd, (char *)buf + ttl, bytes - ttl);
	    if (n <= 0) {
		if (n < 0 && ttl == 0)
		    ttl = -errno;
		break;
	    }
	    ttl += n;
	    jf->jf_pos += n;
	}
    } else {
	if (jf->jf_pos >= bytes) {
	    jf->jf_pos -= bytes;
	    lseek(jf->jf_fd, jf->jf_pos, 0);
	    while (ttl != bytes) {
		n = read(jf->jf_fd, (char *)buf + ttl, bytes - ttl);
		if (n <= 0) {
		    if (n < 0 && ttl == 0)
			ttl = -errno;
		    break;
		}
		ttl += n;
	    }
	}
    }
    return(ttl);
}

