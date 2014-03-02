/**
 * Copyright (c) 2013 Larisa Grigore.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sysvipc_utils.h"
#include "sysvipc_sockets.h"

#define MAX_CONN	10

int
init_socket(const char *sockfile)
{
	struct sockaddr_un un_addr;
	int sock;

	/* create server socket */
	if ( (sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
		sysv_print_err("init socket");
		return (-1);
	}

	/* bind it */
	memset(&un_addr, 0, sizeof(un_addr));
	un_addr.sun_len = sizeof(un_addr);
	un_addr.sun_family = AF_UNIX;
	strcpy(un_addr.sun_path, sockfile);

	unlink(un_addr.sun_path);

	if (bind(sock, (struct sockaddr *)&un_addr, sizeof(un_addr)) < 0) {
		close(sock);
		sysv_print_err("bind");
		return (-1);
	}

	if (listen(sock, MAX_CONN) < 0) {
		close(sock);
		sysv_print_err("listen");
		return (-1);
	}

	/* turn on credentials passing */
	return (sock);
}

int
handle_new_connection(int sock)
{
	int fd, flags;

	do {
		fd = accept(sock, NULL, NULL);
	} while (fd < 0 && errno == EINTR);

	if (fd < 0) {
		sysv_print_err("accept");
		return (-1);
	}

	flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

	return (fd);
}

int
connect_to_daemon(const char *sockfile)
{
	int sock, flags;
	struct sockaddr_un serv_addr;

	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		sysv_print_err("socket(%d)\n", sock);
		return (-1);
	}

	flags = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sun_family = AF_UNIX;
	strcpy(serv_addr.sun_path, sockfile);

	if (connect(sock, (struct sockaddr *)&serv_addr,
				sizeof(serv_addr)) < 0) {
		close(sock);
		sysv_print_err("connect(%d)\n", sock);
		return (-1);
	}

	return (sock);
}

int
send_fd(int sock, int fd)
{
	struct msghdr msg;
	struct iovec vec;
#ifndef HAVE_ACCRIGHTS_IN_MSGHDR
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	struct cmsghdr *cmsg;
#endif
	int result = 0;
	ssize_t n;

	memset(&msg, 0, sizeof(msg));

	if (fd < 0)
		result = errno;
	else {
#ifdef HAVE_ACCRIGHTS_IN_MSGHDR
		msg.msg_accrights = (caddr_t)&fd;
		msg.msg_accrightslen = sizeof(fd);
#else
		msg.msg_control = (caddr_t)cmsgbuf.buf;
		msg.msg_controllen = sizeof(cmsgbuf.buf);
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		*(int *)CMSG_DATA(cmsg) = fd;
#endif
	}

	vec.iov_base = (caddr_t)&result;
	vec.iov_len = sizeof(int);
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

	if ((n = sendmsg(sock, &msg, 0)) == -1) {
		sysv_print_err("sendmsg(%d)\n",
				sock, getpid());
		return (-1);
	}
	if (n != sizeof(int)) {
		sysv_print_err("sendmsg: expected sent 1 got %ld\n",
				(long)n);
		return (-1);
	}

	return (0);
}

/**/
int
receive_fd(int sock)
{
	struct msghdr msg;
	struct iovec vec;
#ifndef HAVE_ACCRIGHTS_IN_MSGHDR
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	struct cmsghdr *cmsg;
#endif
	ssize_t n;
	int result;
	int fd;

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = (caddr_t)&result;
	vec.iov_len = sizeof(int);
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

#ifdef HAVE_ACCRIGHTS_IN_MSGHDR
	msg.msg_accrights = (caddr_t)&fd;
	msg.msg_accrightslen = sizeof(fd);
#else
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);
#endif

	if ((n = recvmsg(sock, &msg, 0)) == -1)
		sysv_print_err("recvmsg\n");
	if (n != sizeof(int)) {
		sysv_print_err("recvmsg: expected received 1 got %ld\n",
				(long)n);
	}
	if (result == 0) {
		cmsg = CMSG_FIRSTHDR(&msg);
		if (cmsg == NULL) {
			sysv_print_err("no message header\n");
			return (-1);
		}
		if (cmsg->cmsg_type != SCM_RIGHTS)
			sysv_print_err("expected type %d got %d\n",
					SCM_RIGHTS, cmsg->cmsg_type);

		fd = (*(int *)CMSG_DATA(cmsg));
		return (fd);
	} else {
		errno = result;
		return (-1);
	}
}

static void
close_fds(int *fds, int num_fds) {
	int i;

	for (i=0; i < num_fds; i++)
		close(fds[i]);
}

/* Send with the message, credentials too. */
int
send_msg_with_cred(int sock, char *buffer, size_t size) {
	struct msghdr msg;
	struct iovec vec;
	ssize_t n;
	
	struct {
		struct cmsghdr hdr;
		char cred[CMSG_SPACE(sizeof(struct cmsgcred))];
	} cmsg;

	memset(&cmsg, 0, sizeof(cmsg));
	cmsg.hdr.cmsg_len =  CMSG_LEN(sizeof(struct cmsgcred));
	cmsg.hdr.cmsg_level = SOL_SOCKET;
	cmsg.hdr.cmsg_type = SCM_CREDS;

	memset(&msg, 0, sizeof(struct msghdr));
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = (caddr_t)&cmsg;
	msg.msg_controllen = CMSG_SPACE(sizeof(struct cmsgcred));

	vec.iov_base = buffer;
	vec.iov_len = size;

	if ((n = sendmsg(sock, &msg, 0)) == -1) {
		sysv_print_err("sendmsg on fd %d\n", sock);
		return (-1);
	}

	return (0);
}

/* Receive a message and the credentials of the sender. */
int
receive_msg_with_cred(int sock, char *buffer, size_t size,
		struct cmsgcred *cred) {
	struct msghdr msg = { .msg_name = NULL };
	struct iovec vec;
	ssize_t n;
	int result;
	struct cmsghdr *cmp;
	struct {
		struct cmsghdr hdr;
		char cred[CMSG_SPACE(sizeof(struct cmsgcred))];
	} cmsg;

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = buffer;
	vec.iov_len = size;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

	msg.msg_control = &cmsg;
	msg.msg_controllen = sizeof(cmsg);

	do {
		n = recvmsg(sock, &msg, 0);
	} while (n < 0 && errno == EINTR);

	if (n < 0) {
		sysv_print_err("recvmsg on fd %d\n", sock);
		return (-1);
	}

	if (n == 0) {
		return (-1);
	}

	result = -1;
	cmp = CMSG_FIRSTHDR(&msg);

	while(cmp != NULL) {
		if (cmp->cmsg_level == SOL_SOCKET
				&& cmp->cmsg_type  == SCM_CREDS) {
			if (cred)
				memcpy(cred, CMSG_DATA(cmp), sizeof(*cred));
			result = n;
		} else if (cmp->cmsg_level == SOL_SOCKET
				&& cmp->cmsg_type  == SCM_RIGHTS) {
			close_fds((int *) CMSG_DATA(cmp),
					(cmp->cmsg_len - CMSG_LEN(0))
					/ sizeof(int));
		}
		cmp = CMSG_NXTHDR(&msg, cmp);
	}

	return (result);
}
