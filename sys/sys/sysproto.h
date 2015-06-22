/*
 * System call prototypes.
 *
 * DO NOT EDIT-- To regenerate this file, edit syscalls.master followed
 *               by running make sysent in the same directory.
 */

#ifndef _SYS_SYSPROTO_H_
#define	_SYS_SYSPROTO_H_

#include <sys/select.h>

#include <sys/signal.h>

#include <sys/acl.h>

#include <sys/msgport.h>

#include <sys/sysmsg.h>

#include <sys/syslink.h>

#include <sys/procctl.h>

#define	PAD_(t)	(sizeof(register_t) <= sizeof(t) ? \
		0 : sizeof(register_t) - sizeof(t))

#ifdef COMPAT_43
#endif
struct	nosys_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	exit_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	rval;	char rval_[PAD_(int)];
};
struct	fork_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	read_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	void *	buf;	char buf_[PAD_(void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
};
struct	write_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	const void *	buf;	char buf_[PAD_(const void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
};
struct	open_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
	int	mode;	char mode_[PAD_(int)];
};
struct	close_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
};
struct	wait_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	pid;	char pid_[PAD_(int)];
	int *	status;	char status_[PAD_(int *)];
	int	options;	char options_[PAD_(int)];
	struct rusage *	rusage;	char rusage_[PAD_(struct rusage *)];
};
struct	link_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	char *	link;	char link_[PAD_(char *)];
};
struct	unlink_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
};
struct	chdir_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
};
struct	fchdir_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
};
struct	mknod_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
	int	dev;	char dev_[PAD_(int)];
};
struct	chmod_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
};
struct	chown_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	uid;	char uid_[PAD_(int)];
	int	gid;	char gid_[PAD_(int)];
};
struct	obreak_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	nsize;	char nsize_[PAD_(char *)];
};
struct	getfsstat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct statfs *	buf;	char buf_[PAD_(struct statfs *)];
	long	bufsize;	char bufsize_[PAD_(long)];
	int	flags;	char flags_[PAD_(int)];
};
struct	getpid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	mount_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	type;	char type_[PAD_(char *)];
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
	caddr_t	data;	char data_[PAD_(caddr_t)];
};
struct	unmount_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	setuid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	uid_t	uid;	char uid_[PAD_(uid_t)];
};
struct	getuid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	geteuid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	ptrace_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	req;	char req_[PAD_(int)];
	pid_t	pid;	char pid_[PAD_(pid_t)];
	caddr_t	addr;	char addr_[PAD_(caddr_t)];
	int	data;	char data_[PAD_(int)];
};
struct	recvmsg_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	struct msghdr *	msg;	char msg_[PAD_(struct msghdr *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	sendmsg_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	caddr_t	msg;	char msg_[PAD_(caddr_t)];
	int	flags;	char flags_[PAD_(int)];
};
struct	recvfrom_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	caddr_t	buf;	char buf_[PAD_(caddr_t)];
	size_t	len;	char len_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	caddr_t	from;	char from_[PAD_(caddr_t)];
	int *	fromlenaddr;	char fromlenaddr_[PAD_(int *)];
};
struct	accept_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int *	anamelen;	char anamelen_[PAD_(int *)];
};
struct	getpeername_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fdes;	char fdes_[PAD_(int)];
	caddr_t	asa;	char asa_[PAD_(caddr_t)];
	int *	alen;	char alen_[PAD_(int *)];
};
struct	getsockname_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fdes;	char fdes_[PAD_(int)];
	caddr_t	asa;	char asa_[PAD_(caddr_t)];
	int *	alen;	char alen_[PAD_(int *)];
};
struct	access_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	chflags_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	fchflags_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
};
struct	sync_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	kill_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	pid;	char pid_[PAD_(int)];
	int	signum;	char signum_[PAD_(int)];
};
struct	getppid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	dup_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_int	fd;	char fd_[PAD_(u_int)];
};
struct	pipe_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	getegid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	profil_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	caddr_t	samples;	char samples_[PAD_(caddr_t)];
	size_t	size;	char size_[PAD_(size_t)];
	size_t	offset;	char offset_[PAD_(size_t)];
	u_int	scale;	char scale_[PAD_(u_int)];
};
struct	ktrace_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	fname;	char fname_[PAD_(const char *)];
	int	ops;	char ops_[PAD_(int)];
	int	facs;	char facs_[PAD_(int)];
	int	pid;	char pid_[PAD_(int)];
};
struct	getgid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	getlogin_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	namebuf;	char namebuf_[PAD_(char *)];
	u_int	namelen;	char namelen_[PAD_(u_int)];
};
struct	setlogin_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	namebuf;	char namebuf_[PAD_(char *)];
};
struct	acct_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
};
struct	sigaltstack_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	stack_t *	ss;	char ss_[PAD_(stack_t *)];
	stack_t *	oss;	char oss_[PAD_(stack_t *)];
};
struct	ioctl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	u_long	com;	char com_[PAD_(u_long)];
	caddr_t	data;	char data_[PAD_(caddr_t)];
};
struct	reboot_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	opt;	char opt_[PAD_(int)];
};
struct	revoke_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
};
struct	symlink_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	char *	link;	char link_[PAD_(char *)];
};
struct	readlink_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	char *	buf;	char buf_[PAD_(char *)];
	int	count;	char count_[PAD_(int)];
};
struct	execve_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	fname;	char fname_[PAD_(char *)];
	char **	argv;	char argv_[PAD_(char **)];
	char **	envv;	char envv_[PAD_(char **)];
};
struct	umask_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	newmask;	char newmask_[PAD_(int)];
};
struct	chroot_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
};
struct	getpagesize_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	msync_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
};
struct	vfork_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	sbrk_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	incr;	char incr_[PAD_(int)];
};
struct	sstk_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	incr;	char incr_[PAD_(int)];
};
struct	munmap_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	mprotect_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	prot;	char prot_[PAD_(int)];
};
struct	madvise_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	behav;	char behav_[PAD_(int)];
};
struct	mincore_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const void *	addr;	char addr_[PAD_(const void *)];
	size_t	len;	char len_[PAD_(size_t)];
	char *	vec;	char vec_[PAD_(char *)];
};
struct	getgroups_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_int	gidsetsize;	char gidsetsize_[PAD_(u_int)];
	gid_t *	gidset;	char gidset_[PAD_(gid_t *)];
};
struct	setgroups_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_int	gidsetsize;	char gidsetsize_[PAD_(u_int)];
	gid_t *	gidset;	char gidset_[PAD_(gid_t *)];
};
struct	getpgrp_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	setpgid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	pid;	char pid_[PAD_(int)];
	int	pgid;	char pgid_[PAD_(int)];
};
struct	setitimer_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_int	which;	char which_[PAD_(u_int)];
	struct itimerval *	itv;	char itv_[PAD_(struct itimerval *)];
	struct itimerval *	oitv;	char oitv_[PAD_(struct itimerval *)];
};
struct	owait_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	swapon_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	name;	char name_[PAD_(char *)];
};
struct	getitimer_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_int	which;	char which_[PAD_(u_int)];
	struct itimerval *	itv;	char itv_[PAD_(struct itimerval *)];
};
struct	getdtablesize_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	dup2_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_int	from;	char from_[PAD_(u_int)];
	u_int	to;	char to_[PAD_(u_int)];
};
struct	fcntl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	int	cmd;	char cmd_[PAD_(int)];
	long	arg;	char arg_[PAD_(long)];
};
struct	select_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	nd;	char nd_[PAD_(int)];
	fd_set *	in;	char in_[PAD_(fd_set *)];
	fd_set *	ou;	char ou_[PAD_(fd_set *)];
	fd_set *	ex;	char ex_[PAD_(fd_set *)];
	struct timeval *	tv;	char tv_[PAD_(struct timeval *)];
};
struct	fsync_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
};
struct	setpriority_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	which;	char which_[PAD_(int)];
	int	who;	char who_[PAD_(int)];
	int	prio;	char prio_[PAD_(int)];
};
struct	socket_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	domain;	char domain_[PAD_(int)];
	int	type;	char type_[PAD_(int)];
	int	protocol;	char protocol_[PAD_(int)];
};
struct	connect_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int	namelen;	char namelen_[PAD_(int)];
};
struct	getpriority_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	which;	char which_[PAD_(int)];
	int	who;	char who_[PAD_(int)];
};
struct	bind_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int	namelen;	char namelen_[PAD_(int)];
};
struct	setsockopt_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	int	level;	char level_[PAD_(int)];
	int	name;	char name_[PAD_(int)];
	caddr_t	val;	char val_[PAD_(caddr_t)];
	int	valsize;	char valsize_[PAD_(int)];
};
struct	listen_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	int	backlog;	char backlog_[PAD_(int)];
};
struct	gettimeofday_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct timeval *	tp;	char tp_[PAD_(struct timeval *)];
	struct timezone *	tzp;	char tzp_[PAD_(struct timezone *)];
};
struct	getrusage_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	who;	char who_[PAD_(int)];
	struct rusage *	rusage;	char rusage_[PAD_(struct rusage *)];
};
struct	getsockopt_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	int	level;	char level_[PAD_(int)];
	int	name;	char name_[PAD_(int)];
	caddr_t	val;	char val_[PAD_(caddr_t)];
	int *	avalsize;	char avalsize_[PAD_(int *)];
};
struct	readv_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	struct iovec *	iovp;	char iovp_[PAD_(struct iovec *)];
	u_int	iovcnt;	char iovcnt_[PAD_(u_int)];
};
struct	writev_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	struct iovec *	iovp;	char iovp_[PAD_(struct iovec *)];
	u_int	iovcnt;	char iovcnt_[PAD_(u_int)];
};
struct	settimeofday_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct timeval *	tv;	char tv_[PAD_(struct timeval *)];
	struct timezone *	tzp;	char tzp_[PAD_(struct timezone *)];
};
struct	fchown_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	int	uid;	char uid_[PAD_(int)];
	int	gid;	char gid_[PAD_(int)];
};
struct	fchmod_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	int	mode;	char mode_[PAD_(int)];
};
struct	setreuid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	ruid;	char ruid_[PAD_(int)];
	int	euid;	char euid_[PAD_(int)];
};
struct	setregid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	rgid;	char rgid_[PAD_(int)];
	int	egid;	char egid_[PAD_(int)];
};
struct	rename_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	from;	char from_[PAD_(char *)];
	char *	to;	char to_[PAD_(char *)];
};
struct	flock_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	int	how;	char how_[PAD_(int)];
};
struct	mkfifo_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
};
struct	sendto_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	caddr_t	buf;	char buf_[PAD_(caddr_t)];
	size_t	len;	char len_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	caddr_t	to;	char to_[PAD_(caddr_t)];
	int	tolen;	char tolen_[PAD_(int)];
};
struct	shutdown_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	int	how;	char how_[PAD_(int)];
};
struct	socketpair_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	domain;	char domain_[PAD_(int)];
	int	type;	char type_[PAD_(int)];
	int	protocol;	char protocol_[PAD_(int)];
	int *	rsv;	char rsv_[PAD_(int *)];
};
struct	mkdir_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
};
struct	rmdir_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
};
struct	utimes_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	struct timeval *	tptr;	char tptr_[PAD_(struct timeval *)];
};
struct	adjtime_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct timeval *	delta;	char delta_[PAD_(struct timeval *)];
	struct timeval *	olddelta;	char olddelta_[PAD_(struct timeval *)];
};
struct	ogethostid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	setsid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	quotactl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	cmd;	char cmd_[PAD_(int)];
	int	uid;	char uid_[PAD_(int)];
	caddr_t	arg;	char arg_[PAD_(caddr_t)];
};
struct	oquota_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	nfssvc_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	flag;	char flag_[PAD_(int)];
	caddr_t	argp;	char argp_[PAD_(caddr_t)];
};
struct	statfs_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	struct statfs *	buf;	char buf_[PAD_(struct statfs *)];
};
struct	fstatfs_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	struct statfs *	buf;	char buf_[PAD_(struct statfs *)];
};
struct	getfh_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	fname;	char fname_[PAD_(char *)];
	struct fhandle *	fhp;	char fhp_[PAD_(struct fhandle *)];
};
struct	getdomainname_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	domainname;	char domainname_[PAD_(char *)];
	int	len;	char len_[PAD_(int)];
};
struct	setdomainname_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	domainname;	char domainname_[PAD_(char *)];
	int	len;	char len_[PAD_(int)];
};
struct	uname_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct utsname *	name;	char name_[PAD_(struct utsname *)];
};
struct	sysarch_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	op;	char op_[PAD_(int)];
	char *	parms;	char parms_[PAD_(char *)];
};
struct	rtprio_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	function;	char function_[PAD_(int)];
	pid_t	pid;	char pid_[PAD_(pid_t)];
	struct rtprio *	rtp;	char rtp_[PAD_(struct rtprio *)];
};
struct	extpread_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	void *	buf;	char buf_[PAD_(void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	extpwrite_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	const void *	buf;	char buf_[PAD_(const void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	ntp_adjtime_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct timex *	tp;	char tp_[PAD_(struct timex *)];
};
struct	setgid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	gid_t	gid;	char gid_[PAD_(gid_t)];
};
struct	setegid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	gid_t	egid;	char egid_[PAD_(gid_t)];
};
struct	seteuid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	uid_t	euid;	char euid_[PAD_(uid_t)];
};
struct	pathconf_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	name;	char name_[PAD_(int)];
};
struct	fpathconf_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	int	name;	char name_[PAD_(int)];
};
struct	__getrlimit_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_int	which;	char which_[PAD_(u_int)];
	struct rlimit *	rlp;	char rlp_[PAD_(struct rlimit *)];
};
struct	__setrlimit_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_int	which;	char which_[PAD_(u_int)];
	struct rlimit *	rlp;	char rlp_[PAD_(struct rlimit *)];
};
struct	mmap_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	caddr_t	addr;	char addr_[PAD_(caddr_t)];
	size_t	len;	char len_[PAD_(size_t)];
	int	prot;	char prot_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	int	fd;	char fd_[PAD_(int)];
	int	pad;	char pad_[PAD_(int)];
	off_t	pos;	char pos_[PAD_(off_t)];
};
struct	lseek_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	int	pad;	char pad_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
	int	whence;	char whence_[PAD_(int)];
};
struct	truncate_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	pad;	char pad_[PAD_(int)];
	off_t	length;	char length_[PAD_(off_t)];
};
struct	ftruncate_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	int	pad;	char pad_[PAD_(int)];
	off_t	length;	char length_[PAD_(off_t)];
};
struct	sysctl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int *	name;	char name_[PAD_(int *)];
	u_int	namelen;	char namelen_[PAD_(u_int)];
	void *	old;	char old_[PAD_(void *)];
	size_t *	oldlenp;	char oldlenp_[PAD_(size_t *)];
	void *	new;	char new_[PAD_(void *)];
	size_t	newlen;	char newlen_[PAD_(size_t)];
};
struct	mlock_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const void *	addr;	char addr_[PAD_(const void *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	munlock_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const void *	addr;	char addr_[PAD_(const void *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	undelete_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
};
struct	futimes_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	struct timeval *	tptr;	char tptr_[PAD_(struct timeval *)];
};
struct	getpgid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	pid_t	pid;	char pid_[PAD_(pid_t)];
};
struct	poll_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct pollfd *	fds;	char fds_[PAD_(struct pollfd *)];
	u_int	nfds;	char nfds_[PAD_(u_int)];
	int	timeout;	char timeout_[PAD_(int)];
};
struct	__semctl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	semid;	char semid_[PAD_(int)];
	int	semnum;	char semnum_[PAD_(int)];
	int	cmd;	char cmd_[PAD_(int)];
	union semun *	arg;	char arg_[PAD_(union semun *)];
};
struct	semget_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	key_t	key;	char key_[PAD_(key_t)];
	int	nsems;	char nsems_[PAD_(int)];
	int	semflg;	char semflg_[PAD_(int)];
};
struct	semop_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	semid;	char semid_[PAD_(int)];
	struct sembuf *	sops;	char sops_[PAD_(struct sembuf *)];
	u_int	nsops;	char nsops_[PAD_(u_int)];
};
struct	msgctl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	msqid;	char msqid_[PAD_(int)];
	int	cmd;	char cmd_[PAD_(int)];
	struct msqid_ds *	buf;	char buf_[PAD_(struct msqid_ds *)];
};
struct	msgget_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	key_t	key;	char key_[PAD_(key_t)];
	int	msgflg;	char msgflg_[PAD_(int)];
};
struct	msgsnd_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	msqid;	char msqid_[PAD_(int)];
	const void *	msgp;	char msgp_[PAD_(const void *)];
	size_t	msgsz;	char msgsz_[PAD_(size_t)];
	int	msgflg;	char msgflg_[PAD_(int)];
};
struct	msgrcv_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	msqid;	char msqid_[PAD_(int)];
	void *	msgp;	char msgp_[PAD_(void *)];
	size_t	msgsz;	char msgsz_[PAD_(size_t)];
	long	msgtyp;	char msgtyp_[PAD_(long)];
	int	msgflg;	char msgflg_[PAD_(int)];
};
struct	shmat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	shmid;	char shmid_[PAD_(int)];
	const void *	shmaddr;	char shmaddr_[PAD_(const void *)];
	int	shmflg;	char shmflg_[PAD_(int)];
};
struct	shmctl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	shmid;	char shmid_[PAD_(int)];
	int	cmd;	char cmd_[PAD_(int)];
	struct shmid_ds *	buf;	char buf_[PAD_(struct shmid_ds *)];
};
struct	shmdt_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const void *	shmaddr;	char shmaddr_[PAD_(const void *)];
};
struct	shmget_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	key_t	key;	char key_[PAD_(key_t)];
	size_t	size;	char size_[PAD_(size_t)];
	int	shmflg;	char shmflg_[PAD_(int)];
};
struct	clock_gettime_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	clockid_t	clock_id;	char clock_id_[PAD_(clockid_t)];
	struct timespec *	tp;	char tp_[PAD_(struct timespec *)];
};
struct	clock_settime_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	clockid_t	clock_id;	char clock_id_[PAD_(clockid_t)];
	const struct timespec *	tp;	char tp_[PAD_(const struct timespec *)];
};
struct	clock_getres_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	clockid_t	clock_id;	char clock_id_[PAD_(clockid_t)];
	struct timespec *	tp;	char tp_[PAD_(struct timespec *)];
};
struct	nanosleep_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const struct timespec *	rqtp;	char rqtp_[PAD_(const struct timespec *)];
	struct timespec *	rmtp;	char rmtp_[PAD_(struct timespec *)];
};
struct	minherit_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	inherit;	char inherit_[PAD_(int)];
};
struct	rfork_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	flags;	char flags_[PAD_(int)];
};
struct	openbsd_poll_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct pollfd *	fds;	char fds_[PAD_(struct pollfd *)];
	u_int	nfds;	char nfds_[PAD_(u_int)];
	int	timeout;	char timeout_[PAD_(int)];
};
struct	issetugid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	lchown_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	uid;	char uid_[PAD_(int)];
	int	gid;	char gid_[PAD_(int)];
};
struct	lchmod_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	mode_t	mode;	char mode_[PAD_(mode_t)];
};
struct	lutimes_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	struct timeval *	tptr;	char tptr_[PAD_(struct timeval *)];
};
struct	extpreadv_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	struct iovec *	iovp;	char iovp_[PAD_(struct iovec *)];
	u_int	iovcnt;	char iovcnt_[PAD_(u_int)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	extpwritev_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	struct iovec *	iovp;	char iovp_[PAD_(struct iovec *)];
	u_int	iovcnt;	char iovcnt_[PAD_(u_int)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	fhstatfs_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const struct fhandle *	u_fhp;	char u_fhp_[PAD_(const struct fhandle *)];
	struct statfs *	buf;	char buf_[PAD_(struct statfs *)];
};
struct	fhopen_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const struct fhandle *	u_fhp;	char u_fhp_[PAD_(const struct fhandle *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	modnext_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	modid;	char modid_[PAD_(int)];
};
struct	modstat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	modid;	char modid_[PAD_(int)];
	struct module_stat *	stat;	char stat_[PAD_(struct module_stat *)];
};
struct	modfnext_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	modid;	char modid_[PAD_(int)];
};
struct	modfind_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	name;	char name_[PAD_(const char *)];
};
struct	kldload_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	file;	char file_[PAD_(const char *)];
};
struct	kldunload_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fileid;	char fileid_[PAD_(int)];
};
struct	kldfind_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	file;	char file_[PAD_(const char *)];
};
struct	kldnext_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fileid;	char fileid_[PAD_(int)];
};
struct	kldstat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fileid;	char fileid_[PAD_(int)];
	struct kld_file_stat *	stat;	char stat_[PAD_(struct kld_file_stat *)];
};
struct	kldfirstmod_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fileid;	char fileid_[PAD_(int)];
};
struct	getsid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	pid_t	pid;	char pid_[PAD_(pid_t)];
};
struct	setresuid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	uid_t	ruid;	char ruid_[PAD_(uid_t)];
	uid_t	euid;	char euid_[PAD_(uid_t)];
	uid_t	suid;	char suid_[PAD_(uid_t)];
};
struct	setresgid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	gid_t	rgid;	char rgid_[PAD_(gid_t)];
	gid_t	egid;	char egid_[PAD_(gid_t)];
	gid_t	sgid;	char sgid_[PAD_(gid_t)];
};
struct	aio_return_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct aiocb *	aiocbp;	char aiocbp_[PAD_(struct aiocb *)];
};
struct	aio_suspend_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct aiocb *const *	aiocbp;	char aiocbp_[PAD_(struct aiocb *const *)];
	int	nent;	char nent_[PAD_(int)];
	const struct timespec *	timeout;	char timeout_[PAD_(const struct timespec *)];
};
struct	aio_cancel_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	struct aiocb *	aiocbp;	char aiocbp_[PAD_(struct aiocb *)];
};
struct	aio_error_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct aiocb *	aiocbp;	char aiocbp_[PAD_(struct aiocb *)];
};
struct	aio_read_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct aiocb *	aiocbp;	char aiocbp_[PAD_(struct aiocb *)];
};
struct	aio_write_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct aiocb *	aiocbp;	char aiocbp_[PAD_(struct aiocb *)];
};
struct	lio_listio_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	mode;	char mode_[PAD_(int)];
	struct aiocb *const *	acb_list;	char acb_list_[PAD_(struct aiocb *const *)];
	int	nent;	char nent_[PAD_(int)];
	struct sigevent *	sig;	char sig_[PAD_(struct sigevent *)];
};
struct	yield_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	mlockall_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	how;	char how_[PAD_(int)];
};
struct	munlockall_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	__getcwd_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_char *	buf;	char buf_[PAD_(u_char *)];
	u_int	buflen;	char buflen_[PAD_(u_int)];
};
struct	sched_setparam_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	pid_t	pid;	char pid_[PAD_(pid_t)];
	const struct sched_param *	param;	char param_[PAD_(const struct sched_param *)];
};
struct	sched_getparam_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	pid_t	pid;	char pid_[PAD_(pid_t)];
	struct sched_param *	param;	char param_[PAD_(struct sched_param *)];
};
struct	sched_setscheduler_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	pid_t	pid;	char pid_[PAD_(pid_t)];
	int	policy;	char policy_[PAD_(int)];
	const struct sched_param *	param;	char param_[PAD_(const struct sched_param *)];
};
struct	sched_getscheduler_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	pid_t	pid;	char pid_[PAD_(pid_t)];
};
struct	sched_yield_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	sched_get_priority_max_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	policy;	char policy_[PAD_(int)];
};
struct	sched_get_priority_min_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	policy;	char policy_[PAD_(int)];
};
struct	sched_rr_get_interval_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	pid_t	pid;	char pid_[PAD_(pid_t)];
	struct timespec *	interval;	char interval_[PAD_(struct timespec *)];
};
struct	utrace_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const void *	addr;	char addr_[PAD_(const void *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	kldsym_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fileid;	char fileid_[PAD_(int)];
	int	cmd;	char cmd_[PAD_(int)];
	void *	data;	char data_[PAD_(void *)];
};
struct	jail_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct jail *	jail;	char jail_[PAD_(struct jail *)];
};
struct	sigprocmask_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	how;	char how_[PAD_(int)];
	const sigset_t *	set;	char set_[PAD_(const sigset_t *)];
	sigset_t *	oset;	char oset_[PAD_(sigset_t *)];
};
struct	sigsuspend_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const sigset_t *	sigmask;	char sigmask_[PAD_(const sigset_t *)];
};
struct	sigaction_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	sig;	char sig_[PAD_(int)];
	const struct sigaction *	act;	char act_[PAD_(const struct sigaction *)];
	struct sigaction *	oact;	char oact_[PAD_(struct sigaction *)];
};
struct	sigpending_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	sigset_t *	set;	char set_[PAD_(sigset_t *)];
};
struct	sigreturn_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	ucontext_t *	sigcntxp;	char sigcntxp_[PAD_(ucontext_t *)];
};
struct	sigtimedwait_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const sigset_t *	set;	char set_[PAD_(const sigset_t *)];
	siginfo_t *	info;	char info_[PAD_(siginfo_t *)];
	const struct timespec *	timeout;	char timeout_[PAD_(const struct timespec *)];
};
struct	sigwaitinfo_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const sigset_t *	set;	char set_[PAD_(const sigset_t *)];
	siginfo_t *	info;	char info_[PAD_(siginfo_t *)];
};
struct	__acl_get_file_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	__acl_set_file_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	__acl_get_fd_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	filedes;	char filedes_[PAD_(int)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	__acl_set_fd_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	filedes;	char filedes_[PAD_(int)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	__acl_delete_file_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
};
struct	__acl_delete_fd_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	filedes;	char filedes_[PAD_(int)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
};
struct	__acl_aclcheck_file_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	__acl_aclcheck_fd_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	filedes;	char filedes_[PAD_(int)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	extattrctl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	int	cmd;	char cmd_[PAD_(int)];
	const char *	filename;	char filename_[PAD_(const char *)];
	int	attrnamespace;	char attrnamespace_[PAD_(int)];
	const char *	attrname;	char attrname_[PAD_(const char *)];
};
struct	extattr_set_file_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	int	attrnamespace;	char attrnamespace_[PAD_(int)];
	const char *	attrname;	char attrname_[PAD_(const char *)];
	void *	data;	char data_[PAD_(void *)];
	size_t	nbytes;	char nbytes_[PAD_(size_t)];
};
struct	extattr_get_file_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	int	attrnamespace;	char attrnamespace_[PAD_(int)];
	const char *	attrname;	char attrname_[PAD_(const char *)];
	void *	data;	char data_[PAD_(void *)];
	size_t	nbytes;	char nbytes_[PAD_(size_t)];
};
struct	extattr_delete_file_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	int	attrnamespace;	char attrnamespace_[PAD_(int)];
	const char *	attrname;	char attrname_[PAD_(const char *)];
};
struct	aio_waitcomplete_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct aiocb **	aiocbp;	char aiocbp_[PAD_(struct aiocb **)];
	struct timespec *	timeout;	char timeout_[PAD_(struct timespec *)];
};
struct	getresuid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	uid_t *	ruid;	char ruid_[PAD_(uid_t *)];
	uid_t *	euid;	char euid_[PAD_(uid_t *)];
	uid_t *	suid;	char suid_[PAD_(uid_t *)];
};
struct	getresgid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	gid_t *	rgid;	char rgid_[PAD_(gid_t *)];
	gid_t *	egid;	char egid_[PAD_(gid_t *)];
	gid_t *	sgid;	char sgid_[PAD_(gid_t *)];
};
struct	kqueue_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	kevent_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	const struct kevent *	changelist;	char changelist_[PAD_(const struct kevent *)];
	int	nchanges;	char nchanges_[PAD_(int)];
	struct kevent *	eventlist;	char eventlist_[PAD_(struct kevent *)];
	int	nevents;	char nevents_[PAD_(int)];
	const struct timespec *	timeout;	char timeout_[PAD_(const struct timespec *)];
};
struct	lchflags_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	uuidgen_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct uuid *	store;	char store_[PAD_(struct uuid *)];
	int	count;	char count_[PAD_(int)];
};
struct	sendfile_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	int	s;	char s_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
	size_t	nbytes;	char nbytes_[PAD_(size_t)];
	struct sf_hdtr *	hdtr;	char hdtr_[PAD_(struct sf_hdtr *)];
	off_t *	sbytes;	char sbytes_[PAD_(off_t *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	varsym_set_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	level;	char level_[PAD_(int)];
	const char *	name;	char name_[PAD_(const char *)];
	const char *	data;	char data_[PAD_(const char *)];
};
struct	varsym_get_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	mask;	char mask_[PAD_(int)];
	const char *	wild;	char wild_[PAD_(const char *)];
	char *	buf;	char buf_[PAD_(char *)];
	int	bufsize;	char bufsize_[PAD_(int)];
};
struct	varsym_list_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	level;	char level_[PAD_(int)];
	char *	buf;	char buf_[PAD_(char *)];
	int	maxsize;	char maxsize_[PAD_(int)];
	int *	marker;	char marker_[PAD_(int *)];
};
struct	exec_sys_register_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	entry;	char entry_[PAD_(void *)];
};
struct	exec_sys_unregister_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	id;	char id_[PAD_(int)];
};
struct	sys_checkpoint_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	type;	char type_[PAD_(int)];
	int	fd;	char fd_[PAD_(int)];
	pid_t	pid;	char pid_[PAD_(pid_t)];
	int	retval;	char retval_[PAD_(int)];
};
struct	mountctl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	int	op;	char op_[PAD_(int)];
	int	fd;	char fd_[PAD_(int)];
	const void *	ctl;	char ctl_[PAD_(const void *)];
	int	ctllen;	char ctllen_[PAD_(int)];
	void *	buf;	char buf_[PAD_(void *)];
	int	buflen;	char buflen_[PAD_(int)];
};
struct	umtx_sleep_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	volatile const int *	ptr;	char ptr_[PAD_(volatile const int *)];
	int	value;	char value_[PAD_(int)];
	int	timeout;	char timeout_[PAD_(int)];
};
struct	umtx_wakeup_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	volatile const int *	ptr;	char ptr_[PAD_(volatile const int *)];
	int	count;	char count_[PAD_(int)];
};
struct	jail_attach_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	jid;	char jid_[PAD_(int)];
};
struct	set_tls_area_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	which;	char which_[PAD_(int)];
	struct tls_info *	info;	char info_[PAD_(struct tls_info *)];
	size_t	infosize;	char infosize_[PAD_(size_t)];
};
struct	get_tls_area_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	which;	char which_[PAD_(int)];
	struct tls_info *	info;	char info_[PAD_(struct tls_info *)];
	size_t	infosize;	char infosize_[PAD_(size_t)];
};
struct	closefrom_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
};
struct	stat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	struct stat *	ub;	char ub_[PAD_(struct stat *)];
};
struct	fstat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	struct stat *	sb;	char sb_[PAD_(struct stat *)];
};
struct	lstat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	struct stat *	ub;	char ub_[PAD_(struct stat *)];
};
struct	fhstat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const struct fhandle *	u_fhp;	char u_fhp_[PAD_(const struct fhandle *)];
	struct stat *	sb;	char sb_[PAD_(struct stat *)];
};
struct	getdirentries_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	buf;	char buf_[PAD_(char *)];
	u_int	count;	char count_[PAD_(u_int)];
	long *	basep;	char basep_[PAD_(long *)];
};
struct	getdents_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	buf;	char buf_[PAD_(char *)];
	size_t	count;	char count_[PAD_(size_t)];
};
struct	usched_set_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	pid_t	pid;	char pid_[PAD_(pid_t)];
	int	cmd;	char cmd_[PAD_(int)];
	void *	data;	char data_[PAD_(void *)];
	int	bytes;	char bytes_[PAD_(int)];
};
struct	extaccept_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int *	anamelen;	char anamelen_[PAD_(int *)];
};
struct	extconnect_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int	namelen;	char namelen_[PAD_(int)];
};
struct	mcontrol_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	behav;	char behav_[PAD_(int)];
	off_t	value;	char value_[PAD_(off_t)];
};
struct	vmspace_create_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	id;	char id_[PAD_(void *)];
	int	type;	char type_[PAD_(int)];
	void *	data;	char data_[PAD_(void *)];
};
struct	vmspace_destroy_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	id;	char id_[PAD_(void *)];
};
struct	vmspace_ctl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	id;	char id_[PAD_(void *)];
	int	cmd;	char cmd_[PAD_(int)];
	struct trapframe *	tframe;	char tframe_[PAD_(struct trapframe *)];
	struct vextframe *	vframe;	char vframe_[PAD_(struct vextframe *)];
};
struct	vmspace_mmap_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	id;	char id_[PAD_(void *)];
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	prot;	char prot_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	int	fd;	char fd_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	vmspace_munmap_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	id;	char id_[PAD_(void *)];
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	vmspace_mcontrol_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	id;	char id_[PAD_(void *)];
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	behav;	char behav_[PAD_(int)];
	off_t	value;	char value_[PAD_(off_t)];
};
struct	vmspace_pread_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	id;	char id_[PAD_(void *)];
	void *	buf;	char buf_[PAD_(void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	vmspace_pwrite_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	id;	char id_[PAD_(void *)];
	const void *	buf;	char buf_[PAD_(const void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	extexit_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	how;	char how_[PAD_(int)];
	int	status;	char status_[PAD_(int)];
	void *	addr;	char addr_[PAD_(void *)];
};
struct	lwp_create_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct lwp_params *	params;	char params_[PAD_(struct lwp_params *)];
};
struct	lwp_gettid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	register_t dummy;
};
struct	lwp_kill_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	pid_t	pid;	char pid_[PAD_(pid_t)];
	lwpid_t	tid;	char tid_[PAD_(lwpid_t)];
	int	signum;	char signum_[PAD_(int)];
};
struct	lwp_rtprio_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	function;	char function_[PAD_(int)];
	pid_t	pid;	char pid_[PAD_(pid_t)];
	lwpid_t	tid;	char tid_[PAD_(lwpid_t)];
	struct rtprio *	rtp;	char rtp_[PAD_(struct rtprio *)];
};
struct	pselect_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	nd;	char nd_[PAD_(int)];
	fd_set *	in;	char in_[PAD_(fd_set *)];
	fd_set *	ou;	char ou_[PAD_(fd_set *)];
	fd_set *	ex;	char ex_[PAD_(fd_set *)];
	const struct timespec *	ts;	char ts_[PAD_(const struct timespec *)];
	const sigset_t *	sigmask;	char sigmask_[PAD_(const sigset_t *)];
};
struct	statvfs_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	struct statvfs *	buf;	char buf_[PAD_(struct statvfs *)];
};
struct	fstatvfs_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	struct statvfs *	buf;	char buf_[PAD_(struct statvfs *)];
};
struct	fhstatvfs_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const struct fhandle *	u_fhp;	char u_fhp_[PAD_(const struct fhandle *)];
	struct statvfs *	buf;	char buf_[PAD_(struct statvfs *)];
};
struct	getvfsstat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct statfs *	buf;	char buf_[PAD_(struct statfs *)];
	struct statvfs *	vbuf;	char vbuf_[PAD_(struct statvfs *)];
	long	vbufsize;	char vbufsize_[PAD_(long)];
	int	flags;	char flags_[PAD_(int)];
};
struct	openat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
	int	mode;	char mode_[PAD_(int)];
};
struct	fstatat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	struct stat *	sb;	char sb_[PAD_(struct stat *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	fchmodat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
};
struct	fchownat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	int	uid;	char uid_[PAD_(int)];
	int	gid;	char gid_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
};
struct	unlinkat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	faccessat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	int	amode;	char amode_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
};
struct	mq_open_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	name;	char name_[PAD_(const char *)];
	int	oflag;	char oflag_[PAD_(int)];
	mode_t	mode;	char mode_[PAD_(mode_t)];
	struct mq_attr *	attr;	char attr_[PAD_(struct mq_attr *)];
};
struct	mq_close_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
};
struct	mq_unlink_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	name;	char name_[PAD_(const char *)];
};
struct	mq_getattr_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	struct mq_attr *	mqstat;	char mqstat_[PAD_(struct mq_attr *)];
};
struct	mq_setattr_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	const struct mq_attr *	mqstat;	char mqstat_[PAD_(const struct mq_attr *)];
	struct mq_attr *	omqstat;	char omqstat_[PAD_(struct mq_attr *)];
};
struct	mq_notify_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	const struct sigevent *	notification;	char notification_[PAD_(const struct sigevent *)];
};
struct	mq_send_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	const char *	msg_ptr;	char msg_ptr_[PAD_(const char *)];
	size_t	msg_len;	char msg_len_[PAD_(size_t)];
	unsigned	msg_prio;	char msg_prio_[PAD_(unsigned)];
};
struct	mq_receive_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	char *	msg_ptr;	char msg_ptr_[PAD_(char *)];
	size_t	msg_len;	char msg_len_[PAD_(size_t)];
	unsigned *	msg_prio;	char msg_prio_[PAD_(unsigned *)];
};
struct	mq_timedsend_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	const char *	msg_ptr;	char msg_ptr_[PAD_(const char *)];
	size_t	msg_len;	char msg_len_[PAD_(size_t)];
	unsigned	msg_prio;	char msg_prio_[PAD_(unsigned)];
	const struct timespec *	abs_timeout;	char abs_timeout_[PAD_(const struct timespec *)];
};
struct	mq_timedreceive_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	char *	msg_ptr;	char msg_ptr_[PAD_(char *)];
	size_t	msg_len;	char msg_len_[PAD_(size_t)];
	unsigned *	msg_prio;	char msg_prio_[PAD_(unsigned *)];
	const struct timespec *	abs_timeout;	char abs_timeout_[PAD_(const struct timespec *)];
};
struct	ioprio_set_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	which;	char which_[PAD_(int)];
	int	who;	char who_[PAD_(int)];
	int	prio;	char prio_[PAD_(int)];
};
struct	ioprio_get_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	which;	char which_[PAD_(int)];
	int	who;	char who_[PAD_(int)];
};
struct	chroot_kernel_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
};
struct	renameat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	oldfd;	char oldfd_[PAD_(int)];
	char *	old;	char old_[PAD_(char *)];
	int	newfd;	char newfd_[PAD_(int)];
	char *	new;	char new_[PAD_(char *)];
};
struct	mkdirat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	mode_t	mode;	char mode_[PAD_(mode_t)];
};
struct	mkfifoat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	mode_t	mode;	char mode_[PAD_(mode_t)];
};
struct	mknodat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	mode_t	mode;	char mode_[PAD_(mode_t)];
	dev_t	dev;	char dev_[PAD_(dev_t)];
};
struct	readlinkat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	char *	buf;	char buf_[PAD_(char *)];
	size_t	bufsize;	char bufsize_[PAD_(size_t)];
};
struct	symlinkat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path1;	char path1_[PAD_(char *)];
	int	fd;	char fd_[PAD_(int)];
	char *	path2;	char path2_[PAD_(char *)];
};
struct	swapoff_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	name;	char name_[PAD_(char *)];
};
struct	vquotactl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	const char *	path;	char path_[PAD_(const char *)];
	struct plistref *	pref;	char pref_[PAD_(struct plistref *)];
};
struct	linkat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd1;	char fd1_[PAD_(int)];
	char *	path1;	char path1_[PAD_(char *)];
	int	fd2;	char fd2_[PAD_(int)];
	char *	path2;	char path2_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	eaccess_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	lpathconf_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	name;	char name_[PAD_(int)];
};
struct	vmm_guest_ctl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	op;	char op_[PAD_(int)];
	struct vmm_guest_options *	options;	char options_[PAD_(struct vmm_guest_options *)];
};
struct	vmm_guest_sync_addr_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	long *	dstaddr;	char dstaddr_[PAD_(long *)];
	long *	srcaddr;	char srcaddr_[PAD_(long *)];
};
struct	procctl_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	idtype_t	idtype;	char idtype_[PAD_(idtype_t)];
	id_t	id;	char id_[PAD_(id_t)];
	int	cmd;	char cmd_[PAD_(int)];
	void *	data;	char data_[PAD_(void *)];
};
struct	chflagsat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	const char *	path;	char path_[PAD_(const char *)];
	int	flags;	char flags_[PAD_(int)];
	int	atflags;	char atflags_[PAD_(int)];
};
struct	pipe2_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int *	fildes;	char fildes_[PAD_(int *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	utimensat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	const char *	path;	char path_[PAD_(const char *)];
	const struct timespec *	ts;	char ts_[PAD_(const struct timespec *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	futimens_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	const struct timespec *	ts;	char ts_[PAD_(const struct timespec *)];
};

#ifdef COMPAT_43

#ifdef COMPAT_43
#endif
struct	ocreat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
};
struct	olseek_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	long	offset;	char offset_[PAD_(long)];
	int	whence;	char whence_[PAD_(int)];
};
struct	ostat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	struct ostat *	ub;	char ub_[PAD_(struct ostat *)];
};
struct	olstat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	struct ostat *	ub;	char ub_[PAD_(struct ostat *)];
};
struct	ofstat_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	struct ostat *	sb;	char sb_[PAD_(struct ostat *)];
};
struct	getkerninfo_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	op;	char op_[PAD_(int)];
	char *	where;	char where_[PAD_(char *)];
	size_t *	size;	char size_[PAD_(size_t *)];
	int	arg;	char arg_[PAD_(int)];
};
struct	ommap_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	void *	addr;	char addr_[PAD_(void *)];
	int	len;	char len_[PAD_(int)];
	int	prot;	char prot_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	int	fd;	char fd_[PAD_(int)];
	long	pos;	char pos_[PAD_(long)];
};
struct	ovadvise_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	anom;	char anom_[PAD_(int)];
};
struct	gethostname_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	hostname;	char hostname_[PAD_(char *)];
	u_int	len;	char len_[PAD_(u_int)];
};
struct	sethostname_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	hostname;	char hostname_[PAD_(char *)];
	u_int	len;	char len_[PAD_(u_int)];
};
struct	osend_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	caddr_t	buf;	char buf_[PAD_(caddr_t)];
	int	len;	char len_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
};
struct	orecv_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	caddr_t	buf;	char buf_[PAD_(caddr_t)];
	int	len;	char len_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
};
struct	osigvec_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	signum;	char signum_[PAD_(int)];
	struct sigvec *	nsv;	char nsv_[PAD_(struct sigvec *)];
	struct sigvec *	osv;	char osv_[PAD_(struct sigvec *)];
};
struct	osigblock_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	mask;	char mask_[PAD_(int)];
};
struct	osigsetmask_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	mask;	char mask_[PAD_(int)];
};
struct	osigstack_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	struct sigstack *	nss;	char nss_[PAD_(struct sigstack *)];
	struct sigstack *	oss;	char oss_[PAD_(struct sigstack *)];
};
struct	orecvmsg_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	struct omsghdr *	msg;	char msg_[PAD_(struct omsghdr *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	osendmsg_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	s;	char s_[PAD_(int)];
	caddr_t	msg;	char msg_[PAD_(caddr_t)];
	int	flags;	char flags_[PAD_(int)];
};
struct	otruncate_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	char *	path;	char path_[PAD_(char *)];
	long	length;	char length_[PAD_(long)];
};
struct	oftruncate_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	long	length;	char length_[PAD_(long)];
};
struct	ogetpeername_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fdes;	char fdes_[PAD_(int)];
	caddr_t	asa;	char asa_[PAD_(caddr_t)];
	int *	alen;	char alen_[PAD_(int *)];
};
struct	osethostid_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	long	hostid;	char hostid_[PAD_(long)];
};
struct	ogetrlimit_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_int	which;	char which_[PAD_(u_int)];
	struct orlimit *	rlp;	char rlp_[PAD_(struct orlimit *)];
};
struct	osetrlimit_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	u_int	which;	char which_[PAD_(u_int)];
	struct orlimit *	rlp;	char rlp_[PAD_(struct orlimit *)];
};
struct	okillpg_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	pgid;	char pgid_[PAD_(int)];
	int	signum;	char signum_[PAD_(int)];
};
struct	ogetdirentries_args {
#ifdef _KERNEL
	struct sysmsg sysmsg;
#endif
	int	fd;	char fd_[PAD_(int)];
	char *	buf;	char buf_[PAD_(char *)];
	u_int	count;	char count_[PAD_(u_int)];
	long *	basep;	char basep_[PAD_(long *)];
};

#ifdef _KERNEL

int	sys_ocreat (struct ocreat_args *);
int	sys_olseek (struct olseek_args *);
int	sys_ostat (struct ostat_args *);
int	sys_olstat (struct olstat_args *);
int	sys_ofstat (struct ofstat_args *);
int	sys_ogetkerninfo (struct getkerninfo_args *);
int	sys_ogetpagesize (struct getpagesize_args *);
int	sys_ommap (struct ommap_args *);
int	sys_ovadvise (struct ovadvise_args *);
int	sys_owait (struct owait_args *);
int	sys_ogethostname (struct gethostname_args *);
int	sys_osethostname (struct sethostname_args *);
int	sys_oaccept (struct accept_args *);
int	sys_osend (struct osend_args *);
int	sys_orecv (struct orecv_args *);
int	sys_osigvec (struct osigvec_args *);
int	sys_osigblock (struct osigblock_args *);
int	sys_osigsetmask (struct osigsetmask_args *);
int	sys_osigstack (struct osigstack_args *);
int	sys_orecvmsg (struct orecvmsg_args *);
int	sys_osendmsg (struct osendmsg_args *);
int	sys_orecvfrom (struct recvfrom_args *);
int	sys_otruncate (struct otruncate_args *);
int	sys_oftruncate (struct oftruncate_args *);
int	sys_ogetpeername (struct ogetpeername_args *);
int	sys_ogethostid (struct ogethostid_args *);
int	sys_osethostid (struct osethostid_args *);
int	sys_ogetrlimit (struct ogetrlimit_args *);
int	sys_osetrlimit (struct osetrlimit_args *);
int	sys_okillpg (struct okillpg_args *);
int	sys_oquota (struct oquota_args *);
int	sys_ogetsockname (struct getsockname_args *);
int	sys_ogetdirentries (struct ogetdirentries_args *);

#endif /* _KERNEL */

#endif /* COMPAT_43 */


#ifdef _KERNEL

#ifdef COMPAT_43
#endif
int	sys_nosys (struct nosys_args *);
int	sys_exit (struct exit_args *);
int	sys_fork (struct fork_args *);
int	sys_read (struct read_args *);
int	sys_write (struct write_args *);
int	sys_open (struct open_args *);
int	sys_close (struct close_args *);
int	sys_wait4 (struct wait_args *);
int	sys_link (struct link_args *);
int	sys_unlink (struct unlink_args *);
int	sys_chdir (struct chdir_args *);
int	sys_fchdir (struct fchdir_args *);
int	sys_mknod (struct mknod_args *);
int	sys_chmod (struct chmod_args *);
int	sys_chown (struct chown_args *);
int	sys_obreak (struct obreak_args *);
int	sys_getfsstat (struct getfsstat_args *);
int	sys_getpid (struct getpid_args *);
int	sys_mount (struct mount_args *);
int	sys_unmount (struct unmount_args *);
int	sys_setuid (struct setuid_args *);
int	sys_getuid (struct getuid_args *);
int	sys_geteuid (struct geteuid_args *);
int	sys_ptrace (struct ptrace_args *);
int	sys_recvmsg (struct recvmsg_args *);
int	sys_sendmsg (struct sendmsg_args *);
int	sys_recvfrom (struct recvfrom_args *);
int	sys_accept (struct accept_args *);
int	sys_getpeername (struct getpeername_args *);
int	sys_getsockname (struct getsockname_args *);
int	sys_access (struct access_args *);
int	sys_chflags (struct chflags_args *);
int	sys_fchflags (struct fchflags_args *);
int	sys_sync (struct sync_args *);
int	sys_kill (struct kill_args *);
int	sys_getppid (struct getppid_args *);
int	sys_dup (struct dup_args *);
int	sys_pipe (struct pipe_args *);
int	sys_getegid (struct getegid_args *);
int	sys_profil (struct profil_args *);
int	sys_ktrace (struct ktrace_args *);
int	sys_getgid (struct getgid_args *);
int	sys_getlogin (struct getlogin_args *);
int	sys_setlogin (struct setlogin_args *);
int	sys_acct (struct acct_args *);
int	sys_sigaltstack (struct sigaltstack_args *);
int	sys_ioctl (struct ioctl_args *);
int	sys_reboot (struct reboot_args *);
int	sys_revoke (struct revoke_args *);
int	sys_symlink (struct symlink_args *);
int	sys_readlink (struct readlink_args *);
int	sys_execve (struct execve_args *);
int	sys_umask (struct umask_args *);
int	sys_chroot (struct chroot_args *);
int	sys_msync (struct msync_args *);
int	sys_vfork (struct vfork_args *);
int	sys_sbrk (struct sbrk_args *);
int	sys_sstk (struct sstk_args *);
int	sys_munmap (struct munmap_args *);
int	sys_mprotect (struct mprotect_args *);
int	sys_madvise (struct madvise_args *);
int	sys_mincore (struct mincore_args *);
int	sys_getgroups (struct getgroups_args *);
int	sys_setgroups (struct setgroups_args *);
int	sys_getpgrp (struct getpgrp_args *);
int	sys_setpgid (struct setpgid_args *);
int	sys_setitimer (struct setitimer_args *);
int	sys_swapon (struct swapon_args *);
int	sys_getitimer (struct getitimer_args *);
int	sys_getdtablesize (struct getdtablesize_args *);
int	sys_dup2 (struct dup2_args *);
int	sys_fcntl (struct fcntl_args *);
int	sys_select (struct select_args *);
int	sys_fsync (struct fsync_args *);
int	sys_setpriority (struct setpriority_args *);
int	sys_socket (struct socket_args *);
int	sys_connect (struct connect_args *);
int	sys_getpriority (struct getpriority_args *);
int	sys_bind (struct bind_args *);
int	sys_setsockopt (struct setsockopt_args *);
int	sys_listen (struct listen_args *);
int	sys_gettimeofday (struct gettimeofday_args *);
int	sys_getrusage (struct getrusage_args *);
int	sys_getsockopt (struct getsockopt_args *);
int	sys_readv (struct readv_args *);
int	sys_writev (struct writev_args *);
int	sys_settimeofday (struct settimeofday_args *);
int	sys_fchown (struct fchown_args *);
int	sys_fchmod (struct fchmod_args *);
int	sys_setreuid (struct setreuid_args *);
int	sys_setregid (struct setregid_args *);
int	sys_rename (struct rename_args *);
int	sys_flock (struct flock_args *);
int	sys_mkfifo (struct mkfifo_args *);
int	sys_sendto (struct sendto_args *);
int	sys_shutdown (struct shutdown_args *);
int	sys_socketpair (struct socketpair_args *);
int	sys_mkdir (struct mkdir_args *);
int	sys_rmdir (struct rmdir_args *);
int	sys_utimes (struct utimes_args *);
int	sys_adjtime (struct adjtime_args *);
int	sys_setsid (struct setsid_args *);
int	sys_quotactl (struct quotactl_args *);
int	sys_nfssvc (struct nfssvc_args *);
int	sys_statfs (struct statfs_args *);
int	sys_fstatfs (struct fstatfs_args *);
int	sys_getfh (struct getfh_args *);
int	sys_getdomainname (struct getdomainname_args *);
int	sys_setdomainname (struct setdomainname_args *);
int	sys_uname (struct uname_args *);
int	sys_sysarch (struct sysarch_args *);
int	sys_rtprio (struct rtprio_args *);
int	sys_extpread (struct extpread_args *);
int	sys_extpwrite (struct extpwrite_args *);
int	sys_ntp_adjtime (struct ntp_adjtime_args *);
int	sys_setgid (struct setgid_args *);
int	sys_setegid (struct setegid_args *);
int	sys_seteuid (struct seteuid_args *);
int	sys_pathconf (struct pathconf_args *);
int	sys_fpathconf (struct fpathconf_args *);
int	sys_getrlimit (struct __getrlimit_args *);
int	sys_setrlimit (struct __setrlimit_args *);
int	sys_mmap (struct mmap_args *);
int	sys_lseek (struct lseek_args *);
int	sys_truncate (struct truncate_args *);
int	sys_ftruncate (struct ftruncate_args *);
int	sys___sysctl (struct sysctl_args *);
int	sys_mlock (struct mlock_args *);
int	sys_munlock (struct munlock_args *);
int	sys_undelete (struct undelete_args *);
int	sys_futimes (struct futimes_args *);
int	sys_getpgid (struct getpgid_args *);
int	sys_poll (struct poll_args *);
int	sys_lkmnosys (struct nosys_args *);
int	sys___semctl (struct __semctl_args *);
int	sys_semget (struct semget_args *);
int	sys_semop (struct semop_args *);
int	sys_msgctl (struct msgctl_args *);
int	sys_msgget (struct msgget_args *);
int	sys_msgsnd (struct msgsnd_args *);
int	sys_msgrcv (struct msgrcv_args *);
int	sys_shmat (struct shmat_args *);
int	sys_shmctl (struct shmctl_args *);
int	sys_shmdt (struct shmdt_args *);
int	sys_shmget (struct shmget_args *);
int	sys_clock_gettime (struct clock_gettime_args *);
int	sys_clock_settime (struct clock_settime_args *);
int	sys_clock_getres (struct clock_getres_args *);
int	sys_nanosleep (struct nanosleep_args *);
int	sys_minherit (struct minherit_args *);
int	sys_rfork (struct rfork_args *);
int	sys_openbsd_poll (struct openbsd_poll_args *);
int	sys_issetugid (struct issetugid_args *);
int	sys_lchown (struct lchown_args *);
int	sys_lchmod (struct lchmod_args *);
int	sys_lutimes (struct lutimes_args *);
int	sys_extpreadv (struct extpreadv_args *);
int	sys_extpwritev (struct extpwritev_args *);
int	sys_fhstatfs (struct fhstatfs_args *);
int	sys_fhopen (struct fhopen_args *);
int	sys_modnext (struct modnext_args *);
int	sys_modstat (struct modstat_args *);
int	sys_modfnext (struct modfnext_args *);
int	sys_modfind (struct modfind_args *);
int	sys_kldload (struct kldload_args *);
int	sys_kldunload (struct kldunload_args *);
int	sys_kldfind (struct kldfind_args *);
int	sys_kldnext (struct kldnext_args *);
int	sys_kldstat (struct kldstat_args *);
int	sys_kldfirstmod (struct kldfirstmod_args *);
int	sys_getsid (struct getsid_args *);
int	sys_setresuid (struct setresuid_args *);
int	sys_setresgid (struct setresgid_args *);
int	sys_aio_return (struct aio_return_args *);
int	sys_aio_suspend (struct aio_suspend_args *);
int	sys_aio_cancel (struct aio_cancel_args *);
int	sys_aio_error (struct aio_error_args *);
int	sys_aio_read (struct aio_read_args *);
int	sys_aio_write (struct aio_write_args *);
int	sys_lio_listio (struct lio_listio_args *);
int	sys_yield (struct yield_args *);
int	sys_mlockall (struct mlockall_args *);
int	sys_munlockall (struct munlockall_args *);
int	sys___getcwd (struct __getcwd_args *);
int	sys_sched_setparam (struct sched_setparam_args *);
int	sys_sched_getparam (struct sched_getparam_args *);
int	sys_sched_setscheduler (struct sched_setscheduler_args *);
int	sys_sched_getscheduler (struct sched_getscheduler_args *);
int	sys_sched_yield (struct sched_yield_args *);
int	sys_sched_get_priority_max (struct sched_get_priority_max_args *);
int	sys_sched_get_priority_min (struct sched_get_priority_min_args *);
int	sys_sched_rr_get_interval (struct sched_rr_get_interval_args *);
int	sys_utrace (struct utrace_args *);
int	sys_kldsym (struct kldsym_args *);
int	sys_jail (struct jail_args *);
int	sys_sigprocmask (struct sigprocmask_args *);
int	sys_sigsuspend (struct sigsuspend_args *);
int	sys_sigaction (struct sigaction_args *);
int	sys_sigpending (struct sigpending_args *);
int	sys_sigreturn (struct sigreturn_args *);
int	sys_sigtimedwait (struct sigtimedwait_args *);
int	sys_sigwaitinfo (struct sigwaitinfo_args *);
int	sys___acl_get_file (struct __acl_get_file_args *);
int	sys___acl_set_file (struct __acl_set_file_args *);
int	sys___acl_get_fd (struct __acl_get_fd_args *);
int	sys___acl_set_fd (struct __acl_set_fd_args *);
int	sys___acl_delete_file (struct __acl_delete_file_args *);
int	sys___acl_delete_fd (struct __acl_delete_fd_args *);
int	sys___acl_aclcheck_file (struct __acl_aclcheck_file_args *);
int	sys___acl_aclcheck_fd (struct __acl_aclcheck_fd_args *);
int	sys_extattrctl (struct extattrctl_args *);
int	sys_extattr_set_file (struct extattr_set_file_args *);
int	sys_extattr_get_file (struct extattr_get_file_args *);
int	sys_extattr_delete_file (struct extattr_delete_file_args *);
int	sys_aio_waitcomplete (struct aio_waitcomplete_args *);
int	sys_getresuid (struct getresuid_args *);
int	sys_getresgid (struct getresgid_args *);
int	sys_kqueue (struct kqueue_args *);
int	sys_kevent (struct kevent_args *);
int	sys_lchflags (struct lchflags_args *);
int	sys_uuidgen (struct uuidgen_args *);
int	sys_sendfile (struct sendfile_args *);
int	sys_varsym_set (struct varsym_set_args *);
int	sys_varsym_get (struct varsym_get_args *);
int	sys_varsym_list (struct varsym_list_args *);
int	sys_exec_sys_register (struct exec_sys_register_args *);
int	sys_exec_sys_unregister (struct exec_sys_unregister_args *);
int	sys_sys_checkpoint (struct sys_checkpoint_args *);
int	sys_mountctl (struct mountctl_args *);
int	sys_umtx_sleep (struct umtx_sleep_args *);
int	sys_umtx_wakeup (struct umtx_wakeup_args *);
int	sys_jail_attach (struct jail_attach_args *);
int	sys_set_tls_area (struct set_tls_area_args *);
int	sys_get_tls_area (struct get_tls_area_args *);
int	sys_closefrom (struct closefrom_args *);
int	sys_stat (struct stat_args *);
int	sys_fstat (struct fstat_args *);
int	sys_lstat (struct lstat_args *);
int	sys_fhstat (struct fhstat_args *);
int	sys_getdirentries (struct getdirentries_args *);
int	sys_getdents (struct getdents_args *);
int	sys_usched_set (struct usched_set_args *);
int	sys_extaccept (struct extaccept_args *);
int	sys_extconnect (struct extconnect_args *);
int	sys_mcontrol (struct mcontrol_args *);
int	sys_vmspace_create (struct vmspace_create_args *);
int	sys_vmspace_destroy (struct vmspace_destroy_args *);
int	sys_vmspace_ctl (struct vmspace_ctl_args *);
int	sys_vmspace_mmap (struct vmspace_mmap_args *);
int	sys_vmspace_munmap (struct vmspace_munmap_args *);
int	sys_vmspace_mcontrol (struct vmspace_mcontrol_args *);
int	sys_vmspace_pread (struct vmspace_pread_args *);
int	sys_vmspace_pwrite (struct vmspace_pwrite_args *);
int	sys_extexit (struct extexit_args *);
int	sys_lwp_create (struct lwp_create_args *);
int	sys_lwp_gettid (struct lwp_gettid_args *);
int	sys_lwp_kill (struct lwp_kill_args *);
int	sys_lwp_rtprio (struct lwp_rtprio_args *);
int	sys_pselect (struct pselect_args *);
int	sys_statvfs (struct statvfs_args *);
int	sys_fstatvfs (struct fstatvfs_args *);
int	sys_fhstatvfs (struct fhstatvfs_args *);
int	sys_getvfsstat (struct getvfsstat_args *);
int	sys_openat (struct openat_args *);
int	sys_fstatat (struct fstatat_args *);
int	sys_fchmodat (struct fchmodat_args *);
int	sys_fchownat (struct fchownat_args *);
int	sys_unlinkat (struct unlinkat_args *);
int	sys_faccessat (struct faccessat_args *);
int	sys_mq_open (struct mq_open_args *);
int	sys_mq_close (struct mq_close_args *);
int	sys_mq_unlink (struct mq_unlink_args *);
int	sys_mq_getattr (struct mq_getattr_args *);
int	sys_mq_setattr (struct mq_setattr_args *);
int	sys_mq_notify (struct mq_notify_args *);
int	sys_mq_send (struct mq_send_args *);
int	sys_mq_receive (struct mq_receive_args *);
int	sys_mq_timedsend (struct mq_timedsend_args *);
int	sys_mq_timedreceive (struct mq_timedreceive_args *);
int	sys_ioprio_set (struct ioprio_set_args *);
int	sys_ioprio_get (struct ioprio_get_args *);
int	sys_chroot_kernel (struct chroot_kernel_args *);
int	sys_renameat (struct renameat_args *);
int	sys_mkdirat (struct mkdirat_args *);
int	sys_mkfifoat (struct mkfifoat_args *);
int	sys_mknodat (struct mknodat_args *);
int	sys_readlinkat (struct readlinkat_args *);
int	sys_symlinkat (struct symlinkat_args *);
int	sys_swapoff (struct swapoff_args *);
int	sys_vquotactl (struct vquotactl_args *);
int	sys_linkat (struct linkat_args *);
int	sys_eaccess (struct eaccess_args *);
int	sys_lpathconf (struct lpathconf_args *);
int	sys_vmm_guest_ctl (struct vmm_guest_ctl_args *);
int	sys_vmm_guest_sync_addr (struct vmm_guest_sync_addr_args *);
int	sys_procctl (struct procctl_args *);
int	sys_chflagsat (struct chflagsat_args *);
int	sys_pipe2 (struct pipe2_args *);
int	sys_utimensat (struct utimensat_args *);
int	sys_futimens (struct futimens_args *);

#endif /* !_SYS_SYSPROTO_H_ */
#undef PAD_

#endif /* _KERNEL */
