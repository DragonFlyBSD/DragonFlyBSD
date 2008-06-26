/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/hammer/cmd_mirror.c,v 1.1 2008/06/26 04:07:57 dillon Exp $
 */

#include "hammer.h"

#define SERIALBUF_SIZE	(512 * 1024)

static int read_mrecords(int fd, char *buf, u_int size,
			 hammer_ioc_mrecord_t pickup);
static void mirror_usage(int code);

void
hammer_cmd_mirror_read(char **av, int ac)
{
	struct hammer_ioc_mirror_rw mirror;
	const char *filesystem;
	char *buf = malloc(SERIALBUF_SIZE);
	int fd;
	hammer_tid_t tid;

	if (ac > 2)
		mirror_usage(1);
	filesystem = av[0];
	tid = 0;
	if (ac == 2)
		tid = strtoull(av[1], NULL, 0);

	bzero(&mirror, sizeof(mirror));
	hammer_key_beg_init(&mirror.key_beg);
	hammer_key_end_init(&mirror.key_end);

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "Unable to open %s", filesystem);

	hammer_get_cycle(&mirror.key_beg);

	mirror.ubuf = buf;
	mirror.size = SERIALBUF_SIZE;
	mirror.tid_beg = tid;
	mirror.tid_end = HAMMER_MAX_TID;

	do {
		mirror.count = 0;
		if (ioctl(fd, HAMMERIOC_MIRROR_READ, &mirror) < 0) {
			fprintf(stderr, "Mirror-read %s failed: %s\n",
				filesystem, strerror(errno));
			exit(1);
		}
		if (mirror.head.flags & HAMMER_IOC_HEAD_INTR) {
			fprintf(stderr,
				"Mirror-read %s interrupted by timer at"
				" %016llx %08x\n",
				filesystem,
				mirror.key_cur.obj_id,
				mirror.key_cur.localization);
			if (CyclePath)
				hammer_set_cycle(&mirror.key_cur);
			exit(0);
		}
		mirror.key_beg = mirror.key_cur;
		if (mirror.count)
			write(1, mirror.ubuf, mirror.count);
	} while (mirror.count != 0);

	if (CyclePath)
		hammer_reset_cycle();
	fprintf(stderr, "Mirror-read %s succeeded\n", filesystem);
}

void
hammer_cmd_mirror_write(char **av, int ac)
{
	struct hammer_ioc_mirror_rw mirror;
	const char *filesystem;
	char *buf = malloc(SERIALBUF_SIZE);
	int fd;
	struct hammer_ioc_mrecord pickup;
	hammer_tid_t tid;

	if (ac > 2)
		mirror_usage(1);
	filesystem = av[0];
	tid = 0;
	if (ac == 2)
		tid = strtoull(av[1], NULL, 0);

	bzero(&mirror, sizeof(mirror));
	hammer_key_beg_init(&mirror.key_beg);
	hammer_key_end_init(&mirror.key_end);

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "Unable to open %s", filesystem);

	mirror.ubuf = buf;
	mirror.size = SERIALBUF_SIZE;
	mirror.tid_beg = tid;
	mirror.tid_end = HAMMER_MAX_TID;

	pickup.signature = 0;

	for (;;) {
		mirror.count = 0;
		mirror.size = read_mrecords(0, buf, SERIALBUF_SIZE, &pickup);
		if (mirror.size <= 0)
			break;
		if (ioctl(fd, HAMMERIOC_MIRROR_WRITE, &mirror) < 0) {
			fprintf(stderr, "Mirror-write %s failed: %s\n",
				filesystem, strerror(errno));
			exit(1);
		}
		if (mirror.head.flags & HAMMER_IOC_HEAD_INTR) {
			fprintf(stderr,
				"Mirror-write %s interrupted by timer at"
				" %016llx %08x\n",
				filesystem,
				mirror.key_cur.obj_id,
				mirror.key_cur.localization);
			exit(0);
		}
		mirror.key_beg = mirror.key_cur;
	}
	fprintf(stderr, "Mirror-write %s succeeded\n", filesystem);
}

void
hammer_cmd_mirror_copy(char **av, int ac)
{
}

static int
read_mrecords(int fd, char *buf, u_int size, hammer_ioc_mrecord_t pickup)
{
	u_int count;
	size_t n;
	size_t i;

	count = 0;
	while (size - count >= HAMMER_MREC_HEADSIZE) {
		/*
		 * Cached the record header in case we run out of buffer
		 * space.
		 */
		if (pickup->signature == 0) {
			for (n = 0; n < HAMMER_MREC_HEADSIZE; n += i) {
				i = read(fd, (char *)pickup + n,
					 HAMMER_MREC_HEADSIZE - n);
				if (i <= 0)
					break;
			}
			if (n == 0)
				break;
			if (n != HAMMER_MREC_HEADSIZE) {
				fprintf(stderr, "read_mrecords: short read on pipe\n");
				exit(1);
			}

			if (pickup->signature != HAMMER_IOC_MIRROR_SIGNATURE) {
				fprintf(stderr, "read_mrecords: malformed record on pipe, bad signature\n");
				exit(1);
			}
			if (pickup->rec_crc != crc32((char *)pickup + HAMMER_MREC_CRCOFF, HAMMER_MREC_HEADSIZE - HAMMER_MREC_CRCOFF)) {
				fprintf(stderr, "read_mrecords: malformed record on pipe, bad crc\n");
				exit(1);
			}
		}
		if (pickup->rec_size < HAMMER_MREC_HEADSIZE ||
		    pickup->rec_size > HAMMER_MREC_HEADSIZE + HAMMER_XBUFSIZE) {
			fprintf(stderr, "read_mrecords: malformed record on pipe, illegal rec_size\n");
			exit(1);
		}
		if (HAMMER_MREC_HEADSIZE + pickup->leaf.data_len > pickup->rec_size) {
			fprintf(stderr, "read_mrecords: malformed record on pipe, illegal element data_len\n");
			exit(1);
		}

		/*
		 * Stop if we have insufficient space for the record and data.
		 */
		if (size - count < pickup->rec_size)
			break;

		/*
		 * Read the remainder and clear the pickup signature.
		 */
		bcopy(pickup, buf + count, HAMMER_MREC_HEADSIZE);
		pickup->signature = 0;
		for (n = HAMMER_MREC_HEADSIZE; n < pickup->rec_size; n += i) {
			i = read(fd, buf + count + n, pickup->rec_size - n);
			if (i <= 0)
				break;
		}
		if (n != pickup->rec_size) {
			fprintf(stderr, "read_mrecords: short read on pipe\n");
			exit(1);
		}
		if (pickup->leaf.data_len && pickup->leaf.data_offset) {
			if (hammer_crc_test_leaf(buf + count + HAMMER_MREC_HEADSIZE, &pickup->leaf) == 0) {
				fprintf(stderr, "read_mrecords: data_crc did not match data! obj=%016llx key=%016llx\n", pickup->leaf.base.obj_id, pickup->leaf.base.key);
				fprintf(stderr, "continuing, but there are problems\n");
			}
		}

		count += pickup->rec_size;
	}
	return(count);
}

static void
mirror_usage(int code)
{
	fprintf(stderr, 
		"hammer mirror-read <filesystem>\n"
		"hammer mirror-write <filesystem>\n"
		"hammer mirror-copy [[user@]host:]fs [[user@]host:]fs\n"
	);
	exit(code);
}
