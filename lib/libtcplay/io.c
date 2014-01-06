/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
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

#include <sys/types.h>
#if defined(__DragonFly__)
#include <sys/diskslice.h>
#elif defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif
#include <sys/uio.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "tcplay.h"

void *
read_to_safe_mem(const char *file, off_t offset, size_t *sz)
{
	void *mem = NULL;
	ssize_t r = 0;
	int fd;

	if ((fd = open(file, O_RDONLY)) < 0) {
		tc_log(1, "Error opening file %s\n", file);
		return NULL;
	}

	if ((mem = alloc_safe_mem(*sz)) == NULL) {
		tc_log(1, "Error allocating memory\n");
		goto out;
	}

	if ((lseek(fd, offset, SEEK_SET) < 0)) {
		tc_log(1, "Error seeking on file %s\n", file);
		goto m_err;
	}

	if ((r = read(fd, mem, *sz)) <= 0) {
		tc_log(1, "Error reading from file %s\n", file);
		goto m_err;
	}

out:
	*sz = r;
	close(fd);
	return mem;
	/* NOT REACHED */

m_err:
	free_safe_mem(mem);
	close(fd);
	return NULL;
}

static size_t get_random_total_bytes = 0;
static size_t get_random_read_bytes = 0;

static
void
get_random_summary(void)
{
	float pct_done;

	pct_done = (1.0 * get_random_read_bytes) /
	    (1.0 * get_random_total_bytes) * 100.0;
	tc_log(0, "Gathering true randomness, %.0f%% done.\n", pct_done);
}

int
get_random(unsigned char *buf, size_t len)
{
	int fd;
	ssize_t r;
	size_t rd = 0;
	size_t sz;
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 }; /* 10 ms */


	if ((fd = open("/dev/random", O_RDONLY)) < 0) {
		tc_log(1, "Error opening /dev/random\n");
		return -1;
	}

	summary_fn = get_random_summary;
	get_random_total_bytes = len;

	/* Get random data in 16-byte chunks */
	sz = 16;
	while (rd < len) {
		get_random_read_bytes = rd;

		if ((len - rd) < sz)
			sz = (len - rd);

		if ((r = read(fd, buf+rd, sz)) < 0) {
			tc_log(1, "Error reading from /dev/random(%d): %s\n",
			    fd, strerror(errno));
			close(fd);
			summary_fn = NULL;
			return -1;
		}
		rd += r;
		nanosleep(&ts, NULL);
	}

	close(fd);
	summary_fn = NULL;

	return 0;
}

static size_t secure_erase_total_bytes = 0;
static size_t secure_erase_erased_bytes = 0;

static
void
secure_erase_summary(void)
{
	float pct_done;

	pct_done = (1.0 * secure_erase_erased_bytes) /
	    (1.0 * secure_erase_total_bytes) * 100.0;
	tc_log(0, "Securely erasing, %.0f%% done.\n", pct_done);
}

int
secure_erase(const char *dev, size_t bytes, size_t blksz)
{
	size_t erased = 0;
	int fd_rand, fd;
	char buf[ERASE_BUFFER_SIZE];
	ssize_t r, w;
	size_t sz;

	if (blksz > MAX_BLKSZ) {
		tc_log(1, "blksz > MAX_BLKSZ\n");
		return -1;
	}

	if ((fd_rand = open("/dev/urandom", O_RDONLY)) < 0) {
		tc_log(1, "Error opening /dev/urandom\n");
		return -1;
	}

	if ((fd = open(dev, O_WRONLY)) < 0) {
		close(fd_rand);
		tc_log(1, "Error opening %s\n", dev);
		return -1;
	}

	summary_fn = secure_erase_summary;
	secure_erase_total_bytes = bytes;

	sz = ERASE_BUFFER_SIZE;
	while (erased < bytes) {
		secure_erase_erased_bytes = erased;
		/* Switch to block size when not much is remaining */
		if ((bytes - erased) <= ERASE_BUFFER_SIZE)
			sz = blksz;

		if ((r = read(fd_rand, buf, sz)) < 0) {
			tc_log(1, "Error reading from /dev/urandom\n");
			close(fd);
			close(fd_rand);
			summary_fn = NULL;
			return -1;
		}

		if (r < (ssize_t)blksz)
			continue;

		if ((w = write(fd, buf, r)) < 0) {
			tc_log(1, "Error writing to %s\n", dev);
			close(fd);
			close(fd_rand);
			summary_fn = NULL;
			return -1;
		}

		erased += (size_t)w;
	}

	close(fd);
	close(fd_rand);

	summary_fn = NULL;

	return 0;
}

#if defined(__DragonFly__)
int
get_disk_info(const char *dev, size_t *blocks, size_t *bsize)
{
	struct partinfo pinfo;
	int fd;

	if ((fd = open(dev, O_RDONLY)) < 0) {
		tc_log(1, "Error opening %s\n", dev);
		return -1;
	}

	memset(&pinfo, 0, sizeof(struct partinfo));

	if (ioctl(fd, DIOCGPART, &pinfo) < 0) {
		close(fd);
		return -1;
	}

	*blocks = pinfo.media_blocks;
	*bsize = pinfo.media_blksize;

	close(fd);
	return 0;
}
#elif defined(__linux__)
int
get_disk_info(const char *dev, size_t *blocks, size_t *bsize)
{
	uint64_t nbytes;
	int blocksz;
	int fd;

	if ((fd = open(dev, O_RDONLY)) < 0) {
		tc_log(1, "Error opening %s\n", dev);
		return -1;
	}

	if ((ioctl(fd, BLKSSZGET, &blocksz)) < 0) {
		close(fd);
		return -1;
	}

	if ((ioctl(fd, BLKGETSIZE64, &nbytes)) < 0) {
		close(fd);
		return -1;
	}

	*blocks = (size_t)(nbytes / blocksz);
	*bsize = (size_t)(blocksz);

	close(fd);
	return 0;
}
#endif

int
write_to_disk(const char *dev, off_t offset, size_t blksz, void *mem,
    size_t bytes)
{
	unsigned char *mem_buf = NULL;
	ssize_t w;
	size_t sz;
	off_t internal_off;
	int fd;

	/* Align to block sizes */
	internal_off = offset % blksz;
#ifdef DEBUG
	printf("offset: %"PRIu64", internal offset: %"PRIu64"\n",
	    (uint64_t)offset, (uint64_t)internal_off);
#endif
	offset = (offset/blksz) * blksz;

	if ((internal_off + bytes) > blksz) {
		tc_log(1, "This should never happen: internal_off + bytes > "
		    "blksz (write_to_disk)\n");
		return -1;
	}

	if ((bytes < blksz) || (internal_off != 0)) {
		sz = blksz;
		if ((mem_buf = read_to_safe_mem(dev, offset, &sz)) == NULL) {
			tc_log(1, "Error buffering data on "
			    "write_to_disk(%s)\n", dev);
			return -1;
		}

		memcpy(mem_buf + internal_off, mem, bytes);
	}

	if ((fd = open(dev, O_WRONLY)) < 0) {
		tc_log(1, "Error opening device %s\n", dev);
		return -1;
	}

	if ((lseek(fd, offset, SEEK_SET) < 0)) {
		tc_log(1, "Error seeking on device %s\n", dev);
		close(fd);
		return -1;
	}

	if ((w = write(fd, (mem_buf != NULL) ? mem_buf : mem, bytes)) <= 0) {
		tc_log(1, "Error writing to device %s\n", dev);
		close(fd);
		return -1;
	}

	close(fd);

	if (mem_buf != NULL)
		free_safe_mem(mem_buf);
	return 0;
}

int
read_passphrase(const char *prompt, char *pass, size_t passlen, time_t timeout)
{
	struct termios termios_old, termios_new;
	struct timeval to;
	fd_set fds;
	ssize_t n;
	int fd, r = 0, cfd = 0, nready;

	if ((fd = open("/dev/tty", O_RDONLY)) == -1) {
		fd = STDIN_FILENO;
		cfd = 1;
	}

	printf("%s", prompt);
	fflush(stdout);

	memset(pass, 0, passlen);

	tcgetattr(fd, &termios_old);
	memcpy(&termios_new, &termios_old, sizeof(termios_new));
	termios_new.c_lflag &= ~ECHO;
	tcsetattr(fd, TCSAFLUSH, &termios_new);

	if (timeout > 0) {
		memset(&to, 0, sizeof(to));
		to.tv_sec = timeout;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		nready = select(fd + 1, &fds, NULL, NULL, &to);
		if (nready <= 0) {
			r = EINTR;
			goto out;
		}
	}

	n = read(fd, pass, passlen-1);
	if (n > 0) {
		pass[n-1] = '\0'; /* Strip trailing \n */
	} else {
		r = EIO;
	}

out:
	if (cfd)
		close(fd);

	tcsetattr(fd, TCSAFLUSH, &termios_old);
	putchar('\n');

	return r;
}
