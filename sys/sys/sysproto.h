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
#include <sys/cpumask.h>
#include <sys/mqueue.h>
#include <sys/msgport.h>
#include <sys/sysmsg.h>
#include <sys/procctl.h>

#define	PAD_(t)	(sizeof(register_t) <= sizeof(t) ? \
		0 : sizeof(register_t) - sizeof(t))

struct	exit_args {
	int	rval;	char rval_[PAD_(int)];
};
struct	fork_args {
	register_t dummy;
};
struct	read_args {
	int	fd;	char fd_[PAD_(int)];
	void *	buf;	char buf_[PAD_(void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
};
struct	write_args {
	int	fd;	char fd_[PAD_(int)];
	const void *	buf;	char buf_[PAD_(const void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
};
struct	open_args {
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
	int	mode;	char mode_[PAD_(int)];
};
struct	close_args {
	int	fd;	char fd_[PAD_(int)];
};
struct	wait_args {
	int	pid;	char pid_[PAD_(int)];
	int *	status;	char status_[PAD_(int *)];
	int	options;	char options_[PAD_(int)];
	struct rusage *	rusage;	char rusage_[PAD_(struct rusage *)];
};
struct	nosys_args {
	register_t dummy;
};
struct	link_args {
	char *	path;	char path_[PAD_(char *)];
	char *	link;	char link_[PAD_(char *)];
};
struct	unlink_args {
	char *	path;	char path_[PAD_(char *)];
};
struct	chdir_args {
	char *	path;	char path_[PAD_(char *)];
};
struct	fchdir_args {
	int	fd;	char fd_[PAD_(int)];
};
struct	mknod_args {
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
	int	dev;	char dev_[PAD_(int)];
};
struct	chmod_args {
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
};
struct	chown_args {
	char *	path;	char path_[PAD_(char *)];
	int	uid;	char uid_[PAD_(int)];
	int	gid;	char gid_[PAD_(int)];
};
struct	obreak_args {
	char *	nsize;	char nsize_[PAD_(char *)];
};
struct	getfsstat_args {
	struct statfs *	buf;	char buf_[PAD_(struct statfs *)];
	long	bufsize;	char bufsize_[PAD_(long)];
	int	flags;	char flags_[PAD_(int)];
};
struct	getpid_args {
	register_t dummy;
};
struct	mount_args {
	char *	type;	char type_[PAD_(char *)];
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
	caddr_t	data;	char data_[PAD_(caddr_t)];
};
struct	unmount_args {
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	setuid_args {
	uid_t	uid;	char uid_[PAD_(uid_t)];
};
struct	getuid_args {
	register_t dummy;
};
struct	geteuid_args {
	register_t dummy;
};
struct	ptrace_args {
	int	req;	char req_[PAD_(int)];
	pid_t	pid;	char pid_[PAD_(pid_t)];
	caddr_t	addr;	char addr_[PAD_(caddr_t)];
	int	data;	char data_[PAD_(int)];
};
struct	recvmsg_args {
	int	s;	char s_[PAD_(int)];
	struct msghdr *	msg;	char msg_[PAD_(struct msghdr *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	sendmsg_args {
	int	s;	char s_[PAD_(int)];
	caddr_t	msg;	char msg_[PAD_(caddr_t)];
	int	flags;	char flags_[PAD_(int)];
};
struct	recvfrom_args {
	int	s;	char s_[PAD_(int)];
	caddr_t	buf;	char buf_[PAD_(caddr_t)];
	size_t	len;	char len_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	caddr_t	from;	char from_[PAD_(caddr_t)];
	int *	fromlenaddr;	char fromlenaddr_[PAD_(int *)];
};
struct	accept_args {
	int	s;	char s_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int *	anamelen;	char anamelen_[PAD_(int *)];
};
struct	getpeername_args {
	int	fdes;	char fdes_[PAD_(int)];
	caddr_t	asa;	char asa_[PAD_(caddr_t)];
	int *	alen;	char alen_[PAD_(int *)];
};
struct	getsockname_args {
	int	fdes;	char fdes_[PAD_(int)];
	caddr_t	asa;	char asa_[PAD_(caddr_t)];
	int *	alen;	char alen_[PAD_(int *)];
};
struct	access_args {
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	chflags_args {
	const char *	path;	char path_[PAD_(const char *)];
	u_long	flags;	char flags_[PAD_(u_long)];
};
struct	fchflags_args {
	int	fd;	char fd_[PAD_(int)];
	u_long	flags;	char flags_[PAD_(u_long)];
};
struct	sync_args {
	register_t dummy;
};
struct	kill_args {
	int	pid;	char pid_[PAD_(int)];
	int	signum;	char signum_[PAD_(int)];
};
struct	getppid_args {
	register_t dummy;
};
struct	dup_args {
	int	fd;	char fd_[PAD_(int)];
};
struct	pipe_args {
	register_t dummy;
};
struct	getegid_args {
	register_t dummy;
};
struct	profil_args {
	caddr_t	samples;	char samples_[PAD_(caddr_t)];
	size_t	size;	char size_[PAD_(size_t)];
	u_long	offset;	char offset_[PAD_(u_long)];
	u_int	scale;	char scale_[PAD_(u_int)];
};
struct	ktrace_args {
	const char *	fname;	char fname_[PAD_(const char *)];
	int	ops;	char ops_[PAD_(int)];
	int	facs;	char facs_[PAD_(int)];
	int	pid;	char pid_[PAD_(int)];
};
struct	getgid_args {
	register_t dummy;
};
struct	getlogin_args {
	char *	namebuf;	char namebuf_[PAD_(char *)];
	size_t	namelen;	char namelen_[PAD_(size_t)];
};
struct	setlogin_args {
	char *	namebuf;	char namebuf_[PAD_(char *)];
};
struct	acct_args {
	char *	path;	char path_[PAD_(char *)];
};
struct	sigaltstack_args {
	stack_t *	ss;	char ss_[PAD_(stack_t *)];
	stack_t *	oss;	char oss_[PAD_(stack_t *)];
};
struct	ioctl_args {
	int	fd;	char fd_[PAD_(int)];
	u_long	com;	char com_[PAD_(u_long)];
	caddr_t	data;	char data_[PAD_(caddr_t)];
};
struct	reboot_args {
	int	opt;	char opt_[PAD_(int)];
};
struct	revoke_args {
	char *	path;	char path_[PAD_(char *)];
};
struct	symlink_args {
	char *	path;	char path_[PAD_(char *)];
	char *	link;	char link_[PAD_(char *)];
};
struct	readlink_args {
	char *	path;	char path_[PAD_(char *)];
	char *	buf;	char buf_[PAD_(char *)];
	int	count;	char count_[PAD_(int)];
};
struct	execve_args {
	char *	fname;	char fname_[PAD_(char *)];
	char **	argv;	char argv_[PAD_(char **)];
	char **	envv;	char envv_[PAD_(char **)];
};
struct	umask_args {
	int	newmask;	char newmask_[PAD_(int)];
};
struct	chroot_args {
	char *	path;	char path_[PAD_(char *)];
};
struct	msync_args {
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
};
struct	vfork_args {
	register_t dummy;
};
struct	sbrk_args {
	size_t	incr;	char incr_[PAD_(size_t)];
};
struct	sstk_args {
	size_t	incr;	char incr_[PAD_(size_t)];
};
struct	munmap_args {
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	mprotect_args {
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	prot;	char prot_[PAD_(int)];
};
struct	madvise_args {
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	behav;	char behav_[PAD_(int)];
};
struct	mincore_args {
	const void *	addr;	char addr_[PAD_(const void *)];
	size_t	len;	char len_[PAD_(size_t)];
	char *	vec;	char vec_[PAD_(char *)];
};
struct	getgroups_args {
	u_int	gidsetsize;	char gidsetsize_[PAD_(u_int)];
	gid_t *	gidset;	char gidset_[PAD_(gid_t *)];
};
struct	setgroups_args {
	u_int	gidsetsize;	char gidsetsize_[PAD_(u_int)];
	gid_t *	gidset;	char gidset_[PAD_(gid_t *)];
};
struct	getpgrp_args {
	register_t dummy;
};
struct	setpgid_args {
	int	pid;	char pid_[PAD_(int)];
	int	pgid;	char pgid_[PAD_(int)];
};
struct	setitimer_args {
	u_int	which;	char which_[PAD_(u_int)];
	struct itimerval *	itv;	char itv_[PAD_(struct itimerval *)];
	struct itimerval *	oitv;	char oitv_[PAD_(struct itimerval *)];
};
struct	swapon_args {
	char *	name;	char name_[PAD_(char *)];
};
struct	getitimer_args {
	u_int	which;	char which_[PAD_(u_int)];
	struct itimerval *	itv;	char itv_[PAD_(struct itimerval *)];
};
struct	getdtablesize_args {
	register_t dummy;
};
struct	dup2_args {
	int	from;	char from_[PAD_(int)];
	int	to;	char to_[PAD_(int)];
};
struct	fcntl_args {
	int	fd;	char fd_[PAD_(int)];
	int	cmd;	char cmd_[PAD_(int)];
	long	arg;	char arg_[PAD_(long)];
};
struct	select_args {
	int	nd;	char nd_[PAD_(int)];
	fd_set *	in;	char in_[PAD_(fd_set *)];
	fd_set *	ou;	char ou_[PAD_(fd_set *)];
	fd_set *	ex;	char ex_[PAD_(fd_set *)];
	struct timeval *	tv;	char tv_[PAD_(struct timeval *)];
};
struct	fsync_args {
	int	fd;	char fd_[PAD_(int)];
};
struct	setpriority_args {
	int	which;	char which_[PAD_(int)];
	int	who;	char who_[PAD_(int)];
	int	prio;	char prio_[PAD_(int)];
};
struct	socket_args {
	int	domain;	char domain_[PAD_(int)];
	int	type;	char type_[PAD_(int)];
	int	protocol;	char protocol_[PAD_(int)];
};
struct	connect_args {
	int	s;	char s_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int	namelen;	char namelen_[PAD_(int)];
};
struct	getpriority_args {
	int	which;	char which_[PAD_(int)];
	int	who;	char who_[PAD_(int)];
};
struct	bind_args {
	int	s;	char s_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int	namelen;	char namelen_[PAD_(int)];
};
struct	setsockopt_args {
	int	s;	char s_[PAD_(int)];
	int	level;	char level_[PAD_(int)];
	int	name;	char name_[PAD_(int)];
	caddr_t	val;	char val_[PAD_(caddr_t)];
	int	valsize;	char valsize_[PAD_(int)];
};
struct	listen_args {
	int	s;	char s_[PAD_(int)];
	int	backlog;	char backlog_[PAD_(int)];
};
struct	gettimeofday_args {
	struct timeval *	tp;	char tp_[PAD_(struct timeval *)];
	struct timezone *	tzp;	char tzp_[PAD_(struct timezone *)];
};
struct	getrusage_args {
	int	who;	char who_[PAD_(int)];
	struct rusage *	rusage;	char rusage_[PAD_(struct rusage *)];
};
struct	getsockopt_args {
	int	s;	char s_[PAD_(int)];
	int	level;	char level_[PAD_(int)];
	int	name;	char name_[PAD_(int)];
	caddr_t	val;	char val_[PAD_(caddr_t)];
	int *	avalsize;	char avalsize_[PAD_(int *)];
};
struct	readv_args {
	int	fd;	char fd_[PAD_(int)];
	struct iovec *	iovp;	char iovp_[PAD_(struct iovec *)];
	u_int	iovcnt;	char iovcnt_[PAD_(u_int)];
};
struct	writev_args {
	int	fd;	char fd_[PAD_(int)];
	struct iovec *	iovp;	char iovp_[PAD_(struct iovec *)];
	u_int	iovcnt;	char iovcnt_[PAD_(u_int)];
};
struct	settimeofday_args {
	struct timeval *	tv;	char tv_[PAD_(struct timeval *)];
	struct timezone *	tzp;	char tzp_[PAD_(struct timezone *)];
};
struct	fchown_args {
	int	fd;	char fd_[PAD_(int)];
	int	uid;	char uid_[PAD_(int)];
	int	gid;	char gid_[PAD_(int)];
};
struct	fchmod_args {
	int	fd;	char fd_[PAD_(int)];
	int	mode;	char mode_[PAD_(int)];
};
struct	setreuid_args {
	int	ruid;	char ruid_[PAD_(int)];
	int	euid;	char euid_[PAD_(int)];
};
struct	setregid_args {
	int	rgid;	char rgid_[PAD_(int)];
	int	egid;	char egid_[PAD_(int)];
};
struct	rename_args {
	char *	from;	char from_[PAD_(char *)];
	char *	to;	char to_[PAD_(char *)];
};
struct	flock_args {
	int	fd;	char fd_[PAD_(int)];
	int	how;	char how_[PAD_(int)];
};
struct	mkfifo_args {
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
};
struct	sendto_args {
	int	s;	char s_[PAD_(int)];
	caddr_t	buf;	char buf_[PAD_(caddr_t)];
	size_t	len;	char len_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	caddr_t	to;	char to_[PAD_(caddr_t)];
	int	tolen;	char tolen_[PAD_(int)];
};
struct	shutdown_args {
	int	s;	char s_[PAD_(int)];
	int	how;	char how_[PAD_(int)];
};
struct	socketpair_args {
	int	domain;	char domain_[PAD_(int)];
	int	type;	char type_[PAD_(int)];
	int	protocol;	char protocol_[PAD_(int)];
	int *	rsv;	char rsv_[PAD_(int *)];
};
struct	mkdir_args {
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
};
struct	rmdir_args {
	char *	path;	char path_[PAD_(char *)];
};
struct	utimes_args {
	char *	path;	char path_[PAD_(char *)];
	struct timeval *	tptr;	char tptr_[PAD_(struct timeval *)];
};
struct	adjtime_args {
	struct timeval *	delta;	char delta_[PAD_(struct timeval *)];
	struct timeval *	olddelta;	char olddelta_[PAD_(struct timeval *)];
};
struct	setsid_args {
	register_t dummy;
};
struct	quotactl_args {
	char *	path;	char path_[PAD_(char *)];
	int	cmd;	char cmd_[PAD_(int)];
	int	uid;	char uid_[PAD_(int)];
	caddr_t	arg;	char arg_[PAD_(caddr_t)];
};
struct	nfssvc_args {
	int	flag;	char flag_[PAD_(int)];
	caddr_t	argp;	char argp_[PAD_(caddr_t)];
};
struct	statfs_args {
	char *	path;	char path_[PAD_(char *)];
	struct statfs *	buf;	char buf_[PAD_(struct statfs *)];
};
struct	fstatfs_args {
	int	fd;	char fd_[PAD_(int)];
	struct statfs *	buf;	char buf_[PAD_(struct statfs *)];
};
struct	getfh_args {
	char *	fname;	char fname_[PAD_(char *)];
	struct fhandle *	fhp;	char fhp_[PAD_(struct fhandle *)];
};
struct	sysarch_args {
	int	op;	char op_[PAD_(int)];
	char *	parms;	char parms_[PAD_(char *)];
};
struct	rtprio_args {
	int	function;	char function_[PAD_(int)];
	pid_t	pid;	char pid_[PAD_(pid_t)];
	struct rtprio *	rtp;	char rtp_[PAD_(struct rtprio *)];
};
struct	extpread_args {
	int	fd;	char fd_[PAD_(int)];
	void *	buf;	char buf_[PAD_(void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	extpwrite_args {
	int	fd;	char fd_[PAD_(int)];
	const void *	buf;	char buf_[PAD_(const void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	ntp_adjtime_args {
	struct timex *	tp;	char tp_[PAD_(struct timex *)];
};
struct	setgid_args {
	gid_t	gid;	char gid_[PAD_(gid_t)];
};
struct	setegid_args {
	gid_t	egid;	char egid_[PAD_(gid_t)];
};
struct	seteuid_args {
	uid_t	euid;	char euid_[PAD_(uid_t)];
};
struct	pathconf_args {
	char *	path;	char path_[PAD_(char *)];
	int	name;	char name_[PAD_(int)];
};
struct	fpathconf_args {
	int	fd;	char fd_[PAD_(int)];
	int	name;	char name_[PAD_(int)];
};
struct	__getrlimit_args {
	u_int	which;	char which_[PAD_(u_int)];
	struct rlimit *	rlp;	char rlp_[PAD_(struct rlimit *)];
};
struct	__setrlimit_args {
	u_int	which;	char which_[PAD_(u_int)];
	struct rlimit *	rlp;	char rlp_[PAD_(struct rlimit *)];
};
struct	mmap_args {
	caddr_t	addr;	char addr_[PAD_(caddr_t)];
	size_t	len;	char len_[PAD_(size_t)];
	int	prot;	char prot_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	int	fd;	char fd_[PAD_(int)];
	int	pad;	char pad_[PAD_(int)];
	off_t	pos;	char pos_[PAD_(off_t)];
};
struct	lseek_args {
	int	fd;	char fd_[PAD_(int)];
	int	pad;	char pad_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
	int	whence;	char whence_[PAD_(int)];
};
struct	truncate_args {
	char *	path;	char path_[PAD_(char *)];
	int	pad;	char pad_[PAD_(int)];
	off_t	length;	char length_[PAD_(off_t)];
};
struct	ftruncate_args {
	int	fd;	char fd_[PAD_(int)];
	int	pad;	char pad_[PAD_(int)];
	off_t	length;	char length_[PAD_(off_t)];
};
struct	sysctl_args {
	int *	name;	char name_[PAD_(int *)];
	u_int	namelen;	char namelen_[PAD_(u_int)];
	void *	old;	char old_[PAD_(void *)];
	size_t *	oldlenp;	char oldlenp_[PAD_(size_t *)];
	void *	new;	char new_[PAD_(void *)];
	size_t	newlen;	char newlen_[PAD_(size_t)];
};
struct	mlock_args {
	const void *	addr;	char addr_[PAD_(const void *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	munlock_args {
	const void *	addr;	char addr_[PAD_(const void *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	undelete_args {
	char *	path;	char path_[PAD_(char *)];
};
struct	futimes_args {
	int	fd;	char fd_[PAD_(int)];
	struct timeval *	tptr;	char tptr_[PAD_(struct timeval *)];
};
struct	getpgid_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
};
struct	poll_args {
	struct pollfd *	fds;	char fds_[PAD_(struct pollfd *)];
	u_int	nfds;	char nfds_[PAD_(u_int)];
	int	timeout;	char timeout_[PAD_(int)];
};
struct	__semctl_args {
	int	semid;	char semid_[PAD_(int)];
	int	semnum;	char semnum_[PAD_(int)];
	int	cmd;	char cmd_[PAD_(int)];
	union semun *	arg;	char arg_[PAD_(union semun *)];
};
struct	semget_args {
	key_t	key;	char key_[PAD_(key_t)];
	int	nsems;	char nsems_[PAD_(int)];
	int	semflg;	char semflg_[PAD_(int)];
};
struct	semop_args {
	int	semid;	char semid_[PAD_(int)];
	struct sembuf *	sops;	char sops_[PAD_(struct sembuf *)];
	u_int	nsops;	char nsops_[PAD_(u_int)];
};
struct	msgctl_args {
	int	msqid;	char msqid_[PAD_(int)];
	int	cmd;	char cmd_[PAD_(int)];
	struct msqid_ds *	buf;	char buf_[PAD_(struct msqid_ds *)];
};
struct	msgget_args {
	key_t	key;	char key_[PAD_(key_t)];
	int	msgflg;	char msgflg_[PAD_(int)];
};
struct	msgsnd_args {
	int	msqid;	char msqid_[PAD_(int)];
	const void *	msgp;	char msgp_[PAD_(const void *)];
	size_t	msgsz;	char msgsz_[PAD_(size_t)];
	int	msgflg;	char msgflg_[PAD_(int)];
};
struct	msgrcv_args {
	int	msqid;	char msqid_[PAD_(int)];
	void *	msgp;	char msgp_[PAD_(void *)];
	size_t	msgsz;	char msgsz_[PAD_(size_t)];
	long	msgtyp;	char msgtyp_[PAD_(long)];
	int	msgflg;	char msgflg_[PAD_(int)];
};
struct	shmat_args {
	int	shmid;	char shmid_[PAD_(int)];
	const void *	shmaddr;	char shmaddr_[PAD_(const void *)];
	int	shmflg;	char shmflg_[PAD_(int)];
};
struct	shmctl_args {
	int	shmid;	char shmid_[PAD_(int)];
	int	cmd;	char cmd_[PAD_(int)];
	struct shmid_ds *	buf;	char buf_[PAD_(struct shmid_ds *)];
};
struct	shmdt_args {
	const void *	shmaddr;	char shmaddr_[PAD_(const void *)];
};
struct	shmget_args {
	key_t	key;	char key_[PAD_(key_t)];
	size_t	size;	char size_[PAD_(size_t)];
	int	shmflg;	char shmflg_[PAD_(int)];
};
struct	clock_gettime_args {
	clockid_t	clock_id;	char clock_id_[PAD_(clockid_t)];
	struct timespec *	tp;	char tp_[PAD_(struct timespec *)];
};
struct	clock_settime_args {
	clockid_t	clock_id;	char clock_id_[PAD_(clockid_t)];
	const struct timespec *	tp;	char tp_[PAD_(const struct timespec *)];
};
struct	clock_getres_args {
	clockid_t	clock_id;	char clock_id_[PAD_(clockid_t)];
	struct timespec *	tp;	char tp_[PAD_(struct timespec *)];
};
struct	nanosleep_args {
	const struct timespec *	rqtp;	char rqtp_[PAD_(const struct timespec *)];
	struct timespec *	rmtp;	char rmtp_[PAD_(struct timespec *)];
};
struct	minherit_args {
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	inherit;	char inherit_[PAD_(int)];
};
struct	rfork_args {
	int	flags;	char flags_[PAD_(int)];
};
struct	openbsd_poll_args {
	struct pollfd *	fds;	char fds_[PAD_(struct pollfd *)];
	u_int	nfds;	char nfds_[PAD_(u_int)];
	int	timeout;	char timeout_[PAD_(int)];
};
struct	issetugid_args {
	register_t dummy;
};
struct	lchown_args {
	char *	path;	char path_[PAD_(char *)];
	int	uid;	char uid_[PAD_(int)];
	int	gid;	char gid_[PAD_(int)];
};
struct	lchmod_args {
	char *	path;	char path_[PAD_(char *)];
	mode_t	mode;	char mode_[PAD_(mode_t)];
};
struct	lutimes_args {
	char *	path;	char path_[PAD_(char *)];
	struct timeval *	tptr;	char tptr_[PAD_(struct timeval *)];
};
struct	extpreadv_args {
	int	fd;	char fd_[PAD_(int)];
	const struct iovec *	iovp;	char iovp_[PAD_(const struct iovec *)];
	int	iovcnt;	char iovcnt_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	extpwritev_args {
	int	fd;	char fd_[PAD_(int)];
	const struct iovec *	iovp;	char iovp_[PAD_(const struct iovec *)];
	int	iovcnt;	char iovcnt_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	fhstatfs_args {
	const struct fhandle *	u_fhp;	char u_fhp_[PAD_(const struct fhandle *)];
	struct statfs *	buf;	char buf_[PAD_(struct statfs *)];
};
struct	fhopen_args {
	const struct fhandle *	u_fhp;	char u_fhp_[PAD_(const struct fhandle *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	modnext_args {
	int	modid;	char modid_[PAD_(int)];
};
struct	modstat_args {
	int	modid;	char modid_[PAD_(int)];
	struct module_stat *	stat;	char stat_[PAD_(struct module_stat *)];
};
struct	modfnext_args {
	int	modid;	char modid_[PAD_(int)];
};
struct	modfind_args {
	const char *	name;	char name_[PAD_(const char *)];
};
struct	kldload_args {
	const char *	file;	char file_[PAD_(const char *)];
};
struct	kldunload_args {
	int	fileid;	char fileid_[PAD_(int)];
};
struct	kldfind_args {
	const char *	file;	char file_[PAD_(const char *)];
};
struct	kldnext_args {
	int	fileid;	char fileid_[PAD_(int)];
};
struct	kldstat_args {
	int	fileid;	char fileid_[PAD_(int)];
	struct kld_file_stat *	stat;	char stat_[PAD_(struct kld_file_stat *)];
};
struct	kldfirstmod_args {
	int	fileid;	char fileid_[PAD_(int)];
};
struct	getsid_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
};
struct	setresuid_args {
	uid_t	ruid;	char ruid_[PAD_(uid_t)];
	uid_t	euid;	char euid_[PAD_(uid_t)];
	uid_t	suid;	char suid_[PAD_(uid_t)];
};
struct	setresgid_args {
	gid_t	rgid;	char rgid_[PAD_(gid_t)];
	gid_t	egid;	char egid_[PAD_(gid_t)];
	gid_t	sgid;	char sgid_[PAD_(gid_t)];
};
struct	aio_return_args {
	struct aiocb *	aiocbp;	char aiocbp_[PAD_(struct aiocb *)];
};
struct	aio_suspend_args {
	struct aiocb *const *	aiocbp;	char aiocbp_[PAD_(struct aiocb *const *)];
	int	nent;	char nent_[PAD_(int)];
	const struct timespec *	timeout;	char timeout_[PAD_(const struct timespec *)];
};
struct	aio_cancel_args {
	int	fd;	char fd_[PAD_(int)];
	struct aiocb *	aiocbp;	char aiocbp_[PAD_(struct aiocb *)];
};
struct	aio_error_args {
	struct aiocb *	aiocbp;	char aiocbp_[PAD_(struct aiocb *)];
};
struct	aio_read_args {
	struct aiocb *	aiocbp;	char aiocbp_[PAD_(struct aiocb *)];
};
struct	aio_write_args {
	struct aiocb *	aiocbp;	char aiocbp_[PAD_(struct aiocb *)];
};
struct	lio_listio_args {
	int	mode;	char mode_[PAD_(int)];
	struct aiocb *const *	acb_list;	char acb_list_[PAD_(struct aiocb *const *)];
	int	nent;	char nent_[PAD_(int)];
	struct sigevent *	sig;	char sig_[PAD_(struct sigevent *)];
};
struct	yield_args {
	register_t dummy;
};
struct	mlockall_args {
	int	how;	char how_[PAD_(int)];
};
struct	munlockall_args {
	register_t dummy;
};
struct	__getcwd_args {
	u_char *	buf;	char buf_[PAD_(u_char *)];
	u_int	buflen;	char buflen_[PAD_(u_int)];
};
struct	sched_setparam_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
	const struct sched_param *	param;	char param_[PAD_(const struct sched_param *)];
};
struct	sched_getparam_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
	struct sched_param *	param;	char param_[PAD_(struct sched_param *)];
};
struct	sched_setscheduler_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
	int	policy;	char policy_[PAD_(int)];
	const struct sched_param *	param;	char param_[PAD_(const struct sched_param *)];
};
struct	sched_getscheduler_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
};
struct	sched_yield_args {
	register_t dummy;
};
struct	sched_get_priority_max_args {
	int	policy;	char policy_[PAD_(int)];
};
struct	sched_get_priority_min_args {
	int	policy;	char policy_[PAD_(int)];
};
struct	sched_rr_get_interval_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
	struct timespec *	interval;	char interval_[PAD_(struct timespec *)];
};
struct	utrace_args {
	const void *	addr;	char addr_[PAD_(const void *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	kldsym_args {
	int	fileid;	char fileid_[PAD_(int)];
	int	cmd;	char cmd_[PAD_(int)];
	void *	data;	char data_[PAD_(void *)];
};
struct	jail_args {
	struct jail *	jail;	char jail_[PAD_(struct jail *)];
};
struct	sigprocmask_args {
	int	how;	char how_[PAD_(int)];
	const sigset_t *	set;	char set_[PAD_(const sigset_t *)];
	sigset_t *	oset;	char oset_[PAD_(sigset_t *)];
};
struct	sigsuspend_args {
	const sigset_t *	sigmask;	char sigmask_[PAD_(const sigset_t *)];
};
struct	sigaction_args {
	int	sig;	char sig_[PAD_(int)];
	const struct sigaction *	act;	char act_[PAD_(const struct sigaction *)];
	struct sigaction *	oact;	char oact_[PAD_(struct sigaction *)];
};
struct	sigpending_args {
	sigset_t *	set;	char set_[PAD_(sigset_t *)];
};
struct	sigreturn_args {
	ucontext_t *	sigcntxp;	char sigcntxp_[PAD_(ucontext_t *)];
};
struct	sigtimedwait_args {
	const sigset_t *	set;	char set_[PAD_(const sigset_t *)];
	siginfo_t *	info;	char info_[PAD_(siginfo_t *)];
	const struct timespec *	timeout;	char timeout_[PAD_(const struct timespec *)];
};
struct	sigwaitinfo_args {
	const sigset_t *	set;	char set_[PAD_(const sigset_t *)];
	siginfo_t *	info;	char info_[PAD_(siginfo_t *)];
};
struct	__acl_get_file_args {
	const char *	path;	char path_[PAD_(const char *)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	__acl_set_file_args {
	const char *	path;	char path_[PAD_(const char *)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	__acl_get_fd_args {
	int	filedes;	char filedes_[PAD_(int)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	__acl_set_fd_args {
	int	filedes;	char filedes_[PAD_(int)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	__acl_delete_file_args {
	const char *	path;	char path_[PAD_(const char *)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
};
struct	__acl_delete_fd_args {
	int	filedes;	char filedes_[PAD_(int)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
};
struct	__acl_aclcheck_file_args {
	const char *	path;	char path_[PAD_(const char *)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	__acl_aclcheck_fd_args {
	int	filedes;	char filedes_[PAD_(int)];
	acl_type_t	type;	char type_[PAD_(acl_type_t)];
	struct acl *	aclp;	char aclp_[PAD_(struct acl *)];
};
struct	extattrctl_args {
	const char *	path;	char path_[PAD_(const char *)];
	int	cmd;	char cmd_[PAD_(int)];
	const char *	filename;	char filename_[PAD_(const char *)];
	int	attrnamespace;	char attrnamespace_[PAD_(int)];
	const char *	attrname;	char attrname_[PAD_(const char *)];
};
struct	extattr_set_file_args {
	const char *	path;	char path_[PAD_(const char *)];
	int	attrnamespace;	char attrnamespace_[PAD_(int)];
	const char *	attrname;	char attrname_[PAD_(const char *)];
	void *	data;	char data_[PAD_(void *)];
	size_t	nbytes;	char nbytes_[PAD_(size_t)];
};
struct	extattr_get_file_args {
	const char *	path;	char path_[PAD_(const char *)];
	int	attrnamespace;	char attrnamespace_[PAD_(int)];
	const char *	attrname;	char attrname_[PAD_(const char *)];
	void *	data;	char data_[PAD_(void *)];
	size_t	nbytes;	char nbytes_[PAD_(size_t)];
};
struct	extattr_delete_file_args {
	const char *	path;	char path_[PAD_(const char *)];
	int	attrnamespace;	char attrnamespace_[PAD_(int)];
	const char *	attrname;	char attrname_[PAD_(const char *)];
};
struct	aio_waitcomplete_args {
	struct aiocb **	aiocbp;	char aiocbp_[PAD_(struct aiocb **)];
	struct timespec *	timeout;	char timeout_[PAD_(struct timespec *)];
};
struct	getresuid_args {
	uid_t *	ruid;	char ruid_[PAD_(uid_t *)];
	uid_t *	euid;	char euid_[PAD_(uid_t *)];
	uid_t *	suid;	char suid_[PAD_(uid_t *)];
};
struct	getresgid_args {
	gid_t *	rgid;	char rgid_[PAD_(gid_t *)];
	gid_t *	egid;	char egid_[PAD_(gid_t *)];
	gid_t *	sgid;	char sgid_[PAD_(gid_t *)];
};
struct	kqueue_args {
	register_t dummy;
};
struct	kevent_args {
	int	fd;	char fd_[PAD_(int)];
	const struct kevent *	changelist;	char changelist_[PAD_(const struct kevent *)];
	int	nchanges;	char nchanges_[PAD_(int)];
	struct kevent *	eventlist;	char eventlist_[PAD_(struct kevent *)];
	int	nevents;	char nevents_[PAD_(int)];
	const struct timespec *	timeout;	char timeout_[PAD_(const struct timespec *)];
};
struct	kenv_args {
	int	what;	char what_[PAD_(int)];
	const char *	name;	char name_[PAD_(const char *)];
	char *	value;	char value_[PAD_(char *)];
	int	len;	char len_[PAD_(int)];
};
struct	lchflags_args {
	const char *	path;	char path_[PAD_(const char *)];
	u_long	flags;	char flags_[PAD_(u_long)];
};
struct	uuidgen_args {
	struct uuid *	store;	char store_[PAD_(struct uuid *)];
	int	count;	char count_[PAD_(int)];
};
struct	sendfile_args {
	int	fd;	char fd_[PAD_(int)];
	int	s;	char s_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
	size_t	nbytes;	char nbytes_[PAD_(size_t)];
	struct sf_hdtr *	hdtr;	char hdtr_[PAD_(struct sf_hdtr *)];
	off_t *	sbytes;	char sbytes_[PAD_(off_t *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	varsym_set_args {
	int	level;	char level_[PAD_(int)];
	const char *	name;	char name_[PAD_(const char *)];
	const char *	data;	char data_[PAD_(const char *)];
};
struct	varsym_get_args {
	int	mask;	char mask_[PAD_(int)];
	const char *	wild;	char wild_[PAD_(const char *)];
	char *	buf;	char buf_[PAD_(char *)];
	int	bufsize;	char bufsize_[PAD_(int)];
};
struct	varsym_list_args {
	int	level;	char level_[PAD_(int)];
	char *	buf;	char buf_[PAD_(char *)];
	int	maxsize;	char maxsize_[PAD_(int)];
	int *	marker;	char marker_[PAD_(int *)];
};
struct	exec_sys_register_args {
	void *	entry;	char entry_[PAD_(void *)];
};
struct	exec_sys_unregister_args {
	int	id;	char id_[PAD_(int)];
};
struct	sys_checkpoint_args {
	int	type;	char type_[PAD_(int)];
	int	fd;	char fd_[PAD_(int)];
	pid_t	pid;	char pid_[PAD_(pid_t)];
	int	retval;	char retval_[PAD_(int)];
};
struct	mountctl_args {
	const char *	path;	char path_[PAD_(const char *)];
	int	op;	char op_[PAD_(int)];
	int	fd;	char fd_[PAD_(int)];
	const void *	ctl;	char ctl_[PAD_(const void *)];
	int	ctllen;	char ctllen_[PAD_(int)];
	void *	buf;	char buf_[PAD_(void *)];
	int	buflen;	char buflen_[PAD_(int)];
};
struct	umtx_sleep_args {
	volatile const int *	ptr;	char ptr_[PAD_(volatile const int *)];
	int	value;	char value_[PAD_(int)];
	int	timeout;	char timeout_[PAD_(int)];
};
struct	umtx_wakeup_args {
	volatile const int *	ptr;	char ptr_[PAD_(volatile const int *)];
	int	count;	char count_[PAD_(int)];
};
struct	jail_attach_args {
	int	jid;	char jid_[PAD_(int)];
};
struct	set_tls_area_args {
	int	which;	char which_[PAD_(int)];
	struct tls_info *	info;	char info_[PAD_(struct tls_info *)];
	size_t	infosize;	char infosize_[PAD_(size_t)];
};
struct	get_tls_area_args {
	int	which;	char which_[PAD_(int)];
	struct tls_info *	info;	char info_[PAD_(struct tls_info *)];
	size_t	infosize;	char infosize_[PAD_(size_t)];
};
struct	closefrom_args {
	int	fd;	char fd_[PAD_(int)];
};
struct	stat_args {
	const char *	path;	char path_[PAD_(const char *)];
	struct stat *	ub;	char ub_[PAD_(struct stat *)];
};
struct	fstat_args {
	int	fd;	char fd_[PAD_(int)];
	struct stat *	sb;	char sb_[PAD_(struct stat *)];
};
struct	lstat_args {
	const char *	path;	char path_[PAD_(const char *)];
	struct stat *	ub;	char ub_[PAD_(struct stat *)];
};
struct	fhstat_args {
	const struct fhandle *	u_fhp;	char u_fhp_[PAD_(const struct fhandle *)];
	struct stat *	sb;	char sb_[PAD_(struct stat *)];
};
struct	getdirentries_args {
	int	fd;	char fd_[PAD_(int)];
	char *	buf;	char buf_[PAD_(char *)];
	u_int	count;	char count_[PAD_(u_int)];
	long *	basep;	char basep_[PAD_(long *)];
};
struct	getdents_args {
	int	fd;	char fd_[PAD_(int)];
	char *	buf;	char buf_[PAD_(char *)];
	size_t	count;	char count_[PAD_(size_t)];
};
struct	usched_set_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
	int	cmd;	char cmd_[PAD_(int)];
	void *	data;	char data_[PAD_(void *)];
	int	bytes;	char bytes_[PAD_(int)];
};
struct	extaccept_args {
	int	s;	char s_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int *	anamelen;	char anamelen_[PAD_(int *)];
};
struct	extconnect_args {
	int	s;	char s_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int	namelen;	char namelen_[PAD_(int)];
};
struct	mcontrol_args {
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	behav;	char behav_[PAD_(int)];
	off_t	value;	char value_[PAD_(off_t)];
};
struct	vmspace_create_args {
	void *	id;	char id_[PAD_(void *)];
	int	type;	char type_[PAD_(int)];
	void *	data;	char data_[PAD_(void *)];
};
struct	vmspace_destroy_args {
	void *	id;	char id_[PAD_(void *)];
};
struct	vmspace_ctl_args {
	void *	id;	char id_[PAD_(void *)];
	int	cmd;	char cmd_[PAD_(int)];
	struct trapframe *	tframe;	char tframe_[PAD_(struct trapframe *)];
	struct vextframe *	vframe;	char vframe_[PAD_(struct vextframe *)];
};
struct	vmspace_mmap_args {
	void *	id;	char id_[PAD_(void *)];
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	prot;	char prot_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
	int	fd;	char fd_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	vmspace_munmap_args {
	void *	id;	char id_[PAD_(void *)];
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	vmspace_mcontrol_args {
	void *	id;	char id_[PAD_(void *)];
	void *	addr;	char addr_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	int	behav;	char behav_[PAD_(int)];
	off_t	value;	char value_[PAD_(off_t)];
};
struct	vmspace_pread_args {
	void *	id;	char id_[PAD_(void *)];
	void *	buf;	char buf_[PAD_(void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	vmspace_pwrite_args {
	void *	id;	char id_[PAD_(void *)];
	const void *	buf;	char buf_[PAD_(const void *)];
	size_t	nbyte;	char nbyte_[PAD_(size_t)];
	int	flags;	char flags_[PAD_(int)];
	off_t	offset;	char offset_[PAD_(off_t)];
};
struct	extexit_args {
	int	how;	char how_[PAD_(int)];
	int	status;	char status_[PAD_(int)];
	void *	addr;	char addr_[PAD_(void *)];
};
struct	lwp_create_args {
	struct lwp_params *	params;	char params_[PAD_(struct lwp_params *)];
};
struct	lwp_gettid_args {
	register_t dummy;
};
struct	lwp_kill_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
	lwpid_t	tid;	char tid_[PAD_(lwpid_t)];
	int	signum;	char signum_[PAD_(int)];
};
struct	lwp_rtprio_args {
	int	function;	char function_[PAD_(int)];
	pid_t	pid;	char pid_[PAD_(pid_t)];
	lwpid_t	tid;	char tid_[PAD_(lwpid_t)];
	struct rtprio *	rtp;	char rtp_[PAD_(struct rtprio *)];
};
struct	pselect_args {
	int	nd;	char nd_[PAD_(int)];
	fd_set *	in;	char in_[PAD_(fd_set *)];
	fd_set *	ou;	char ou_[PAD_(fd_set *)];
	fd_set *	ex;	char ex_[PAD_(fd_set *)];
	const struct timespec *	ts;	char ts_[PAD_(const struct timespec *)];
	const sigset_t *	sigmask;	char sigmask_[PAD_(const sigset_t *)];
};
struct	statvfs_args {
	const char *	path;	char path_[PAD_(const char *)];
	struct statvfs *	buf;	char buf_[PAD_(struct statvfs *)];
};
struct	fstatvfs_args {
	int	fd;	char fd_[PAD_(int)];
	struct statvfs *	buf;	char buf_[PAD_(struct statvfs *)];
};
struct	fhstatvfs_args {
	const struct fhandle *	u_fhp;	char u_fhp_[PAD_(const struct fhandle *)];
	struct statvfs *	buf;	char buf_[PAD_(struct statvfs *)];
};
struct	getvfsstat_args {
	struct statfs *	buf;	char buf_[PAD_(struct statfs *)];
	struct statvfs *	vbuf;	char vbuf_[PAD_(struct statvfs *)];
	long	vbufsize;	char vbufsize_[PAD_(long)];
	int	flags;	char flags_[PAD_(int)];
};
struct	openat_args {
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
	int	mode;	char mode_[PAD_(int)];
};
struct	fstatat_args {
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	struct stat *	sb;	char sb_[PAD_(struct stat *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	fchmodat_args {
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	int	mode;	char mode_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
};
struct	fchownat_args {
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	int	uid;	char uid_[PAD_(int)];
	int	gid;	char gid_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
};
struct	unlinkat_args {
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	faccessat_args {
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	int	amode;	char amode_[PAD_(int)];
	int	flags;	char flags_[PAD_(int)];
};
struct	mq_open_args {
	const char *	name;	char name_[PAD_(const char *)];
	int	oflag;	char oflag_[PAD_(int)];
	mode_t	mode;	char mode_[PAD_(mode_t)];
	struct mq_attr *	attr;	char attr_[PAD_(struct mq_attr *)];
};
struct	mq_close_args {
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
};
struct	mq_unlink_args {
	const char *	name;	char name_[PAD_(const char *)];
};
struct	mq_getattr_args {
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	struct mq_attr *	mqstat;	char mqstat_[PAD_(struct mq_attr *)];
};
struct	mq_setattr_args {
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	const struct mq_attr *	mqstat;	char mqstat_[PAD_(const struct mq_attr *)];
	struct mq_attr *	omqstat;	char omqstat_[PAD_(struct mq_attr *)];
};
struct	mq_notify_args {
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	const struct sigevent *	notification;	char notification_[PAD_(const struct sigevent *)];
};
struct	mq_send_args {
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	const char *	msg_ptr;	char msg_ptr_[PAD_(const char *)];
	size_t	msg_len;	char msg_len_[PAD_(size_t)];
	unsigned	msg_prio;	char msg_prio_[PAD_(unsigned)];
};
struct	mq_receive_args {
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	char *	msg_ptr;	char msg_ptr_[PAD_(char *)];
	size_t	msg_len;	char msg_len_[PAD_(size_t)];
	unsigned *	msg_prio;	char msg_prio_[PAD_(unsigned *)];
};
struct	mq_timedsend_args {
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	const char *	msg_ptr;	char msg_ptr_[PAD_(const char *)];
	size_t	msg_len;	char msg_len_[PAD_(size_t)];
	unsigned	msg_prio;	char msg_prio_[PAD_(unsigned)];
	const struct timespec *	abs_timeout;	char abs_timeout_[PAD_(const struct timespec *)];
};
struct	mq_timedreceive_args {
	mqd_t	mqdes;	char mqdes_[PAD_(mqd_t)];
	char *	msg_ptr;	char msg_ptr_[PAD_(char *)];
	size_t	msg_len;	char msg_len_[PAD_(size_t)];
	unsigned *	msg_prio;	char msg_prio_[PAD_(unsigned *)];
	const struct timespec *	abs_timeout;	char abs_timeout_[PAD_(const struct timespec *)];
};
struct	ioprio_set_args {
	int	which;	char which_[PAD_(int)];
	int	who;	char who_[PAD_(int)];
	int	prio;	char prio_[PAD_(int)];
};
struct	ioprio_get_args {
	int	which;	char which_[PAD_(int)];
	int	who;	char who_[PAD_(int)];
};
struct	chroot_kernel_args {
	char *	path;	char path_[PAD_(char *)];
};
struct	renameat_args {
	int	oldfd;	char oldfd_[PAD_(int)];
	char *	old;	char old_[PAD_(char *)];
	int	newfd;	char newfd_[PAD_(int)];
	char *	new;	char new_[PAD_(char *)];
};
struct	mkdirat_args {
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	mode_t	mode;	char mode_[PAD_(mode_t)];
};
struct	mkfifoat_args {
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	mode_t	mode;	char mode_[PAD_(mode_t)];
};
struct	mknodat_args {
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	mode_t	mode;	char mode_[PAD_(mode_t)];
	dev_t	dev;	char dev_[PAD_(dev_t)];
};
struct	readlinkat_args {
	int	fd;	char fd_[PAD_(int)];
	char *	path;	char path_[PAD_(char *)];
	char *	buf;	char buf_[PAD_(char *)];
	size_t	bufsize;	char bufsize_[PAD_(size_t)];
};
struct	symlinkat_args {
	char *	path1;	char path1_[PAD_(char *)];
	int	fd;	char fd_[PAD_(int)];
	char *	path2;	char path2_[PAD_(char *)];
};
struct	swapoff_args {
	char *	name;	char name_[PAD_(char *)];
};
struct	vquotactl_args {
	const char *	path;	char path_[PAD_(const char *)];
	struct plistref *	pref;	char pref_[PAD_(struct plistref *)];
};
struct	linkat_args {
	int	fd1;	char fd1_[PAD_(int)];
	char *	path1;	char path1_[PAD_(char *)];
	int	fd2;	char fd2_[PAD_(int)];
	char *	path2;	char path2_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	eaccess_args {
	char *	path;	char path_[PAD_(char *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	lpathconf_args {
	char *	path;	char path_[PAD_(char *)];
	int	name;	char name_[PAD_(int)];
};
struct	vmm_guest_ctl_args {
	int	op;	char op_[PAD_(int)];
	struct vmm_guest_options *	options;	char options_[PAD_(struct vmm_guest_options *)];
};
struct	vmm_guest_sync_addr_args {
	long *	dstaddr;	char dstaddr_[PAD_(long *)];
	long *	srcaddr;	char srcaddr_[PAD_(long *)];
};
struct	procctl_args {
	idtype_t	idtype;	char idtype_[PAD_(idtype_t)];
	id_t	id;	char id_[PAD_(id_t)];
	int	cmd;	char cmd_[PAD_(int)];
	void *	data;	char data_[PAD_(void *)];
};
struct	chflagsat_args {
	int	fd;	char fd_[PAD_(int)];
	const char *	path;	char path_[PAD_(const char *)];
	u_long	flags;	char flags_[PAD_(u_long)];
	int	atflags;	char atflags_[PAD_(int)];
};
struct	pipe2_args {
	int *	fildes;	char fildes_[PAD_(int *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	utimensat_args {
	int	fd;	char fd_[PAD_(int)];
	const char *	path;	char path_[PAD_(const char *)];
	const struct timespec *	ts;	char ts_[PAD_(const struct timespec *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	futimens_args {
	int	fd;	char fd_[PAD_(int)];
	const struct timespec *	ts;	char ts_[PAD_(const struct timespec *)];
};
struct	accept4_args {
	int	s;	char s_[PAD_(int)];
	caddr_t	name;	char name_[PAD_(caddr_t)];
	int *	anamelen;	char anamelen_[PAD_(int *)];
	int	flags;	char flags_[PAD_(int)];
};
struct	lwp_setname_args {
	lwpid_t	tid;	char tid_[PAD_(lwpid_t)];
	const char *	name;	char name_[PAD_(const char *)];
};
struct	ppoll_args {
	struct pollfd *	fds;	char fds_[PAD_(struct pollfd *)];
	u_int	nfds;	char nfds_[PAD_(u_int)];
	const struct timespec *	ts;	char ts_[PAD_(const struct timespec *)];
	const sigset_t *	sigmask;	char sigmask_[PAD_(const sigset_t *)];
};
struct	lwp_setaffinity_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
	lwpid_t	tid;	char tid_[PAD_(lwpid_t)];
	const cpumask_t *	mask;	char mask_[PAD_(const cpumask_t *)];
};
struct	lwp_getaffinity_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
	lwpid_t	tid;	char tid_[PAD_(lwpid_t)];
	cpumask_t *	mask;	char mask_[PAD_(cpumask_t *)];
};
struct	lwp_create2_args {
	struct lwp_params *	params;	char params_[PAD_(struct lwp_params *)];
	const cpumask_t *	mask;	char mask_[PAD_(const cpumask_t *)];
};
struct	getcpuclockid_args {
	pid_t	pid;	char pid_[PAD_(pid_t)];
	lwpid_t	lwp_id;	char lwp_id_[PAD_(lwpid_t)];
	clockid_t *	clock_id;	char clock_id_[PAD_(clockid_t *)];
};
struct	wait6_args {
	idtype_t	idtype;	char idtype_[PAD_(idtype_t)];
	id_t	id;	char id_[PAD_(id_t)];
	int *	status;	char status_[PAD_(int *)];
	int	options;	char options_[PAD_(int)];
	struct __wrusage *	wrusage;	char wrusage_[PAD_(struct __wrusage *)];
	siginfo_t *	info;	char info_[PAD_(siginfo_t *)];
};
struct	lwp_getname_args {
	lwpid_t	tid;	char tid_[PAD_(lwpid_t)];
	char *	name;	char name_[PAD_(char *)];
	size_t	len;	char len_[PAD_(size_t)];
};
struct	getrandom_args {
	void *	buf;	char buf_[PAD_(void *)];
	size_t	len;	char len_[PAD_(size_t)];
	unsigned	flags;	char flags_[PAD_(unsigned)];
};
struct	__realpath_args {
	const char *	path;	char path_[PAD_(const char *)];
	char *	buf;	char buf_[PAD_(char *)];
	size_t	len;	char len_[PAD_(size_t)];
};

#ifdef _KERNEL

struct sysmsg;


#endif /* _KERNEL */

#ifdef _KERNEL

struct sysmsg;

int	sys_xsyscall (struct sysmsg *sysmsg, const struct nosys_args *);
int	sys_exit (struct sysmsg *sysmsg, const struct exit_args *);
int	sys_fork (struct sysmsg *sysmsg, const struct fork_args *);
int	sys_read (struct sysmsg *sysmsg, const struct read_args *);
int	sys_write (struct sysmsg *sysmsg, const struct write_args *);
int	sys_open (struct sysmsg *sysmsg, const struct open_args *);
int	sys_close (struct sysmsg *sysmsg, const struct close_args *);
int	sys_wait4 (struct sysmsg *sysmsg, const struct wait_args *);
int	sys_nosys (struct sysmsg *sysmsg, const struct nosys_args *);
int	sys_link (struct sysmsg *sysmsg, const struct link_args *);
int	sys_unlink (struct sysmsg *sysmsg, const struct unlink_args *);
int	sys_chdir (struct sysmsg *sysmsg, const struct chdir_args *);
int	sys_fchdir (struct sysmsg *sysmsg, const struct fchdir_args *);
int	sys_mknod (struct sysmsg *sysmsg, const struct mknod_args *);
int	sys_chmod (struct sysmsg *sysmsg, const struct chmod_args *);
int	sys_chown (struct sysmsg *sysmsg, const struct chown_args *);
int	sys_obreak (struct sysmsg *sysmsg, const struct obreak_args *);
int	sys_getfsstat (struct sysmsg *sysmsg, const struct getfsstat_args *);
int	sys_getpid (struct sysmsg *sysmsg, const struct getpid_args *);
int	sys_mount (struct sysmsg *sysmsg, const struct mount_args *);
int	sys_unmount (struct sysmsg *sysmsg, const struct unmount_args *);
int	sys_setuid (struct sysmsg *sysmsg, const struct setuid_args *);
int	sys_getuid (struct sysmsg *sysmsg, const struct getuid_args *);
int	sys_geteuid (struct sysmsg *sysmsg, const struct geteuid_args *);
int	sys_ptrace (struct sysmsg *sysmsg, const struct ptrace_args *);
int	sys_recvmsg (struct sysmsg *sysmsg, const struct recvmsg_args *);
int	sys_sendmsg (struct sysmsg *sysmsg, const struct sendmsg_args *);
int	sys_recvfrom (struct sysmsg *sysmsg, const struct recvfrom_args *);
int	sys_accept (struct sysmsg *sysmsg, const struct accept_args *);
int	sys_getpeername (struct sysmsg *sysmsg, const struct getpeername_args *);
int	sys_getsockname (struct sysmsg *sysmsg, const struct getsockname_args *);
int	sys_access (struct sysmsg *sysmsg, const struct access_args *);
int	sys_chflags (struct sysmsg *sysmsg, const struct chflags_args *);
int	sys_fchflags (struct sysmsg *sysmsg, const struct fchflags_args *);
int	sys_sync (struct sysmsg *sysmsg, const struct sync_args *);
int	sys_kill (struct sysmsg *sysmsg, const struct kill_args *);
int	sys_getppid (struct sysmsg *sysmsg, const struct getppid_args *);
int	sys_dup (struct sysmsg *sysmsg, const struct dup_args *);
int	sys_pipe (struct sysmsg *sysmsg, const struct pipe_args *);
int	sys_getegid (struct sysmsg *sysmsg, const struct getegid_args *);
int	sys_profil (struct sysmsg *sysmsg, const struct profil_args *);
int	sys_ktrace (struct sysmsg *sysmsg, const struct ktrace_args *);
int	sys_getgid (struct sysmsg *sysmsg, const struct getgid_args *);
int	sys_getlogin (struct sysmsg *sysmsg, const struct getlogin_args *);
int	sys_setlogin (struct sysmsg *sysmsg, const struct setlogin_args *);
int	sys_acct (struct sysmsg *sysmsg, const struct acct_args *);
int	sys_sigaltstack (struct sysmsg *sysmsg, const struct sigaltstack_args *);
int	sys_ioctl (struct sysmsg *sysmsg, const struct ioctl_args *);
int	sys_reboot (struct sysmsg *sysmsg, const struct reboot_args *);
int	sys_revoke (struct sysmsg *sysmsg, const struct revoke_args *);
int	sys_symlink (struct sysmsg *sysmsg, const struct symlink_args *);
int	sys_readlink (struct sysmsg *sysmsg, const struct readlink_args *);
int	sys_execve (struct sysmsg *sysmsg, const struct execve_args *);
int	sys_umask (struct sysmsg *sysmsg, const struct umask_args *);
int	sys_chroot (struct sysmsg *sysmsg, const struct chroot_args *);
int	sys_msync (struct sysmsg *sysmsg, const struct msync_args *);
int	sys_vfork (struct sysmsg *sysmsg, const struct vfork_args *);
int	sys_sbrk (struct sysmsg *sysmsg, const struct sbrk_args *);
int	sys_sstk (struct sysmsg *sysmsg, const struct sstk_args *);
int	sys_munmap (struct sysmsg *sysmsg, const struct munmap_args *);
int	sys_mprotect (struct sysmsg *sysmsg, const struct mprotect_args *);
int	sys_madvise (struct sysmsg *sysmsg, const struct madvise_args *);
int	sys_mincore (struct sysmsg *sysmsg, const struct mincore_args *);
int	sys_getgroups (struct sysmsg *sysmsg, const struct getgroups_args *);
int	sys_setgroups (struct sysmsg *sysmsg, const struct setgroups_args *);
int	sys_getpgrp (struct sysmsg *sysmsg, const struct getpgrp_args *);
int	sys_setpgid (struct sysmsg *sysmsg, const struct setpgid_args *);
int	sys_setitimer (struct sysmsg *sysmsg, const struct setitimer_args *);
int	sys_swapon (struct sysmsg *sysmsg, const struct swapon_args *);
int	sys_getitimer (struct sysmsg *sysmsg, const struct getitimer_args *);
int	sys_getdtablesize (struct sysmsg *sysmsg, const struct getdtablesize_args *);
int	sys_dup2 (struct sysmsg *sysmsg, const struct dup2_args *);
int	sys_fcntl (struct sysmsg *sysmsg, const struct fcntl_args *);
int	sys_select (struct sysmsg *sysmsg, const struct select_args *);
int	sys_fsync (struct sysmsg *sysmsg, const struct fsync_args *);
int	sys_setpriority (struct sysmsg *sysmsg, const struct setpriority_args *);
int	sys_socket (struct sysmsg *sysmsg, const struct socket_args *);
int	sys_connect (struct sysmsg *sysmsg, const struct connect_args *);
int	sys_getpriority (struct sysmsg *sysmsg, const struct getpriority_args *);
int	sys_bind (struct sysmsg *sysmsg, const struct bind_args *);
int	sys_setsockopt (struct sysmsg *sysmsg, const struct setsockopt_args *);
int	sys_listen (struct sysmsg *sysmsg, const struct listen_args *);
int	sys_gettimeofday (struct sysmsg *sysmsg, const struct gettimeofday_args *);
int	sys_getrusage (struct sysmsg *sysmsg, const struct getrusage_args *);
int	sys_getsockopt (struct sysmsg *sysmsg, const struct getsockopt_args *);
int	sys_readv (struct sysmsg *sysmsg, const struct readv_args *);
int	sys_writev (struct sysmsg *sysmsg, const struct writev_args *);
int	sys_settimeofday (struct sysmsg *sysmsg, const struct settimeofday_args *);
int	sys_fchown (struct sysmsg *sysmsg, const struct fchown_args *);
int	sys_fchmod (struct sysmsg *sysmsg, const struct fchmod_args *);
int	sys_setreuid (struct sysmsg *sysmsg, const struct setreuid_args *);
int	sys_setregid (struct sysmsg *sysmsg, const struct setregid_args *);
int	sys_rename (struct sysmsg *sysmsg, const struct rename_args *);
int	sys_flock (struct sysmsg *sysmsg, const struct flock_args *);
int	sys_mkfifo (struct sysmsg *sysmsg, const struct mkfifo_args *);
int	sys_sendto (struct sysmsg *sysmsg, const struct sendto_args *);
int	sys_shutdown (struct sysmsg *sysmsg, const struct shutdown_args *);
int	sys_socketpair (struct sysmsg *sysmsg, const struct socketpair_args *);
int	sys_mkdir (struct sysmsg *sysmsg, const struct mkdir_args *);
int	sys_rmdir (struct sysmsg *sysmsg, const struct rmdir_args *);
int	sys_utimes (struct sysmsg *sysmsg, const struct utimes_args *);
int	sys_adjtime (struct sysmsg *sysmsg, const struct adjtime_args *);
int	sys_setsid (struct sysmsg *sysmsg, const struct setsid_args *);
int	sys_quotactl (struct sysmsg *sysmsg, const struct quotactl_args *);
int	sys_nfssvc (struct sysmsg *sysmsg, const struct nfssvc_args *);
int	sys_statfs (struct sysmsg *sysmsg, const struct statfs_args *);
int	sys_fstatfs (struct sysmsg *sysmsg, const struct fstatfs_args *);
int	sys_getfh (struct sysmsg *sysmsg, const struct getfh_args *);
int	sys_sysarch (struct sysmsg *sysmsg, const struct sysarch_args *);
int	sys_rtprio (struct sysmsg *sysmsg, const struct rtprio_args *);
int	sys_extpread (struct sysmsg *sysmsg, const struct extpread_args *);
int	sys_extpwrite (struct sysmsg *sysmsg, const struct extpwrite_args *);
int	sys_ntp_adjtime (struct sysmsg *sysmsg, const struct ntp_adjtime_args *);
int	sys_setgid (struct sysmsg *sysmsg, const struct setgid_args *);
int	sys_setegid (struct sysmsg *sysmsg, const struct setegid_args *);
int	sys_seteuid (struct sysmsg *sysmsg, const struct seteuid_args *);
int	sys_pathconf (struct sysmsg *sysmsg, const struct pathconf_args *);
int	sys_fpathconf (struct sysmsg *sysmsg, const struct fpathconf_args *);
int	sys_getrlimit (struct sysmsg *sysmsg, const struct __getrlimit_args *);
int	sys_setrlimit (struct sysmsg *sysmsg, const struct __setrlimit_args *);
int	sys_mmap (struct sysmsg *sysmsg, const struct mmap_args *);
int	sys_lseek (struct sysmsg *sysmsg, const struct lseek_args *);
int	sys_truncate (struct sysmsg *sysmsg, const struct truncate_args *);
int	sys_ftruncate (struct sysmsg *sysmsg, const struct ftruncate_args *);
int	sys___sysctl (struct sysmsg *sysmsg, const struct sysctl_args *);
int	sys_mlock (struct sysmsg *sysmsg, const struct mlock_args *);
int	sys_munlock (struct sysmsg *sysmsg, const struct munlock_args *);
int	sys_undelete (struct sysmsg *sysmsg, const struct undelete_args *);
int	sys_futimes (struct sysmsg *sysmsg, const struct futimes_args *);
int	sys_getpgid (struct sysmsg *sysmsg, const struct getpgid_args *);
int	sys_poll (struct sysmsg *sysmsg, const struct poll_args *);
int	sys_lkmnosys (struct sysmsg *sysmsg, const struct nosys_args *);
int	sys___semctl (struct sysmsg *sysmsg, const struct __semctl_args *);
int	sys_semget (struct sysmsg *sysmsg, const struct semget_args *);
int	sys_semop (struct sysmsg *sysmsg, const struct semop_args *);
int	sys_msgctl (struct sysmsg *sysmsg, const struct msgctl_args *);
int	sys_msgget (struct sysmsg *sysmsg, const struct msgget_args *);
int	sys_msgsnd (struct sysmsg *sysmsg, const struct msgsnd_args *);
int	sys_msgrcv (struct sysmsg *sysmsg, const struct msgrcv_args *);
int	sys_shmat (struct sysmsg *sysmsg, const struct shmat_args *);
int	sys_shmctl (struct sysmsg *sysmsg, const struct shmctl_args *);
int	sys_shmdt (struct sysmsg *sysmsg, const struct shmdt_args *);
int	sys_shmget (struct sysmsg *sysmsg, const struct shmget_args *);
int	sys_clock_gettime (struct sysmsg *sysmsg, const struct clock_gettime_args *);
int	sys_clock_settime (struct sysmsg *sysmsg, const struct clock_settime_args *);
int	sys_clock_getres (struct sysmsg *sysmsg, const struct clock_getres_args *);
int	sys_nanosleep (struct sysmsg *sysmsg, const struct nanosleep_args *);
int	sys_minherit (struct sysmsg *sysmsg, const struct minherit_args *);
int	sys_rfork (struct sysmsg *sysmsg, const struct rfork_args *);
int	sys_openbsd_poll (struct sysmsg *sysmsg, const struct openbsd_poll_args *);
int	sys_issetugid (struct sysmsg *sysmsg, const struct issetugid_args *);
int	sys_lchown (struct sysmsg *sysmsg, const struct lchown_args *);
int	sys_lchmod (struct sysmsg *sysmsg, const struct lchmod_args *);
int	sys_lutimes (struct sysmsg *sysmsg, const struct lutimes_args *);
int	sys_extpreadv (struct sysmsg *sysmsg, const struct extpreadv_args *);
int	sys_extpwritev (struct sysmsg *sysmsg, const struct extpwritev_args *);
int	sys_fhstatfs (struct sysmsg *sysmsg, const struct fhstatfs_args *);
int	sys_fhopen (struct sysmsg *sysmsg, const struct fhopen_args *);
int	sys_modnext (struct sysmsg *sysmsg, const struct modnext_args *);
int	sys_modstat (struct sysmsg *sysmsg, const struct modstat_args *);
int	sys_modfnext (struct sysmsg *sysmsg, const struct modfnext_args *);
int	sys_modfind (struct sysmsg *sysmsg, const struct modfind_args *);
int	sys_kldload (struct sysmsg *sysmsg, const struct kldload_args *);
int	sys_kldunload (struct sysmsg *sysmsg, const struct kldunload_args *);
int	sys_kldfind (struct sysmsg *sysmsg, const struct kldfind_args *);
int	sys_kldnext (struct sysmsg *sysmsg, const struct kldnext_args *);
int	sys_kldstat (struct sysmsg *sysmsg, const struct kldstat_args *);
int	sys_kldfirstmod (struct sysmsg *sysmsg, const struct kldfirstmod_args *);
int	sys_getsid (struct sysmsg *sysmsg, const struct getsid_args *);
int	sys_setresuid (struct sysmsg *sysmsg, const struct setresuid_args *);
int	sys_setresgid (struct sysmsg *sysmsg, const struct setresgid_args *);
int	sys_aio_return (struct sysmsg *sysmsg, const struct aio_return_args *);
int	sys_aio_suspend (struct sysmsg *sysmsg, const struct aio_suspend_args *);
int	sys_aio_cancel (struct sysmsg *sysmsg, const struct aio_cancel_args *);
int	sys_aio_error (struct sysmsg *sysmsg, const struct aio_error_args *);
int	sys_aio_read (struct sysmsg *sysmsg, const struct aio_read_args *);
int	sys_aio_write (struct sysmsg *sysmsg, const struct aio_write_args *);
int	sys_lio_listio (struct sysmsg *sysmsg, const struct lio_listio_args *);
int	sys_yield (struct sysmsg *sysmsg, const struct yield_args *);
int	sys_mlockall (struct sysmsg *sysmsg, const struct mlockall_args *);
int	sys_munlockall (struct sysmsg *sysmsg, const struct munlockall_args *);
int	sys___getcwd (struct sysmsg *sysmsg, const struct __getcwd_args *);
int	sys_sched_setparam (struct sysmsg *sysmsg, const struct sched_setparam_args *);
int	sys_sched_getparam (struct sysmsg *sysmsg, const struct sched_getparam_args *);
int	sys_sched_setscheduler (struct sysmsg *sysmsg, const struct sched_setscheduler_args *);
int	sys_sched_getscheduler (struct sysmsg *sysmsg, const struct sched_getscheduler_args *);
int	sys_sched_yield (struct sysmsg *sysmsg, const struct sched_yield_args *);
int	sys_sched_get_priority_max (struct sysmsg *sysmsg, const struct sched_get_priority_max_args *);
int	sys_sched_get_priority_min (struct sysmsg *sysmsg, const struct sched_get_priority_min_args *);
int	sys_sched_rr_get_interval (struct sysmsg *sysmsg, const struct sched_rr_get_interval_args *);
int	sys_utrace (struct sysmsg *sysmsg, const struct utrace_args *);
int	sys_kldsym (struct sysmsg *sysmsg, const struct kldsym_args *);
int	sys_jail (struct sysmsg *sysmsg, const struct jail_args *);
int	sys_sigprocmask (struct sysmsg *sysmsg, const struct sigprocmask_args *);
int	sys_sigsuspend (struct sysmsg *sysmsg, const struct sigsuspend_args *);
int	sys_sigaction (struct sysmsg *sysmsg, const struct sigaction_args *);
int	sys_sigpending (struct sysmsg *sysmsg, const struct sigpending_args *);
int	sys_sigreturn (struct sysmsg *sysmsg, const struct sigreturn_args *);
int	sys_sigtimedwait (struct sysmsg *sysmsg, const struct sigtimedwait_args *);
int	sys_sigwaitinfo (struct sysmsg *sysmsg, const struct sigwaitinfo_args *);
int	sys___acl_get_file (struct sysmsg *sysmsg, const struct __acl_get_file_args *);
int	sys___acl_set_file (struct sysmsg *sysmsg, const struct __acl_set_file_args *);
int	sys___acl_get_fd (struct sysmsg *sysmsg, const struct __acl_get_fd_args *);
int	sys___acl_set_fd (struct sysmsg *sysmsg, const struct __acl_set_fd_args *);
int	sys___acl_delete_file (struct sysmsg *sysmsg, const struct __acl_delete_file_args *);
int	sys___acl_delete_fd (struct sysmsg *sysmsg, const struct __acl_delete_fd_args *);
int	sys___acl_aclcheck_file (struct sysmsg *sysmsg, const struct __acl_aclcheck_file_args *);
int	sys___acl_aclcheck_fd (struct sysmsg *sysmsg, const struct __acl_aclcheck_fd_args *);
int	sys_extattrctl (struct sysmsg *sysmsg, const struct extattrctl_args *);
int	sys_extattr_set_file (struct sysmsg *sysmsg, const struct extattr_set_file_args *);
int	sys_extattr_get_file (struct sysmsg *sysmsg, const struct extattr_get_file_args *);
int	sys_extattr_delete_file (struct sysmsg *sysmsg, const struct extattr_delete_file_args *);
int	sys_aio_waitcomplete (struct sysmsg *sysmsg, const struct aio_waitcomplete_args *);
int	sys_getresuid (struct sysmsg *sysmsg, const struct getresuid_args *);
int	sys_getresgid (struct sysmsg *sysmsg, const struct getresgid_args *);
int	sys_kqueue (struct sysmsg *sysmsg, const struct kqueue_args *);
int	sys_kevent (struct sysmsg *sysmsg, const struct kevent_args *);
int	sys_kenv (struct sysmsg *sysmsg, const struct kenv_args *);
int	sys_lchflags (struct sysmsg *sysmsg, const struct lchflags_args *);
int	sys_uuidgen (struct sysmsg *sysmsg, const struct uuidgen_args *);
int	sys_sendfile (struct sysmsg *sysmsg, const struct sendfile_args *);
int	sys_varsym_set (struct sysmsg *sysmsg, const struct varsym_set_args *);
int	sys_varsym_get (struct sysmsg *sysmsg, const struct varsym_get_args *);
int	sys_varsym_list (struct sysmsg *sysmsg, const struct varsym_list_args *);
int	sys_exec_sys_register (struct sysmsg *sysmsg, const struct exec_sys_register_args *);
int	sys_exec_sys_unregister (struct sysmsg *sysmsg, const struct exec_sys_unregister_args *);
int	sys_sys_checkpoint (struct sysmsg *sysmsg, const struct sys_checkpoint_args *);
int	sys_mountctl (struct sysmsg *sysmsg, const struct mountctl_args *);
int	sys_umtx_sleep (struct sysmsg *sysmsg, const struct umtx_sleep_args *);
int	sys_umtx_wakeup (struct sysmsg *sysmsg, const struct umtx_wakeup_args *);
int	sys_jail_attach (struct sysmsg *sysmsg, const struct jail_attach_args *);
int	sys_set_tls_area (struct sysmsg *sysmsg, const struct set_tls_area_args *);
int	sys_get_tls_area (struct sysmsg *sysmsg, const struct get_tls_area_args *);
int	sys_closefrom (struct sysmsg *sysmsg, const struct closefrom_args *);
int	sys_stat (struct sysmsg *sysmsg, const struct stat_args *);
int	sys_fstat (struct sysmsg *sysmsg, const struct fstat_args *);
int	sys_lstat (struct sysmsg *sysmsg, const struct lstat_args *);
int	sys_fhstat (struct sysmsg *sysmsg, const struct fhstat_args *);
int	sys_getdirentries (struct sysmsg *sysmsg, const struct getdirentries_args *);
int	sys_getdents (struct sysmsg *sysmsg, const struct getdents_args *);
int	sys_usched_set (struct sysmsg *sysmsg, const struct usched_set_args *);
int	sys_extaccept (struct sysmsg *sysmsg, const struct extaccept_args *);
int	sys_extconnect (struct sysmsg *sysmsg, const struct extconnect_args *);
int	sys_mcontrol (struct sysmsg *sysmsg, const struct mcontrol_args *);
int	sys_vmspace_create (struct sysmsg *sysmsg, const struct vmspace_create_args *);
int	sys_vmspace_destroy (struct sysmsg *sysmsg, const struct vmspace_destroy_args *);
int	sys_vmspace_ctl (struct sysmsg *sysmsg, const struct vmspace_ctl_args *);
int	sys_vmspace_mmap (struct sysmsg *sysmsg, const struct vmspace_mmap_args *);
int	sys_vmspace_munmap (struct sysmsg *sysmsg, const struct vmspace_munmap_args *);
int	sys_vmspace_mcontrol (struct sysmsg *sysmsg, const struct vmspace_mcontrol_args *);
int	sys_vmspace_pread (struct sysmsg *sysmsg, const struct vmspace_pread_args *);
int	sys_vmspace_pwrite (struct sysmsg *sysmsg, const struct vmspace_pwrite_args *);
int	sys_extexit (struct sysmsg *sysmsg, const struct extexit_args *);
int	sys_lwp_create (struct sysmsg *sysmsg, const struct lwp_create_args *);
int	sys_lwp_gettid (struct sysmsg *sysmsg, const struct lwp_gettid_args *);
int	sys_lwp_kill (struct sysmsg *sysmsg, const struct lwp_kill_args *);
int	sys_lwp_rtprio (struct sysmsg *sysmsg, const struct lwp_rtprio_args *);
int	sys_pselect (struct sysmsg *sysmsg, const struct pselect_args *);
int	sys_statvfs (struct sysmsg *sysmsg, const struct statvfs_args *);
int	sys_fstatvfs (struct sysmsg *sysmsg, const struct fstatvfs_args *);
int	sys_fhstatvfs (struct sysmsg *sysmsg, const struct fhstatvfs_args *);
int	sys_getvfsstat (struct sysmsg *sysmsg, const struct getvfsstat_args *);
int	sys_openat (struct sysmsg *sysmsg, const struct openat_args *);
int	sys_fstatat (struct sysmsg *sysmsg, const struct fstatat_args *);
int	sys_fchmodat (struct sysmsg *sysmsg, const struct fchmodat_args *);
int	sys_fchownat (struct sysmsg *sysmsg, const struct fchownat_args *);
int	sys_unlinkat (struct sysmsg *sysmsg, const struct unlinkat_args *);
int	sys_faccessat (struct sysmsg *sysmsg, const struct faccessat_args *);
int	sys_mq_open (struct sysmsg *sysmsg, const struct mq_open_args *);
int	sys_mq_close (struct sysmsg *sysmsg, const struct mq_close_args *);
int	sys_mq_unlink (struct sysmsg *sysmsg, const struct mq_unlink_args *);
int	sys_mq_getattr (struct sysmsg *sysmsg, const struct mq_getattr_args *);
int	sys_mq_setattr (struct sysmsg *sysmsg, const struct mq_setattr_args *);
int	sys_mq_notify (struct sysmsg *sysmsg, const struct mq_notify_args *);
int	sys_mq_send (struct sysmsg *sysmsg, const struct mq_send_args *);
int	sys_mq_receive (struct sysmsg *sysmsg, const struct mq_receive_args *);
int	sys_mq_timedsend (struct sysmsg *sysmsg, const struct mq_timedsend_args *);
int	sys_mq_timedreceive (struct sysmsg *sysmsg, const struct mq_timedreceive_args *);
int	sys_ioprio_set (struct sysmsg *sysmsg, const struct ioprio_set_args *);
int	sys_ioprio_get (struct sysmsg *sysmsg, const struct ioprio_get_args *);
int	sys_chroot_kernel (struct sysmsg *sysmsg, const struct chroot_kernel_args *);
int	sys_renameat (struct sysmsg *sysmsg, const struct renameat_args *);
int	sys_mkdirat (struct sysmsg *sysmsg, const struct mkdirat_args *);
int	sys_mkfifoat (struct sysmsg *sysmsg, const struct mkfifoat_args *);
int	sys_mknodat (struct sysmsg *sysmsg, const struct mknodat_args *);
int	sys_readlinkat (struct sysmsg *sysmsg, const struct readlinkat_args *);
int	sys_symlinkat (struct sysmsg *sysmsg, const struct symlinkat_args *);
int	sys_swapoff (struct sysmsg *sysmsg, const struct swapoff_args *);
int	sys_vquotactl (struct sysmsg *sysmsg, const struct vquotactl_args *);
int	sys_linkat (struct sysmsg *sysmsg, const struct linkat_args *);
int	sys_eaccess (struct sysmsg *sysmsg, const struct eaccess_args *);
int	sys_lpathconf (struct sysmsg *sysmsg, const struct lpathconf_args *);
int	sys_vmm_guest_ctl (struct sysmsg *sysmsg, const struct vmm_guest_ctl_args *);
int	sys_vmm_guest_sync_addr (struct sysmsg *sysmsg, const struct vmm_guest_sync_addr_args *);
int	sys_procctl (struct sysmsg *sysmsg, const struct procctl_args *);
int	sys_chflagsat (struct sysmsg *sysmsg, const struct chflagsat_args *);
int	sys_pipe2 (struct sysmsg *sysmsg, const struct pipe2_args *);
int	sys_utimensat (struct sysmsg *sysmsg, const struct utimensat_args *);
int	sys_futimens (struct sysmsg *sysmsg, const struct futimens_args *);
int	sys_accept4 (struct sysmsg *sysmsg, const struct accept4_args *);
int	sys_lwp_setname (struct sysmsg *sysmsg, const struct lwp_setname_args *);
int	sys_ppoll (struct sysmsg *sysmsg, const struct ppoll_args *);
int	sys_lwp_setaffinity (struct sysmsg *sysmsg, const struct lwp_setaffinity_args *);
int	sys_lwp_getaffinity (struct sysmsg *sysmsg, const struct lwp_getaffinity_args *);
int	sys_lwp_create2 (struct sysmsg *sysmsg, const struct lwp_create2_args *);
int	sys_getcpuclockid (struct sysmsg *sysmsg, const struct getcpuclockid_args *);
int	sys_wait6 (struct sysmsg *sysmsg, const struct wait6_args *);
int	sys_lwp_getname (struct sysmsg *sysmsg, const struct lwp_getname_args *);
int	sys_getrandom (struct sysmsg *sysmsg, const struct getrandom_args *);
int	sys___realpath (struct sysmsg *sysmsg, const struct __realpath_args *);

#endif /* !_SYS_SYSPROTO_H_ */
#undef PAD_

#endif /* _KERNEL */
