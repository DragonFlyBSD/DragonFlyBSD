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
 * $DragonFly: src/sbin/hammer/cmd_mirror.c,v 1.3 2008/07/04 07:20:43 dillon Exp $
 */

#include "hammer.h"

#define SERIALBUF_SIZE	(512 * 1024)

struct hammer_pfs_head {
	struct hammer_ioc_mrecord mrec;
	u_int32_t version;
	struct hammer_pseudofs_data pfsd;
};

static int read_mrecords(int fd, char *buf, u_int size,
			 hammer_ioc_mrecord_t pickup);
static void generate_mrec_header(int fd, int fdout,
			 hammer_tid_t *tid_begp, hammer_tid_t *tid_endp);
static void validate_mrec_header(int fd, int fdin,
			 hammer_tid_t *tid_begp, hammer_tid_t *tid_endp);
static void run_cmd(const char *path, ...);
static void mirror_usage(int code);

void
hammer_cmd_mirror_read(char **av, int ac)
{
	struct hammer_ioc_mirror_rw mirror;
	const char *filesystem;
	char *buf = malloc(SERIALBUF_SIZE);
	int fd;

	if (ac > 2)
		mirror_usage(1);
	filesystem = av[0];

	bzero(&mirror, sizeof(mirror));
	hammer_key_beg_init(&mirror.key_beg);
	hammer_key_end_init(&mirror.key_end);

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "Unable to open %s", filesystem);

	hammer_get_cycle(&mirror.key_beg);

	generate_mrec_header(fd, 1, &mirror.tid_beg, &mirror.tid_end);

	mirror.ubuf = buf;
	mirror.size = SERIALBUF_SIZE;
	if (ac == 2)
		mirror.tid_beg = strtoull(av[1], NULL, 0);

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

	/* generate_mrec_update(fd, 1); */

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

	if (ac > 2)
		mirror_usage(1);
	filesystem = av[0];

	bzero(&mirror, sizeof(mirror));
	hammer_key_beg_init(&mirror.key_beg);
	hammer_key_end_init(&mirror.key_end);

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "Unable to open %s", filesystem);

	validate_mrec_header(fd, 0, &mirror.tid_beg, &mirror.tid_end);

	mirror.ubuf = buf;
	mirror.size = SERIALBUF_SIZE;

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
	pid_t pid1;
	pid_t pid2;
	int fds[2];
	char *ptr;

	if (ac != 2)
		mirror_usage(1);

	if (pipe(fds) < 0) {
		perror("pipe");
		exit(1);
	}

	/*
	 * Source
	 */
	if ((pid1 = fork()) == 0) {
		dup2(fds[0], 0);
		dup2(fds[0], 1);
		close(fds[0]);
		close(fds[1]);
		if ((ptr = strchr(av[0], ':')) != NULL) {
			*ptr++ = 0;
			run_cmd("/usr/bin/ssh", "ssh",
				av[0], "hammer mirror-read", ptr, NULL);
		} else {
			hammer_cmd_mirror_read(av, 1);
		}
		_exit(1);
	}

	/*
	 * Target
	 */
	if ((pid2 = fork()) == 0) {
		dup2(fds[1], 0);
		dup2(fds[1], 1);
		close(fds[0]);
		close(fds[1]);
		if ((ptr = strchr(av[1], ':')) != NULL) {
			*ptr++ = 0;
			run_cmd("/usr/bin/ssh", "ssh",
				av[1], "hammer mirror-write", ptr, NULL);
		} else {
			hammer_cmd_mirror_write(av + 1, 1);
		}
		_exit(1);
	}
	close(fds[0]);
	close(fds[1]);

	while (waitpid(pid1, NULL, 0) <= 0)
		;
	while (waitpid(pid2, NULL, 0) <= 0)
		;
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

/*
 * Generate a mirroring header with the pfs information of the
 * originating filesytem.
 */
static void
generate_mrec_header(int fd, int fdout,
		     hammer_tid_t *tid_begp, hammer_tid_t *tid_endp)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pfs_head pfs_head;

	bzero(&pfs, sizeof(pfs));
	bzero(&pfs_head, sizeof(pfs_head));
	pfs.ondisk = &pfs_head.pfsd;
	pfs.bytes = sizeof(pfs_head.pfsd);
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) != 0) {
		fprintf(stderr, "mirror-read: not a HAMMER fs/pseudofs!\n");
		exit(1);
	}
	if (pfs.version != HAMMER_IOC_PSEUDOFS_VERSION) {
		fprintf(stderr, "mirror-read: HAMMER pfs version mismatch!\n");
		exit(1);
	}

	/*
	 * sync_beg_tid - lowest TID on source after which a full history
	 *	 	  is available.
	 *
	 * sync_end_tid - highest fully synchronized TID from source.
	 */
	*tid_begp = pfs_head.pfsd.sync_beg_tid;
	*tid_endp = pfs_head.pfsd.sync_end_tid;

	pfs_head.version = pfs.version;
	pfs_head.mrec.signature = HAMMER_IOC_MIRROR_SIGNATURE;
	pfs_head.mrec.rec_size = sizeof(pfs_head);
	pfs_head.mrec.type = HAMMER_MREC_TYPE_PFSD;
	pfs_head.mrec.rec_crc = crc32((char *)&pfs_head + HAMMER_MREC_CRCOFF,
				      sizeof(pfs_head) - HAMMER_MREC_CRCOFF);
	write(fdout, &pfs_head, sizeof(pfs_head));
}

/*
 * Validate the pfs information from the originating filesystem
 * against the target filesystem.  shared_uuid must match.
 */
static void
validate_mrec_header(int fd, int fdin,
		     hammer_tid_t *tid_begp, hammer_tid_t *tid_endp)
{
	struct hammer_ioc_pseudofs_rw pfs;
	struct hammer_pfs_head pfs_head;
	struct hammer_pseudofs_data pfsd;
	size_t bytes;
	size_t n;
	size_t i;

	/*
	 * Get the PFSD info from the target filesystem.
	 */
	bzero(&pfs, sizeof(pfs));
	bzero(&pfsd, sizeof(pfsd));
	pfs.ondisk = &pfsd;
	pfs.bytes = sizeof(pfsd);
	if (ioctl(fd, HAMMERIOC_GET_PSEUDOFS, &pfs) != 0) {
		fprintf(stderr, "mirror-write: not a HAMMER fs/pseudofs!\n");
		exit(1);
	}
	if (pfs.version != HAMMER_IOC_PSEUDOFS_VERSION) {
		fprintf(stderr, "mirror-write: HAMMER pfs version mismatch!\n");
		exit(1);
	}

	/*
	 * Read in the PFSD header from the sender.
	 */
	for (n = 0; n < HAMMER_MREC_HEADSIZE; n += i) {
		i = read(fdin, (char *)&pfs_head + n, HAMMER_MREC_HEADSIZE - n);
		if (i <= 0)
			break;
	}
	if (n != HAMMER_MREC_HEADSIZE) {
		fprintf(stderr, "mirror-write: short read of PFS header\n");
		exit(1);
	}
	if (pfs_head.mrec.signature != HAMMER_IOC_MIRROR_SIGNATURE) {
		fprintf(stderr, "mirror-write: PFS header has bad signature\n");
		exit(1);
	}
	if (pfs_head.mrec.type != HAMMER_MREC_TYPE_PFSD) {
		fprintf(stderr, "mirror-write: Expected PFS header, got mirroring record header instead!\n");
		exit(1);
	}
	bytes = pfs_head.mrec.rec_size;
	if (bytes < HAMMER_MREC_HEADSIZE)
		bytes = (int)HAMMER_MREC_HEADSIZE;
	if (bytes > sizeof(pfs_head))
		bytes = sizeof(pfs_head);
	while (n < bytes) {
		i = read(fdin, (char *)&pfs_head + n, bytes - n);
		if (i <= 0)
			break;
		n += i;
	}
	if (n != bytes) {
		fprintf(stderr, "mirror-write: short read of PFS payload\n");
		exit(1);
	}
	if (pfs_head.version != pfs.version) {
		fprintf(stderr, "mirror-write: Version mismatch in PFS header\n");
		exit(1);
	}
	if (pfs_head.mrec.rec_size != sizeof(pfs_head)) {
		fprintf(stderr, "mirror-write: The PFS header has the wrong size!\n");
		exit(1);
	}

	/*
	 * Whew.  Ok, is the read PFS info compatible with the target?
	 */
	if (bcmp(&pfs_head.pfsd.shared_uuid, &pfsd.shared_uuid, sizeof(pfsd.shared_uuid)) != 0) {
		fprintf(stderr, "mirror-write: source and target have different shared_uuid's!\n");
		exit(1);
	}
	if ((pfsd.mirror_flags & HAMMER_PFSD_SLAVE) == 0) {
		fprintf(stderr, "mirror-write: target must be in slave mode\n");
		exit(1);
	}
	*tid_begp = pfs_head.pfsd.sync_beg_tid;
	*tid_endp = pfs_head.pfsd.sync_end_tid;
}

static void
run_cmd(const char *path, ...)
{
	va_list va;
	char *av[16];
	int n;

	va_start(va, path);
	for (n = 0; n < 16; ++n) {
		av[n] = va_arg(va, char *);
		if (av[n] == NULL)
			break;
	}
	va_end(va);
	assert(n != 16);
	execv(path, av);
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
