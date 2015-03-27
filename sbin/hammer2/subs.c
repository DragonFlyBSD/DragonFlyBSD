/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
 */

#include "hammer2.h"

/*
 * Obtain a file descriptor that the caller can execute ioctl()'s on.
 */
int
hammer2_ioctl_handle(const char *sel_path)
{
	struct hammer2_ioc_version info;
	int fd;

	if (sel_path == NULL)
		sel_path = ".";

	fd = open(sel_path, O_RDONLY, 0);
	if (fd < 0) {
		fprintf(stderr, "hammer2: Unable to open %s: %s\n",
			sel_path, strerror(errno));
		return(-1);
	}
	if (ioctl(fd, HAMMER2IOC_VERSION_GET, &info) < 0) {
		fprintf(stderr, "hammer2: '%s' is not a hammer2 filesystem\n",
			sel_path);
		close(fd);
		return(-1);
	}
	return (fd);
}

/*
 * Execute the specified function as a detached independent process/daemon,
 * unless we are in debug mode.  If we are in debug mode the function is
 * executed as a pthread in the current process.
 */
void
hammer2_demon(void *(*func)(void *), void *arg)
{
	pthread_t thread = NULL;
	pid_t pid;
	int ttyfd;

	/*
	 * Do not disconnect in debug mode
	 */
	if (DebugOpt) {
                pthread_create(&thread, NULL, func, arg);
		NormalExit = 0;
		return;
	}

	/*
	 * Otherwise disconnect us.  Double-fork to get rid of the ppid
	 * association and disconnect the TTY.
	 */
	if ((pid = fork()) < 0) {
		fprintf(stderr, "hammer2: fork(): %s\n", strerror(errno));
		exit(1);
	}
	if (pid > 0) {
		while (waitpid(pid, NULL, 0) != pid)
			;
		return;		/* parent returns */
	}

	/*
	 * Get rid of the TTY/session before double-forking to finish off
	 * the ppid.
	 */
	ttyfd = open("/dev/null", O_RDWR);
	if (ttyfd >= 0) {
		if (ttyfd != 0)
			dup2(ttyfd, 0);
		if (ttyfd != 1)
			dup2(ttyfd, 1);
		if (ttyfd != 2)
			dup2(ttyfd, 2);
		if (ttyfd > 2)
			close(ttyfd);
	}

	ttyfd = open("/dev/tty", O_RDWR);
	if (ttyfd >= 0) {
		ioctl(ttyfd, TIOCNOTTY, 0);
		close(ttyfd);
	}
	setsid();

	/*
	 * Second fork to disconnect ppid (the original parent waits for
	 * us to exit).
	 */
	if ((pid = fork()) < 0) {
		_exit(2);
	}
	if (pid > 0)
		_exit(0);

	/*
	 * The double child
	 */
	setsid();
	pthread_create(&thread, NULL, func, arg);
	pthread_exit(NULL);
	_exit(2);	/* NOT REACHED */
}

const char *
hammer2_time64_to_str(uint64_t htime64, char **strp)
{
	struct tm *tp;
	time_t t;

	if (*strp) {
		free(*strp);
		*strp = NULL;
	}
	*strp = malloc(64);
	t = htime64 / 1000000;
	tp = localtime(&t);
	strftime(*strp, 64, "%d-%b-%Y %H:%M:%S", tp);
	return (*strp);
}

const char *
hammer2_uuid_to_str(uuid_t *uuid, char **strp)
{
	uint32_t status;
	if (*strp) {
		free(*strp);
		*strp = NULL;
	}
	uuid_to_string(uuid, strp, &status);
	return (*strp);
}

const char *
hammer2_iptype_to_str(uint8_t type)
{
	switch(type) {
	case HAMMER2_OBJTYPE_UNKNOWN:
		return("UNKNOWN");
	case HAMMER2_OBJTYPE_DIRECTORY:
		return("DIR");
	case HAMMER2_OBJTYPE_REGFILE:
		return("FILE");
	case HAMMER2_OBJTYPE_FIFO:
		return("FIFO");
	case HAMMER2_OBJTYPE_CDEV:
		return("CDEV");
	case HAMMER2_OBJTYPE_BDEV:
		return("BDEV");
	case HAMMER2_OBJTYPE_SOFTLINK:
		return("SOFTLINK");
	case HAMMER2_OBJTYPE_HARDLINK:
		return("HARDLINK");
	case HAMMER2_OBJTYPE_SOCKET:
		return("SOCKET");
	case HAMMER2_OBJTYPE_WHITEOUT:
		return("WHITEOUT");
	default:
		return("ILLEGAL");
	}
}

const char *
hammer2_pfstype_to_str(uint8_t type)
{
	switch(type) {
	case HAMMER2_PFSTYPE_NONE:
		return("NONE");
	case HAMMER2_PFSTYPE_CACHE:
		return("CACHE");
	case HAMMER2_PFSTYPE_COPY:
		return("COPY");
	case HAMMER2_PFSTYPE_SLAVE:
		return("SLAVE");
	case HAMMER2_PFSTYPE_SOFT_SLAVE:
		return("SOFT_SLAVE");
	case HAMMER2_PFSTYPE_SOFT_MASTER:
		return("SOFT_MASTER");
	case HAMMER2_PFSTYPE_MASTER:
		return("MASTER");
	default:
		return("ILLEGAL");
	}
}

const char *
sizetostr(hammer2_off_t size)
{
	static char buf[32];

	if (size < 1024 / 2) {
		snprintf(buf, sizeof(buf), "%6.2f", (double)size);
	} else if (size < 1024 * 1024 / 2) {
		snprintf(buf, sizeof(buf), "%6.2fKB",
			(double)size / 1024);
	} else if (size < 1024 * 1024 * 1024LL / 2) {
		snprintf(buf, sizeof(buf), "%6.2fMB",
			(double)size / (1024 * 1024));
	} else if (size < 1024 * 1024 * 1024LL * 1024LL / 2) {
		snprintf(buf, sizeof(buf), "%6.2fGB",
			(double)size / (1024 * 1024 * 1024LL));
	} else {
		snprintf(buf, sizeof(buf), "%6.2fTB",
			(double)size / (1024 * 1024 * 1024LL * 1024LL));
	}
	return(buf);
}

const char *
counttostr(hammer2_off_t size)
{
	static char buf[32];

	if (size < 1024 / 2) {
		snprintf(buf, sizeof(buf), "%jd",
			 (intmax_t)size);
	} else if (size < 1024 * 1024 / 2) {
		snprintf(buf, sizeof(buf), "%jd",
			 (intmax_t)size);
	} else if (size < 1024 * 1024 * 1024LL / 2) {
		snprintf(buf, sizeof(buf), "%6.2fM",
			 (double)size / (1024 * 1024));
	} else if (size < 1024 * 1024 * 1024LL * 1024LL / 2) {
		snprintf(buf, sizeof(buf), "%6.2fG",
			 (double)(size / (1024 * 1024 * 1024LL)));
	} else {
		snprintf(buf, sizeof(buf), "%6.2fT",
			 (double)(size / (1024 * 1024 * 1024LL * 1024LL)));
	}
	return(buf);
}

#if 0
/*
 * Allocation wrappers give us shims for possible future use
 */
void *
hammer2_alloc(size_t bytes)
{
	void *ptr;

	ptr = malloc(bytes);
	assert(ptr);
	bzero(ptr, bytes);
	return (ptr);
}

void
hammer2_free(void *ptr)
{
	free(ptr);
}

#endif

hammer2_key_t
dirhash(const unsigned char *name, size_t len)
{
	const unsigned char *aname = name;
	uint32_t crcx;
	uint64_t key;
	size_t i;
	size_t j;

	/*
	 * Filesystem version 6 or better will create directories
	 * using the ALG1 dirhash.  This hash breaks the filename
	 * up into domains separated by special characters and
	 * hashes each domain independently.
	 *
	 * We also do a simple sub-sort using the first character
	 * of the filename in the top 5-bits.
	 */
	key = 0;

	/*
	 * m32
	 */
	crcx = 0;
	for (i = j = 0; i < len; ++i) {
		if (aname[i] == '.' ||
		    aname[i] == '-' ||
		    aname[i] == '_' ||
		    aname[i] == '~') {
			if (i != j)
				crcx += hammer2_icrc32(aname + j, i - j);
			j = i + 1;
		}
	}
	if (i != j)
		crcx += hammer2_icrc32(aname + j, i - j);

	/*
	 * The directory hash utilizes the top 32 bits of the 64-bit key.
	 * Bit 63 must be set to 1.
	 */
	crcx |= 0x80000000U;
	key |= (uint64_t)crcx << 32;

	/*
	 * l16 - crc of entire filename
	 *
	 * This crc reduces degenerate hash collision conditions
	 */
	crcx = hammer2_icrc32(aname, len);
	crcx = crcx ^ (crcx << 16);
	key |= crcx & 0xFFFF0000U;

	/*
	 * Set bit 15.  This allows readdir to strip bit 63 so a positive
	 * 64-bit cookie/offset can always be returned, and still guarantee
	 * that the values 0x0000-0x7FFF are available for artificial entries.
	 * ('.' and '..').
	 */
	key |= 0x8000U;

	return (key);
}
