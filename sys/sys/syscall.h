/*
 * System call numbers.
 *
 * DO NOT EDIT-- To regenerate this file, edit syscalls.master followed
 *               by running make sysent in the same directory.
 */

#define	SYS_syscall	0
#define	SYS_exit	1
#define	SYS_fork	2
#define	SYS_read	3
#define	SYS_write	4
#define	SYS_open	5
#define	SYS_close	6
#define	SYS_wait4	7
				/* 8 is old creat */
#define	SYS_link	9
#define	SYS_unlink	10
				/* 11 is obsolete execv */
#define	SYS_chdir	12
#define	SYS_fchdir	13
#define	SYS_mknod	14
#define	SYS_chmod	15
#define	SYS_chown	16
#define	SYS_break	17
#define	SYS_getfsstat	18
				/* 19 is old lseek */
#define	SYS_getpid	20
#define	SYS_mount	21
#define	SYS_unmount	22
#define	SYS_setuid	23
#define	SYS_getuid	24
#define	SYS_geteuid	25
#define	SYS_ptrace	26
#define	SYS_recvmsg	27
#define	SYS_sendmsg	28
#define	SYS_recvfrom	29
#define	SYS_accept	30
#define	SYS_getpeername	31
#define	SYS_getsockname	32
#define	SYS_access	33
#define	SYS_chflags	34
#define	SYS_fchflags	35
#define	SYS_sync	36
#define	SYS_kill	37
				/* 38 is old stat */
#define	SYS_getppid	39
				/* 40 is old lstat */
#define	SYS_dup	41
#define	SYS_pipe	42
#define	SYS_getegid	43
#define	SYS_profil	44
#define	SYS_ktrace	45
				/* 46 is obsolete freebsd3_sigaction */
#define	SYS_getgid	47
				/* 48 is obsolete freebsd3_sigprocmask */
#define	SYS_getlogin	49
#define	SYS_setlogin	50
#define	SYS_acct	51
				/* 52 is obsolete freebsd3_sigpending */
#define	SYS_sigaltstack	53
#define	SYS_ioctl	54
#define	SYS_reboot	55
#define	SYS_revoke	56
#define	SYS_symlink	57
#define	SYS_readlink	58
#define	SYS_execve	59
#define	SYS_umask	60
#define	SYS_chroot	61
				/* 62 is old fstat */
				/* 63 is old getkerninfo */
				/* 64 is old getpagesize */
#define	SYS_msync	65
#define	SYS_vfork	66
				/* 67 is obsolete vread */
				/* 68 is obsolete vwrite */
#define	SYS_sbrk	69
#define	SYS_sstk	70
				/* 71 is old mmap */
				/* 72 is old vadvise */
#define	SYS_munmap	73
#define	SYS_mprotect	74
#define	SYS_madvise	75
				/* 76 is obsolete vhangup */
				/* 77 is obsolete vlimit */
#define	SYS_mincore	78
#define	SYS_getgroups	79
#define	SYS_setgroups	80
#define	SYS_getpgrp	81
#define	SYS_setpgid	82
#define	SYS_setitimer	83
				/* 84 is old wait */
#define	SYS_swapon	85
#define	SYS_getitimer	86
				/* 87 is old gethostname */
				/* 88 is old sethostname */
#define	SYS_getdtablesize	89
#define	SYS_dup2	90
#define	SYS_fcntl	92
#define	SYS_select	93
#define	SYS_fsync	95
#define	SYS_setpriority	96
#define	SYS_socket	97
#define	SYS_connect	98
				/* 99 is old accept */
#define	SYS_getpriority	100
				/* 101 is old send */
				/* 102 is old recv */
				/* 103 is obsolete freebsd3_sigreturn */
#define	SYS_bind	104
#define	SYS_setsockopt	105
#define	SYS_listen	106
				/* 107 is obsolete vtimes */
				/* 108 is old sigvec */
				/* 109 is old sigblock */
				/* 110 is old sigsetmask */
				/* 111 is obsolete freebsd3_sigsuspend */
				/* 112 is old sigstack */
				/* 113 is old recvmsg */
				/* 114 is old sendmsg */
				/* 115 is obsolete vtrace */
#define	SYS_gettimeofday	116
#define	SYS_getrusage	117
#define	SYS_getsockopt	118
#define	SYS_readv	120
#define	SYS_writev	121
#define	SYS_settimeofday	122
#define	SYS_fchown	123
#define	SYS_fchmod	124
				/* 125 is old recvfrom */
#define	SYS_setreuid	126
#define	SYS_setregid	127
#define	SYS_rename	128
				/* 129 is old truncate */
				/* 130 is old ftruncate */
#define	SYS_flock	131
#define	SYS_mkfifo	132
#define	SYS_sendto	133
#define	SYS_shutdown	134
#define	SYS_socketpair	135
#define	SYS_mkdir	136
#define	SYS_rmdir	137
#define	SYS_utimes	138
				/* 139 is obsolete 4.2 sigreturn */
#define	SYS_adjtime	140
				/* 141 is old getpeername */
				/* 142 is old gethostid */
				/* 143 is old sethostid */
				/* 144 is old getrlimit */
				/* 145 is old setrlimit */
				/* 146 is old killpg */
#define	SYS_setsid	147
#define	SYS_quotactl	148
				/* 149 is old quota */
				/* 150 is old getsockname */
#define	SYS_nfssvc	155
				/* 156 is old getdirentries */
#define	SYS_statfs	157
#define	SYS_fstatfs	158
#define	SYS_getfh	161
#define	SYS_getdomainname	162
#define	SYS_setdomainname	163
#define	SYS_uname	164
#define	SYS_sysarch	165
#define	SYS_rtprio	166
				/* 169 is obsolete semsys */
				/* 170 is obsolete msgsys */
				/* 171 is obsolete shmsys */
#define	SYS_extpread	173
#define	SYS_extpwrite	174
#define	SYS_ntp_adjtime	176
#define	SYS_setgid	181
#define	SYS_setegid	182
#define	SYS_seteuid	183
				/* 188 is old stat */
				/* 189 is old fstat */
				/* 190 is old lstat */
#define	SYS_pathconf	191
#define	SYS_fpathconf	192
#define	SYS_getrlimit	194
#define	SYS_setrlimit	195
				/* 196 is old getdirentries */
#define	SYS_mmap	197
#define	SYS___syscall	198
#define	SYS_lseek	199
#define	SYS_truncate	200
#define	SYS_ftruncate	201
#define	SYS___sysctl	202
#define	SYS_mlock	203
#define	SYS_munlock	204
#define	SYS_undelete	205
#define	SYS_futimes	206
#define	SYS_getpgid	207
#define	SYS_poll	209
#define	SYS___semctl	220
#define	SYS_semget	221
#define	SYS_semop	222
#define	SYS_msgctl	224
#define	SYS_msgget	225
#define	SYS_msgsnd	226
#define	SYS_msgrcv	227
#define	SYS_shmat	228
#define	SYS_shmctl	229
#define	SYS_shmdt	230
#define	SYS_shmget	231
#define	SYS_clock_gettime	232
#define	SYS_clock_settime	233
#define	SYS_clock_getres	234
#define	SYS_nanosleep	240
#define	SYS_minherit	250
#define	SYS_rfork	251
#define	SYS_openbsd_poll	252
#define	SYS_issetugid	253
#define	SYS_lchown	254
				/* 272 is old getdents */
#define	SYS_lchmod	274
#define	SYS_netbsd_lchown	275
#define	SYS_lutimes	276
#define	SYS_netbsd_msync	277
				/* 278 is obsolete { */
				/* 279 is obsolete { */
				/* 280 is obsolete { */
#define	SYS_extpreadv	289
#define	SYS_extpwritev	290
#define	SYS_fhstatfs	297
#define	SYS_fhopen	298
				/* 299 is old fhstat */
#define	SYS_modnext	300
#define	SYS_modstat	301
#define	SYS_modfnext	302
#define	SYS_modfind	303
#define	SYS_kldload	304
#define	SYS_kldunload	305
#define	SYS_kldfind	306
#define	SYS_kldnext	307
#define	SYS_kldstat	308
#define	SYS_kldfirstmod	309
#define	SYS_getsid	310
#define	SYS_setresuid	311
#define	SYS_setresgid	312
				/* 313 is obsolete signanosleep */
#define	SYS_aio_return	314
#define	SYS_aio_suspend	315
#define	SYS_aio_cancel	316
#define	SYS_aio_error	317
#define	SYS_aio_read	318
#define	SYS_aio_write	319
#define	SYS_lio_listio	320
#define	SYS_yield	321
#define	SYS_mlockall	324
#define	SYS_munlockall	325
#define	SYS___getcwd	326
#define	SYS_sched_setparam	327
#define	SYS_sched_getparam	328
#define	SYS_sched_setscheduler	329
#define	SYS_sched_getscheduler	330
#define	SYS_sched_yield	331
#define	SYS_sched_get_priority_max	332
#define	SYS_sched_get_priority_min	333
#define	SYS_sched_rr_get_interval	334
#define	SYS_utrace	335
				/* 336 is obsolete freebsd4_sendfile */
#define	SYS_kldsym	337
#define	SYS_jail	338
#define	SYS_sigprocmask	340
#define	SYS_sigsuspend	341
#define	SYS_sigaction	342
#define	SYS_sigpending	343
#define	SYS_sigreturn	344
#define	SYS_sigtimedwait	345
#define	SYS_sigwaitinfo	346
#define	SYS___acl_get_file	347
#define	SYS___acl_set_file	348
#define	SYS___acl_get_fd	349
#define	SYS___acl_set_fd	350
#define	SYS___acl_delete_file	351
#define	SYS___acl_delete_fd	352
#define	SYS___acl_aclcheck_file	353
#define	SYS___acl_aclcheck_fd	354
#define	SYS_extattrctl	355
#define	SYS_extattr_set_file	356
#define	SYS_extattr_get_file	357
#define	SYS_extattr_delete_file	358
#define	SYS_aio_waitcomplete	359
#define	SYS_getresuid	360
#define	SYS_getresgid	361
#define	SYS_kqueue	362
#define	SYS_kevent	363
#define	SYS_lchflags	391
#define	SYS_uuidgen	392
#define	SYS_sendfile	393
#define	SYS_varsym_set	450
#define	SYS_varsym_get	451
#define	SYS_varsym_list	452
				/* 453 is obsolete upc_register */
				/* 454 is obsolete upc_control */
				/* 455 is obsolete caps_sys_service */
				/* 456 is obsolete caps_sys_client */
				/* 457 is obsolete caps_sys_close */
				/* 458 is obsolete caps_sys_put */
				/* 459 is obsolete caps_sys_reply */
				/* 460 is obsolete caps_sys_get */
				/* 461 is obsolete caps_sys_wait */
				/* 462 is obsolete caps_sys_abort */
				/* 463 is obsolete caps_sys_getgen */
				/* 464 is obsolete caps_sys_setgen */
#define	SYS_exec_sys_register	465
#define	SYS_exec_sys_unregister	466
#define	SYS_sys_checkpoint	467
#define	SYS_mountctl	468
#define	SYS_umtx_sleep	469
#define	SYS_umtx_wakeup	470
#define	SYS_jail_attach	471
#define	SYS_set_tls_area	472
#define	SYS_get_tls_area	473
#define	SYS_closefrom	474
#define	SYS_stat	475
#define	SYS_fstat	476
#define	SYS_lstat	477
#define	SYS_fhstat	478
#define	SYS_getdirentries	479
#define	SYS_getdents	480
#define	SYS_usched_set	481
#define	SYS_extaccept	482
#define	SYS_extconnect	483
				/* 484 is obsolete syslink */
#define	SYS_mcontrol	485
#define	SYS_vmspace_create	486
#define	SYS_vmspace_destroy	487
#define	SYS_vmspace_ctl	488
#define	SYS_vmspace_mmap	489
#define	SYS_vmspace_munmap	490
#define	SYS_vmspace_mcontrol	491
#define	SYS_vmspace_pread	492
#define	SYS_vmspace_pwrite	493
#define	SYS_extexit	494
#define	SYS_lwp_create	495
#define	SYS_lwp_gettid	496
#define	SYS_lwp_kill	497
#define	SYS_lwp_rtprio	498
#define	SYS_pselect	499
#define	SYS_statvfs	500
#define	SYS_fstatvfs	501
#define	SYS_fhstatvfs	502
#define	SYS_getvfsstat	503
#define	SYS_openat	504
#define	SYS_fstatat	505
#define	SYS_fchmodat	506
#define	SYS_fchownat	507
#define	SYS_unlinkat	508
#define	SYS_faccessat	509
#define	SYS_mq_open	510
#define	SYS_mq_close	511
#define	SYS_mq_unlink	512
#define	SYS_mq_getattr	513
#define	SYS_mq_setattr	514
#define	SYS_mq_notify	515
#define	SYS_mq_send	516
#define	SYS_mq_receive	517
#define	SYS_mq_timedsend	518
#define	SYS_mq_timedreceive	519
#define	SYS_ioprio_set	520
#define	SYS_ioprio_get	521
#define	SYS_chroot_kernel	522
#define	SYS_renameat	523
#define	SYS_mkdirat	524
#define	SYS_mkfifoat	525
#define	SYS_mknodat	526
#define	SYS_readlinkat	527
#define	SYS_symlinkat	528
#define	SYS_swapoff	529
#define	SYS_vquotactl	530
#define	SYS_linkat	531
#define	SYS_eaccess	532
#define	SYS_lpathconf	533
#define	SYS_vmm_guest_ctl	534
#define	SYS_vmm_guest_sync_addr	535
#define	SYS_procctl	536
#define	SYS_chflagsat	537
#define	SYS_pipe2	538
#define	SYS_utimensat	539
#define	SYS_futimens	540
#define	SYS_MAXSYSCALL	541
