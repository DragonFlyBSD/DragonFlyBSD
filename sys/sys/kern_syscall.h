/*
 * KERN_SYSCALL.H	- Split syscall prototypes
 *
 * Copyright (c) 2003 David P. Reese, Jr. <daver@gomerbud.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/sys/kern_syscall.h,v 1.7 2003/10/17 05:25:45 daver Exp $
 */

#ifndef _SYS_KERN_SYSCALL_H_
#define _SYS_KERN_SYSCALL_H_

enum dup_type {DUP_FIXED, DUP_VARIABLE};
union fcntl_dat;
struct mbuf;
struct msghdr;
struct sf_hdtr;
struct sockaddr;
struct socket;
struct sockopt;
struct uio;
struct vnode;

/*
 * Prototypes for syscalls in kern/kern_descrip.c
 */
int kern_dup(enum dup_type type, int old, int new, int *res);
int kern_fcntl(int fd, int cmd, union fcntl_dat *dat);

/*
 * Prototypes for syscalls in kern/sys_generic.c
 */
int kern_readv(int fd, struct uio *auio, int flags, int *res);
int kern_writev(int fd, struct uio *auio, int flags, int *res);

/*
 * Prototypes for syscalls in kern/uipc_syscalls.c
 */
int kern_accept(int s, struct sockaddr **name, int *namelen, int *res);
int kern_bind(int s, struct sockaddr *sa);
int kern_connect(int s, struct sockaddr *sa);
int kern_listen(int s, int backlog);
int kern_getpeername(int s, struct sockaddr **name, int *namelen);
int kern_getsockopt(int s, struct sockopt *sopt);
int kern_getsockname(int s, struct sockaddr **name, int *namelen);
int kern_recvmsg(int s, struct sockaddr **sa, struct uio *auio,
	struct mbuf **control, int *flags, int *res);
int kern_shutdown(int s, int how);
int kern_sendfile(struct vnode *vp, int s, off_t offset, size_t nbytes,
	off_t *sbytes, int flags);
int kern_sendmsg(int s, struct sockaddr *sa, struct uio *auio,
	struct mbuf *control, int flags, int *res);
int kern_setsockopt(int s, struct sockopt *sopt);
int kern_socket(int domain, int type, int protocol, int *res);
int kern_socketpair(int domain, int type, int protocol, int *sockv);

#endif /* !_SYS_KERN_SYSCALL_H_ */
