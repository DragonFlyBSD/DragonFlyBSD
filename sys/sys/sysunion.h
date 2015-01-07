/*
 * Union of syscall args for messaging.
 *
 * DO NOT EDIT-- To regenerate this file, edit syscalls.master followed
 *               by running make sysent in the same directory.
 */

union sysunion {
#ifdef _KERNEL /* header only applies in kernel */
	struct	lwkt_msg lmsg;
	struct	sysmsg sysmsg;
#endif
#ifdef COMPAT_43
#endif
	struct	nosys_args nosys;
	struct	exit_args exit;
	struct	fork_args fork;
	struct	read_args read;
	struct	write_args write;
	struct	open_args open;
	struct	close_args close;
	struct	wait_args wait4;
#ifdef COMPAT_43
	struct	ocreat_args sys_ocreat;
#endif
	struct	link_args link;
	struct	unlink_args unlink;
	struct	chdir_args chdir;
	struct	fchdir_args fchdir;
	struct	mknod_args mknod;
	struct	chmod_args chmod;
	struct	chown_args chown;
	struct	obreak_args obreak;
	struct	getfsstat_args getfsstat;
#ifdef COMPAT_43
	struct	olseek_args sys_olseek;
#endif
	struct	getpid_args getpid;
	struct	mount_args mount;
	struct	unmount_args unmount;
	struct	setuid_args setuid;
	struct	getuid_args getuid;
	struct	geteuid_args geteuid;
	struct	ptrace_args ptrace;
	struct	recvmsg_args recvmsg;
	struct	sendmsg_args sendmsg;
	struct	recvfrom_args recvfrom;
	struct	accept_args accept;
	struct	getpeername_args getpeername;
	struct	getsockname_args getsockname;
	struct	access_args access;
	struct	chflags_args chflags;
	struct	fchflags_args fchflags;
	struct	sync_args sync;
	struct	kill_args kill;
#ifdef COMPAT_43
	struct	ostat_args sys_ostat;
#endif
	struct	getppid_args getppid;
#ifdef COMPAT_43
	struct	olstat_args sys_olstat;
#endif
	struct	dup_args dup;
	struct	pipe_args pipe;
	struct	getegid_args getegid;
	struct	profil_args profil;
	struct	ktrace_args ktrace;
	struct	getgid_args getgid;
	struct	getlogin_args getlogin;
	struct	setlogin_args setlogin;
	struct	acct_args acct;
	struct	sigaltstack_args sigaltstack;
	struct	ioctl_args ioctl;
	struct	reboot_args reboot;
	struct	revoke_args revoke;
	struct	symlink_args symlink;
	struct	readlink_args readlink;
	struct	execve_args execve;
	struct	umask_args umask;
	struct	chroot_args chroot;
#ifdef COMPAT_43
	struct	ofstat_args sys_ofstat;
#endif
#ifdef COMPAT_43
	struct	getkerninfo_args getkerninfo;
#endif
	struct	getpagesize_args getpagesize;
	struct	msync_args msync;
	struct	vfork_args vfork;
	struct	sbrk_args sbrk;
	struct	sstk_args sstk;
#ifdef COMPAT_43
	struct	ommap_args sys_ommap;
#endif
#ifdef COMPAT_43
	struct	ovadvise_args vadvise;
#endif
	struct	munmap_args munmap;
	struct	mprotect_args mprotect;
	struct	madvise_args madvise;
	struct	mincore_args mincore;
	struct	getgroups_args getgroups;
	struct	setgroups_args setgroups;
	struct	getpgrp_args getpgrp;
	struct	setpgid_args setpgid;
	struct	setitimer_args setitimer;
	struct	owait_args sys_owait;
	struct	swapon_args swapon;
	struct	getitimer_args getitimer;
#ifdef COMPAT_43
	struct	gethostname_args gethostname;
#endif
#ifdef COMPAT_43
	struct	sethostname_args sethostname;
#endif
	struct	getdtablesize_args getdtablesize;
	struct	dup2_args dup2;
	struct	fcntl_args fcntl;
	struct	select_args select;
	struct	fsync_args fsync;
	struct	setpriority_args setpriority;
	struct	socket_args socket;
	struct	connect_args connect;
	struct	getpriority_args getpriority;
#ifdef COMPAT_43
	struct	osend_args sys_osend;
#endif
#ifdef COMPAT_43
	struct	orecv_args sys_orecv;
#endif
	struct	bind_args bind;
	struct	setsockopt_args setsockopt;
	struct	listen_args listen;
#ifdef COMPAT_43
	struct	osigvec_args sys_osigvec;
#endif
#ifdef COMPAT_43
	struct	osigblock_args sys_osigblock;
#endif
#ifdef COMPAT_43
	struct	osigsetmask_args sys_osigsetmask;
#endif
#ifdef COMPAT_43
	struct	osigstack_args sys_osigstack;
#endif
#ifdef COMPAT_43
	struct	orecvmsg_args sys_orecvmsg;
#endif
#ifdef COMPAT_43
	struct	osendmsg_args sys_osendmsg;
#endif
	struct	gettimeofday_args gettimeofday;
	struct	getrusage_args getrusage;
	struct	getsockopt_args getsockopt;
	struct	readv_args readv;
	struct	writev_args writev;
	struct	settimeofday_args settimeofday;
	struct	fchown_args fchown;
	struct	fchmod_args fchmod;
	struct	setreuid_args setreuid;
	struct	setregid_args setregid;
	struct	rename_args rename;
#ifdef COMPAT_43
	struct	otruncate_args sys_otruncate;
#endif
#ifdef COMPAT_43
	struct	oftruncate_args sys_oftruncate;
#endif
	struct	flock_args flock;
	struct	mkfifo_args mkfifo;
	struct	sendto_args sendto;
	struct	shutdown_args shutdown;
	struct	socketpair_args socketpair;
	struct	mkdir_args mkdir;
	struct	rmdir_args rmdir;
	struct	utimes_args utimes;
	struct	adjtime_args adjtime;
#ifdef COMPAT_43
	struct	ogetpeername_args sys_ogetpeername;
#endif
	struct	ogethostid_args sys_ogethostid;
#ifdef COMPAT_43
	struct	osethostid_args sys_osethostid;
#endif
#ifdef COMPAT_43
	struct	ogetrlimit_args sys_ogetrlimit;
#endif
#ifdef COMPAT_43
	struct	osetrlimit_args sys_osetrlimit;
#endif
#ifdef COMPAT_43
	struct	okillpg_args sys_okillpg;
#endif
	struct	setsid_args setsid;
	struct	quotactl_args quotactl;
	struct	oquota_args sys_oquota;
	struct	nfssvc_args nfssvc;
#ifdef COMPAT_43
	struct	ogetdirentries_args sys_ogetdirentries;
#endif
	struct	statfs_args statfs;
	struct	fstatfs_args fstatfs;
	struct	getfh_args getfh;
	struct	getdomainname_args getdomainname;
	struct	setdomainname_args setdomainname;
	struct	uname_args uname;
	struct	sysarch_args sysarch;
	struct	rtprio_args rtprio;
	struct	extpread_args extpread;
	struct	extpwrite_args extpwrite;
	struct	ntp_adjtime_args ntp_adjtime;
	struct	setgid_args setgid;
	struct	setegid_args setegid;
	struct	seteuid_args seteuid;
#ifdef COMPAT_DF12
	struct	dfbsd12_stat_args sys_dfbsd12_stat;
#endif
#ifdef COMPAT_DF12
	struct	dfbsd12_fstat_args sys_dfbsd12_fstat;
#endif
#ifdef COMPAT_DF12
	struct	dfbsd12_lstat_args sys_dfbsd12_lstat;
#endif
	struct	pathconf_args pathconf;
	struct	fpathconf_args fpathconf;
	struct	__getrlimit_args getrlimit;
	struct	__setrlimit_args setrlimit;
#ifdef COMPAT_DF12
	struct	dfbsd12_getdirentries_args sys_dfbsd12_getdirentries;
#endif
	struct	mmap_args mmap;
	struct	lseek_args lseek;
	struct	truncate_args truncate;
	struct	ftruncate_args ftruncate;
	struct	sysctl_args __sysctl;
	struct	mlock_args mlock;
	struct	munlock_args munlock;
	struct	undelete_args undelete;
	struct	futimes_args futimes;
	struct	getpgid_args getpgid;
	struct	poll_args poll;
	struct	__semctl_args __semctl;
	struct	semget_args semget;
	struct	semop_args semop;
	struct	msgctl_args msgctl;
	struct	msgget_args msgget;
	struct	msgsnd_args msgsnd;
	struct	msgrcv_args msgrcv;
	struct	shmat_args shmat;
	struct	shmctl_args shmctl;
	struct	shmdt_args shmdt;
	struct	shmget_args shmget;
	struct	clock_gettime_args clock_gettime;
	struct	clock_settime_args clock_settime;
	struct	clock_getres_args clock_getres;
	struct	nanosleep_args nanosleep;
	struct	minherit_args minherit;
	struct	rfork_args rfork;
	struct	openbsd_poll_args openbsd_poll;
	struct	issetugid_args issetugid;
	struct	lchown_args lchown;
#ifdef COMPAT_DF12
	struct	dfbsd12_getdents_args sys_dfbsd12_getdents;
#endif
	struct	lchmod_args lchmod;
	struct	lutimes_args lutimes;
	struct	extpreadv_args extpreadv;
	struct	extpwritev_args extpwritev;
	struct	fhstatfs_args fhstatfs;
	struct	fhopen_args fhopen;
#ifdef COMPAT_DF12
	struct	dfbsd12_fhstat_args sys_dfbsd12_fhstat;
#endif
	struct	modnext_args modnext;
	struct	modstat_args modstat;
	struct	modfnext_args modfnext;
	struct	modfind_args modfind;
	struct	kldload_args kldload;
	struct	kldunload_args kldunload;
	struct	kldfind_args kldfind;
	struct	kldnext_args kldnext;
	struct	kldstat_args kldstat;
	struct	kldfirstmod_args kldfirstmod;
	struct	getsid_args getsid;
	struct	setresuid_args setresuid;
	struct	setresgid_args setresgid;
	struct	aio_return_args aio_return;
	struct	aio_suspend_args aio_suspend;
	struct	aio_cancel_args aio_cancel;
	struct	aio_error_args aio_error;
	struct	aio_read_args aio_read;
	struct	aio_write_args aio_write;
	struct	lio_listio_args lio_listio;
	struct	yield_args yield;
	struct	mlockall_args mlockall;
	struct	munlockall_args munlockall;
	struct	__getcwd_args __getcwd;
	struct	sched_setparam_args sched_setparam;
	struct	sched_getparam_args sched_getparam;
	struct	sched_setscheduler_args sched_setscheduler;
	struct	sched_getscheduler_args sched_getscheduler;
	struct	sched_yield_args sched_yield;
	struct	sched_get_priority_max_args sched_get_priority_max;
	struct	sched_get_priority_min_args sched_get_priority_min;
	struct	sched_rr_get_interval_args sched_rr_get_interval;
	struct	utrace_args utrace;
	struct	kldsym_args kldsym;
	struct	jail_args jail;
	struct	sigprocmask_args sigprocmask;
	struct	sigsuspend_args sigsuspend;
	struct	sigaction_args sigaction;
	struct	sigpending_args sigpending;
	struct	sigreturn_args sigreturn;
	struct	sigtimedwait_args sigtimedwait;
	struct	sigwaitinfo_args sigwaitinfo;
	struct	__acl_get_file_args __acl_get_file;
	struct	__acl_set_file_args __acl_set_file;
	struct	__acl_get_fd_args __acl_get_fd;
	struct	__acl_set_fd_args __acl_set_fd;
	struct	__acl_delete_file_args __acl_delete_file;
	struct	__acl_delete_fd_args __acl_delete_fd;
	struct	__acl_aclcheck_file_args __acl_aclcheck_file;
	struct	__acl_aclcheck_fd_args __acl_aclcheck_fd;
	struct	extattrctl_args extattrctl;
	struct	extattr_set_file_args extattr_set_file;
	struct	extattr_get_file_args extattr_get_file;
	struct	extattr_delete_file_args extattr_delete_file;
	struct	aio_waitcomplete_args aio_waitcomplete;
	struct	getresuid_args getresuid;
	struct	getresgid_args getresgid;
	struct	kqueue_args kqueue;
	struct	kevent_args kevent;
	struct	lchflags_args lchflags;
	struct	uuidgen_args uuidgen;
	struct	sendfile_args sendfile;
	struct	varsym_set_args varsym_set;
	struct	varsym_get_args varsym_get;
	struct	varsym_list_args varsym_list;
	struct	exec_sys_register_args exec_sys_register;
	struct	exec_sys_unregister_args exec_sys_unregister;
	struct	sys_checkpoint_args sys_checkpoint;
	struct	mountctl_args mountctl;
	struct	umtx_sleep_args umtx_sleep;
	struct	umtx_wakeup_args umtx_wakeup;
	struct	jail_attach_args jail_attach;
	struct	set_tls_area_args set_tls_area;
	struct	get_tls_area_args get_tls_area;
	struct	closefrom_args closefrom;
	struct	stat_args stat;
	struct	fstat_args fstat;
	struct	lstat_args lstat;
	struct	fhstat_args fhstat;
	struct	getdirentries_args getdirentries;
	struct	getdents_args getdents;
	struct	usched_set_args usched_set;
	struct	extaccept_args extaccept;
	struct	extconnect_args extconnect;
	struct	mcontrol_args mcontrol;
	struct	vmspace_create_args vmspace_create;
	struct	vmspace_destroy_args vmspace_destroy;
	struct	vmspace_ctl_args vmspace_ctl;
	struct	vmspace_mmap_args vmspace_mmap;
	struct	vmspace_munmap_args vmspace_munmap;
	struct	vmspace_mcontrol_args vmspace_mcontrol;
	struct	vmspace_pread_args vmspace_pread;
	struct	vmspace_pwrite_args vmspace_pwrite;
	struct	extexit_args extexit;
	struct	lwp_create_args lwp_create;
	struct	lwp_gettid_args lwp_gettid;
	struct	lwp_kill_args lwp_kill;
	struct	lwp_rtprio_args lwp_rtprio;
	struct	pselect_args pselect;
	struct	statvfs_args statvfs;
	struct	fstatvfs_args fstatvfs;
	struct	fhstatvfs_args fhstatvfs;
	struct	getvfsstat_args getvfsstat;
	struct	openat_args openat;
	struct	fstatat_args fstatat;
	struct	fchmodat_args fchmodat;
	struct	fchownat_args fchownat;
	struct	unlinkat_args unlinkat;
	struct	faccessat_args faccessat;
	struct	mq_open_args mq_open;
	struct	mq_close_args mq_close;
	struct	mq_unlink_args mq_unlink;
	struct	mq_getattr_args mq_getattr;
	struct	mq_setattr_args mq_setattr;
	struct	mq_notify_args mq_notify;
	struct	mq_send_args mq_send;
	struct	mq_receive_args mq_receive;
	struct	mq_timedsend_args mq_timedsend;
	struct	mq_timedreceive_args mq_timedreceive;
	struct	ioprio_set_args ioprio_set;
	struct	ioprio_get_args ioprio_get;
	struct	chroot_kernel_args chroot_kernel;
	struct	renameat_args renameat;
	struct	mkdirat_args mkdirat;
	struct	mkfifoat_args mkfifoat;
	struct	mknodat_args mknodat;
	struct	readlinkat_args readlinkat;
	struct	symlinkat_args symlinkat;
	struct	swapoff_args swapoff;
	struct	vquotactl_args vquotactl;
	struct	linkat_args linkat;
	struct	eaccess_args eaccess;
	struct	lpathconf_args lpathconf;
	struct	vmm_guest_ctl_args vmm_guest_ctl;
	struct	vmm_guest_sync_addr_args vmm_guest_sync_addr;
	struct	procctl_args procctl;
};
