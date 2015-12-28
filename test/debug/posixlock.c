/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/test/debug/posixlock.c,v 1.1 2006/05/08 00:30:41 dillon Exp $
 */

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static void prompt(void);

int
main(int ac, char **av)
{
    struct flock lk;
    off_t save_start;
    off_t save_len;
    char c;
    int r;
    int fd;
    char buf[256];

    if ((fd = open("test", O_CREAT|O_RDWR, 0666)) < 0) {
	perror("open");
	exit(1);
    }
    prompt();
    while (fgets(buf, sizeof(buf), stdin) != NULL) {
	bzero(&lk, sizeof(lk));
	c = '#';
	sscanf(buf, "%c %ld %ld", &c, &lk.l_start, &lk.l_len);

	save_start = lk.l_start;
	save_len = lk.l_len;

	switch(c) {
	case 'l':
	    lk.l_type = F_WRLCK;
	    while ((r = fcntl(fd, F_GETLK, &lk)) == 0) {
		printf("%5d %c %jd %jd\n",
			(int)lk.l_pid,
			((lk.l_type == F_WRLCK) ? 'x' :
			 (lk.l_type == F_RDLCK) ? 'r' :
			 (lk.l_type == F_UNLCK) ? 'u' : '?'),
			lk.l_start,
			lk.l_len
		);
		if (lk.l_len == 0)
			break;
		lk.l_start += lk.l_len;
		if (save_len)
			lk.l_len = save_len - (lk.l_start - save_start);
		else
			lk.l_len = 0;
		lk.l_type = F_WRLCK;
	    }
	    if (r < 0)
		printf("%s\n", strerror(errno));
	    break;
	case 's':
	    lk.l_type = F_RDLCK;
	    if (fcntl(fd, F_SETLKW, &lk) == 0) {
		printf("ok\n");
	    } else {
		printf("%s\n", strerror(errno));
	    }
	    break;
	case 'x':
	    lk.l_type = F_WRLCK;
	    if (fcntl(fd, F_SETLKW, &lk) == 0) {
		printf("ok\n");
	    } else {
		printf("%s\n", strerror(errno));
	    }
	    break;
	case 'u':
	    lk.l_type = F_UNLCK;
	    if (fcntl(fd, F_SETLKW, &lk) == 0) {
		printf("ok\n");
	    } else {
		printf("%s\n", strerror(errno));
	    }
	    break;
	case '?':
		printf(
			"l start len\tlist locks\n"
			"s start len\tobtain a read lock\n"
			"x start len\tobtain an exclusive lock\n"
			"u start len\tounlock a range\n"
		);
		break;
	case '\n':
	case '\0':
	case '#':
		break;
	default:
		printf("unknown command '%c'\n", c);
		break;
	}
	prompt();
    }
    return(0);
}

static
void
prompt(void)
{
    printf("locker> ");
    fflush(stdout);
}
