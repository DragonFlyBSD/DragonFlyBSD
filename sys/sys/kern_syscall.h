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
 * $DragonFly: src/sys/sys/kern_syscall.h,v 1.17 2004/06/05 09:58:15 eirikn Exp $
 */

#ifndef _SYS_KERN_SYSCALL_H_
#define _SYS_KERN_SYSCALL_H_

enum dup_type {DUP_FIXED, DUP_VARIABLE};
union fcntl_dat;
struct image_args;
struct mbuf;
struct msghdr;
struct nameidata;
struct rlimit;
struct rusage;
struct sigaction;
struct sigaltstack;
struct __sigset;
struct sf_hdtr;
struct sockaddr;
struct socket;
struct sockopt;
struct stat;
struct statfs;
struct timeval;
struct uio;
struct vnode;

/*
 * Prototypes for syscalls in kern/kern_descrip.c
 */
int kern_dup(enum dup_type type, int old, int new, int *res);
int kern_fcntl(int fd, int cmd, union fcntl_dat *dat);
int kern_fstat(int fd, struct stat *st);

/*
 * Prototypes for syscalls in kern/kern_exec.c
 */
int kern_execve(struct nameidata *nd, struct image_args *args);

/*
 * Prototypes for syscalls in kern/kern_exit.c
 */
int kern_wait(pid_t pid, int *status, int options, struct rusage *rusage,
	int *res);

/*
 * Prototypes for syscalls in kern/kern_sig.c
 */
int kern_sigaction(int sig, struct sigaction *act, struct sigaction *oact);
int kern_sigprocmask(int how, struct __sigset *set, struct __sigset *oset);
int kern_sigpending(struct __sigset *set);
int kern_sigsuspend(struct __sigset *mask);
int kern_sigaltstack(struct sigaltstack *ss, struct sigaltstack *oss);
int kern_kill(int sig, int id);

/*
 * Prototypes for syscalls in kern/sys_generic.c
 */
int kern_readv(int fd, struct uio *auio, int flags, int *res);
int kern_writev(int fd, struct uio *auio, int flags, int *res);

/*
 * Prototypes for syscalls in kern/kern_resource.c
 */
int kern_setrlimit(u_int which, struct rlimit *limp);
int kern_getrlimit(u_int which, struct rlimit *limp);

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
	struct mbuf *mheader, off_t *sbytes, int flags);
int kern_sendmsg(int s, struct sockaddr *sa, struct uio *auio,
	struct mbuf *control, int flags, int *res);
int kern_setsockopt(int s, struct sockopt *sopt);
int kern_socket(int domain, int type, int protocol, int *res);
int kern_socketpair(int domain, int type, int protocol, int *sockv);

/*
 * Prototypes for syscalls in kern/vfs_syscalls.c
 */
int kern_access(struct nameidata *nd, int aflags);
int kern_chdir(struct nameidata *nd);
int kern_chmod(struct nameidata *nd, int mode);
int kern_chown(struct nameidata *nd, int uid, int gid);
int kern_chroot(struct vnode *vp);
int kern_fstatfs(int fd, struct statfs *buf);
int kern_ftruncate(int fd, off_t length);
int kern_futimes(int fd, struct timeval *tptr);
int kern_getdirentries(int fd, char *buf, u_int count, long *basep, int *res);
int kern_link(struct nameidata *nd, struct nameidata *linknd);
int kern_lseek(int fd, off_t offset, int whence, off_t *res);
int kern_mkdir(struct nameidata *nd, int mode);
int kern_mkfifo(struct nameidata *nd, int mode);
int kern_mknod(struct nameidata *nd, int mode, int dev);
int kern_open(struct nameidata *nd, int flags, int mode, int *res);
int kern_readlink(struct nameidata *nd, char *buf, int count, int *res);
int kern_rename(struct nameidata *fromnd, struct nameidata *tond);
int kern_rmdir(struct nameidata *nd);
int kern_stat(struct nameidata *nd, struct stat *st);
int kern_statfs(struct nameidata *nd, struct statfs *buf);
int kern_symlink(char *path, struct nameidata *nd);
int kern_truncate(struct nameidata *nd, off_t length);
int kern_unlink(struct nameidata *nd);
int kern_utimes(struct nameidata *nd, struct timeval *tptr);

/*
 * Prototypes for syscalls in kern/vfs_cache.c
 */
int kern_getcwd(char *, size_t);

/*
 * Prototypes for syscalls in vm/vm_mmap.c
 */
int kern_mmap(caddr_t addr, size_t len, int prot, int flags, int fd,
	off_t pos, void **res);

#endif /* !_SYS_KERN_SYSCALL_H_ */
