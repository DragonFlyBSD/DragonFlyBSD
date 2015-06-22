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
 */

#ifndef _SYS_KERN_SYSCALL_H_
#define _SYS_KERN_SYSCALL_H_

#ifndef _KERNEL
#error "This file should not be included by userland programs."
#endif

#include <sys/uio.h>

#define DUP_FIXED	0x1	/* Copy to specific fd even if in use */
#define DUP_VARIABLE	0x2	/* Copy fd to an unused fd */
#define DUP_CLOEXEC	0x4	/* Set fd close on exec flag */
#define DUP_FCNTL	0x8	/* Set for F_DUPFD and F_DUPFD_CLOEXEC */
union fcntl_dat;
struct image_args;
struct plimit;
struct mbuf;
struct msghdr;
struct namecache;
struct nchandle;
struct nlookupdata;
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
struct vmspace;
struct vnode;
struct file;
struct ucred;
struct uuid;
struct statvfs;

/*
 * Prototypes for syscalls in kern/kern_descrip.c
 */
int kern_dup(int flags, int old, int new, int *res);
int kern_fcntl(int fd, int cmd, union fcntl_dat *dat, struct ucred *cred);
int kern_fstat(int fd, struct stat *st);

/*
 * Prototypes for syscalls in kern/kern_exec.c
 */
int kern_execve(struct nlookupdata *nd, struct image_args *args);

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
int kern_kill(int sig, pid_t pid, lwpid_t tid);

/*
 * Prototypes for syscalls in kern/sys_generic.c
 */
int kern_preadv(int fd, struct uio *auio, int flags, size_t *res);
int kern_pwritev(int fd, struct uio *auio, int flags, size_t *res);

/*
 * Prototypes for syscalls in kern/kern_resource.c
 */
int kern_setrlimit(u_int which, struct rlimit *limp);
int kern_getrlimit(u_int which, struct rlimit *limp);

/*
 * Prototypes for syscalls in kern/uipc_syscalls.c
 */
int kern_accept(int s, int fflags, struct sockaddr **name, int *namelen, int *res);
int kern_bind(int s, struct sockaddr *sa);
int kern_connect(int s, int fflags, struct sockaddr *sa);
int kern_listen(int s, int backlog);
int kern_getpeername(int s, struct sockaddr **name, int *namelen);
int kern_getsockopt(int s, struct sockopt *sopt);
int kern_getsockname(int s, struct sockaddr **name, int *namelen);
int kern_recvmsg(int s, struct sockaddr **sa, struct uio *auio,
	struct mbuf **control, int *flags, size_t *res);
int kern_shutdown(int s, int how);
int kern_sendfile(struct vnode *vp, int s, off_t offset, size_t nbytes,
	struct mbuf *mheader, off_t *sbytes, int flags);
int kern_sendmsg(int s, struct sockaddr *sa, struct uio *auio,
	struct mbuf *control, int flags, size_t *res);
int kern_setsockopt(int s, struct sockopt *sopt);
int kern_socket(int domain, int type, int protocol, int *res);
int kern_socketpair(int domain, int type, int protocol, int *sockv);

/*
 * Prototypes for syscalls in kern/sys_pipe.c
 */
int kern_pipe(long *fds, int flags);

/*
 * Prototypes for syscalls in kern/vfs_syscalls.c
 */
int kern_access(struct nlookupdata *nd, int amode, int flags);
int kern_chdir(struct nlookupdata *nd);
int kern_chmod(struct nlookupdata *nd, int mode);
int kern_chown(struct nlookupdata *nd, int uid, int gid);
int kern_chroot(struct nchandle *nch);
int kern_fstatfs(int fd, struct statfs *buf);
int kern_fstatvfs(int fd, struct statvfs *buf);
int kern_ftruncate(int fd, off_t length);
int kern_futimens(int fd, struct timespec *ts);
int kern_futimes(int fd, struct timeval *tptr);
int kern_getdirentries(int fd, char *buf, u_int count, long *basep, int *res,
		       enum uio_seg);
int kern_link(struct nlookupdata *nd, struct nlookupdata *linknd);
int kern_lseek(int fd, off_t offset, int whence, off_t *res);
int kern_mountctl(const char *path, int op, struct file *fp,
		const void *ctl, int ctllen,
                void *buf, int buflen, int *res);
int kern_mkdir(struct nlookupdata *nd, int mode);
int kern_mkfifo(struct nlookupdata *nd, int mode);
int kern_mknod(struct nlookupdata *nd, int mode, int rmajor, int rminor);
int kern_open(struct nlookupdata *nd, int flags, int mode, int *res);
int kern_close(int fd);
int kern_closefrom(int fd);
int kern_readlink(struct nlookupdata *nd, char *buf, int count, int *res);
int kern_rename(struct nlookupdata *fromnd, struct nlookupdata *tond);
int kern_rmdir(struct nlookupdata *nd);
int kern_stat(struct nlookupdata *nd, struct stat *st);
int kern_statfs(struct nlookupdata *nd, struct statfs *buf);
int kern_statvfs(struct nlookupdata *nd, struct statvfs *buf);
int kern_symlink(struct nlookupdata *nd, char *path, int mode);
int kern_truncate(struct nlookupdata *nd, off_t length);
int kern_unlink(struct nlookupdata *nd);
int kern_utimensat(struct nlookupdata *nd, const struct timespec *ts, int flag);
int kern_utimes(struct nlookupdata *nd, struct timeval *tptr);
struct uuid *kern_uuidgen(struct uuid *store, size_t count);

/*
 * Prototypes for syscalls in kern/kern_time.c
 */
int kern_clock_gettime(clockid_t, struct timespec *);
int kern_clock_settime(clockid_t, struct timespec *);
int kern_clock_getres(clockid_t, struct timespec *);

/*
 * Prototypes for syscalls in kern/vfs_cache.c
 */
char *kern_getcwd(char *, size_t, int *);

/*
 * Prototypes for syscalls in vm/vm_mmap.c
 */
int kern_mmap(struct vmspace *, caddr_t addr, size_t len,
	      int prot, int flags, int fd, off_t pos, void **res);

#endif /* !_SYS_KERN_SYSCALL_H_ */
