/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/jscan/jsession.c,v 1.1 2005/09/06 18:43:52 dillon Exp $
 */

#include "jscan.h"

void
jsession_init(struct jsession *ss, struct jfile *jfin,
	      const  char *transid_file, int64_t transid)
{
    bzero(ss, sizeof(*ss));
    ss->ss_jfin = jfin;
    ss->ss_transid = transid;
    ss->ss_transid_file = transid_file;
    ss->ss_transid_fd = -1;
}

void
jsession_update_transid(struct jsession *ss __unused, int64_t transid)
{
    char buf[32];

    if (ss->ss_transid_file) {
	if (ss->ss_transid_fd < 0) {
	    ss->ss_transid_fd = open(ss->ss_transid_file, O_RDWR|O_CREAT, 0666);
	}
	if (ss->ss_transid_fd < 0) {
	    fprintf(stderr, "Cannot open/create %s\n", ss->ss_transid_file);
	    exit(1);
	}
	snprintf(buf, sizeof(buf), "%016llx\n", transid);
	lseek(ss->ss_transid_fd, 0L, 0);
	write(ss->ss_transid_fd, buf, strlen(buf));
	if (fsync_opt > 1)
	    fsync(ss->ss_transid_fd);
    }
}

void
jsession_term(struct jsession *ss)
{
   if (ss->ss_transid_fd >= 0) {
	close(ss->ss_transid_fd);
	ss->ss_transid_fd = -1;
   }
}

