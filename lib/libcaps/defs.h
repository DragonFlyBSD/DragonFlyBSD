/*
 * DEFS.H
 *
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/lib/libcaps/defs.h,v 1.2 2003/12/04 22:06:19 dillon Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/stdint.h>
#include <sys/upcall.h>
#include <sys/malloc.h>
#include "thread.h"
#include <sys/thread.h>
#include <sys/msgport.h>
#include <sys/errno.h>
#include "globaldata.h"
#include "sysport.h"
#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <sys/caps.h>

#include <sys/time.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <fcntl.h>
#include <stdio.h>	/* temporary debugging */
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>	/* temporary debugging */
#include <assert.h>

#define CAPS_PATH1	"/var/caps/root/%s"
#define CAPS_PATH2	"/var/caps/users/%d/%s"

#ifdef CAPS_DEBUG
#define DBPRINTF(x)	printf x
#else
#define DBPRINTF(x)
#endif

struct caps_creds_cmsg {
	struct cmsghdr  cmsg;
	struct cmsgcred cred;
};

caps_port_t caps_mkport(enum caps_type type,
    int (*cs_putport)(lwkt_port_t port, lwkt_msg_t msg),
    void *(*cs_waitport)(lwkt_port_t port, lwkt_msg_t msg),
    void (*cs_replyport)(lwkt_port_t port, lwkt_msg_t msg));

void caps_shutdown(caps_port_t port);
void caps_close(caps_port_t port);
void caps_kev_write(caps_port_t port, lwkt_msg_t msg);
lwkt_msg_t caps_kev_read(caps_port_t port);

extern struct thread main_td;


