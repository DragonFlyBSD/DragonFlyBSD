#	@(#)Makefile.inc	8.3 (Berkeley) 10/24/94
# $FreeBSD: src/lib/libc/sys/Makefile.inc,v 1.75.2.7 2003/04/22 17:31:18 trhodes Exp $

# sys sources
.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/sys ${.CURDIR}/../libc/sys

CMAPS+=	${.CURDIR}/sys/Symbol.map

# Include the generated makefile containing the *complete* list
# of syscall names in MIASM.
.include "${.CURDIR}/../../sys/sys/syscall.mk"

# Include machine dependent definitions.
#
# MDASM names override the default syscall names in MIASM.
# NOASM will prevent the default syscall code from being generated.
#
.include "${.CURDIR}/../libc/${MACHINE_ARCH}/sys/Makefile.inc"

SRCS+=	fork.c

# Sources common to both syscall interfaces:
SRCS+=	ftruncate.c lseek.c mmap.c truncate.c
SRCS+=	pread.c pwrite.c preadv.c pwritev.c
SRCS+=	sched_getaffinity.c sched_getcpu.c sched_setaffinity.c

# Add machine dependent asm sources:
SRCS+=	${MDASM}

# Look through the complete list of syscalls (MIASM) for names that are
# not defined with machine dependent implementations (MDASM) and are
# not declared for no generation of default code (NOASM).
.for _asm in ${MIASM}
.if (${MDASM:R:M${_asm:R}} == "")
.if (${NOASM:R:M${_asm:R}} == "")
ASM+=	${_asm}
.endif
.endif
.endfor

OBJS+=	${ASM} ${PSEUDO}

SASM=	${ASM:S/.o/.S/}

SPSEUDO= ${PSEUDO:S/.o/.S/}

SRCS+=	${SASM} ${SPSEUDO}

# Generated files
CLEANFILES+=	${SASM} ${SPSEUDO}

# Generate the syscalls source-files
NOTE_GNU_STACK='.section .note.GNU-stack,"",%%progbits\n'

${SASM}:
	printf '#include "SYS.h"\nRSYSCALL(${.PREFIX})\n' > ${.TARGET}
	printf  ${NOTE_GNU_STACK} >> ${.TARGET}

${SPSEUDO}:
	printf '#include "SYS.h"\nPSEUDO(${.PREFIX:S/_//})\n' \
	    > ${.TARGET}
	printf  ${NOTE_GNU_STACK} >> ${.TARGET}

MAN+=	_exit.2 accept.2 access.2 acct.2 adjtime.2 bind.2 \
	sys_checkpoint.2 chdir.2 chflags.2 \
	chmod.2 chown.2 chroot.2 clock_gettime.2 close.2 closefrom.2 \
	connect.2 dup.2 exec_sys_register.2 execve.2 extattr.2 extexit.2 \
	fcntl.2 fhopen.2 flock.2 fork.2 fsync.2 \
	getdirentries.2 getdtablesize.2 \
	getfh.2 getfsstat.2 getgid.2 getgroups.2 getitimer.2 getlogin.2 \
	getpeername.2 getpgrp.2 getpid.2 getpriority.2 getrandom.2 \
	getrlimit.2 getrusage.2 getsid.2 getsockname.2 \
	getsockopt.2 gettimeofday.2 getuid.2 \
	intro.2 ioctl.2 ioprio_get.2 issetugid.2 \
	jail.2 jail_attach.2 kenv.2 kill.2 \
	kldfind.2 kldfirstmod.2 kldload.2 kldnext.2 kldstat.2 kldsym.2 \
	kldunload.2 ktrace.2 kqueue.2 link.2 listen.2 lseek.2 \
	lwp_create.2 lwp_gettid.2 lwp_kill.2 lwp_setname.2 lwp_setaffinity.2 \
	madvise.2 mincore.2 minherit.2 mkdir.2 mkfifo.2 mknod.2 mlock.2 \
	mlockall.2 mmap.2 \
	modfind.2 modnext.2 modstat.2 \
	mount.2 mountctl.2 mprotect.2 \
	msgctl.2 msgget.2 msgrcv.2 msgsnd.2 \
	msync.2 munmap.2 nanosleep.2 \
	nfssvc.2 ntp_adjtime.2 ntp_gettime.2 open.2 \
	pathconf.2 pipe.2 poll.2 posix_fallocate.2 \
	procctl.2 profil.2 pselect.2 ptrace.2 \
	quotactl.2 \
	read.2 readlink.2 reboot.2 recv.2 rename.2 revoke.2 rfork.2 rmdir.2 \
	rtprio.2 __realpath.2 \
	sbrk.2 sched_getcpu.2 sched_get_priority_max.2 sched_setaffinity.2 \
	sched_setparam.2 sched_setscheduler.2 sched_yield.2 \
	select.2 semctl.2 semget.2 semop.2 send.2 sendfile.2 \
	setgroups.2 setpgid.2 setregid.2 setresuid.2 setreuid.2 setsid.2 \
	setuid.2 shmat.2 shmctl.2 shmget.2 shutdown.2 \
	syscap_get.2 \
	sigaction.2 sigaltstack.2 sigpending.2 sigprocmask.2 sigreturn.2 \
	sigsuspend.2 sigwaitinfo.2 socket.2 socketpair.2 \
	stat.2 statfs.2 statvfs.2 swapon.2 \
	symlink.2 sync.2 sysarch.2 syscall.2 \
	truncate.2 tls.2 umask.2 umtx.2 undelete.2 \
	unlink.2 unlinkat.2 utimensat.2 utimes.2 utrace.2 usched_set.2 \
	uuidgen.2 \
	varsym.2 vfork.2 vquotactl.2 wait.2 write.2

MLINKS+=accept.2 accept4.2
MLINKS+=access.2 eaccess.2 \
	access.2 faccessat.2
MLINKS+=chdir.2 fchdir.2
MLINKS+=chflags.2 chflagsat.2 \
	chflags.2 fchflags.2 \
	chflags.2 lchflags.2
MLINKS+=chmod.2 fchmod.2 \
	chmod.2 fchmodat.2 \
	chmod.2 lchmod.2
MLINKS+=chown.2 fchown.2 \
	chown.2 fchownat.2 \
	chown.2 lchown.2
MLINKS+=chroot.2 chroot_kernel.2
MLINKS+=clock_gettime.2 clock_getres.2 \
	clock_gettime.2 clock_settime.2
MLINKS+=dup.2 dup2.2
MLINKS+=exec_sys_register.2 exec_sys_unregister.2
MLINKS+=execve.2 fexecve.2
MLINKS+=extattr.2 extattr_delete_file.2 \
	extattr.2 extattr_get_file.2 \
	extattr.2 extattr_set_file.2
MLINKS+=fhopen.2 fhstat.2 \
	fhopen.2 fhstatfs.2 \
	fhopen.2 fhstatvfs.2
MLINKS+=fsync.2 fdatasync.2
MLINKS+=getdirentries.2 _DIRENT_DIRSIZ.2 \
	getdirentries.2 _DIRENT_NEXT.2 \
	getdirentries.2 getdents.2
MLINKS+=getfsstat.2 getvfsstat.2
MLINKS+=getgid.2 getegid.2
MLINKS+=getitimer.2 setitimer.2
MLINKS+=getlogin.2 getlogin_r.3 \
	getlogin.2 setlogin.2
MLINKS+=getpgrp.2 getpgid.2
MLINKS+=getpid.2 getppid.2
MLINKS+=getpriority.2 setpriority.2
MLINKS+=getrlimit.2 setrlimit.2
MLINKS+=getsockopt.2 setsockopt.2
MLINKS+=gettimeofday.2 settimeofday.2
MLINKS+=getuid.2 geteuid.2
MLINKS+=intro.2 errno.2
MLINKS+=ioprio_get.2 ioprio_set.2
MLINKS+=kqueue.2 EV_SET.2 \
	kqueue.2 kevent.2
MLINKS+=link.2 linkat.2
MLINKS+=lseek.2 seek.2
MLINKS+=lwp_create.2 lwp_create2.2
MLINKS+=lwp_setaffinity.2 lwp_getaffinity.2
MLINKS+=lwp_setname.2 lwp_getname.2
MLINKS+=madvise.2 mcontrol.2 \
	madvise.2 posix_madvise.3
MLINKS+=mkdir.2 mkdirat.2
MLINKS+=mkfifo.2 mkfifoat.2
MLINKS+=mknod.2 mknodat.2
MLINKS+=mlock.2 munlock.2
MLINKS+=mlockall.2 munlockall.2
MLINKS+=modnext.2 modfnext.2
MLINKS+=mount.2 unmount.2
MLINKS+=msgctl.2 msgctl.3
MLINKS+=msgget.2 msgget.3
MLINKS+=msgrcv.2 msgrcv.3
MLINKS+=msgsnd.2 msgsnd.3
MLINKS+=nanosleep.2 clock_nanosleep.2
MLINKS+=open.2 openat.2
MLINKS+=pathconf.2 fpathconf.2 \
	pathconf.2 lpathconf.2
MLINKS+=pipe.2 pipe2.2
MLINKS+=poll.2 ppoll.2
MLINKS+=read.2 extpread.2 \
	read.2 extpreadv.2 \
	read.2 pread.2 \
	read.2 preadv.2 \
	read.2 readv.2
MLINKS+=readlink.2 readlinkat.2
MLINKS+=recv.2 recvfrom.2 \
	recv.2 recvmsg.2
MLINKS+=rename.2 renameat.2
MLINKS+=rtprio.2 lwp_rtprio.2
MLINKS+=sched_get_priority_max.2 sched_get_priority_min.2 \
	sched_get_priority_max.2 sched_rr_get_interval.2
MLINKS+=sched_setaffinity.2 CPU_AND.3 \
	sched_setaffinity.2 CPU_CLR.3 \
	sched_setaffinity.2 CPU_COUNT.3 \
	sched_setaffinity.2 CPU_EQUAL.3 \
	sched_setaffinity.2 CPU_ISSET.3 \
	sched_setaffinity.2 CPU_OR.3 \
	sched_setaffinity.2 CPU_SET.3 \
	sched_setaffinity.2 CPU_XOR.3 \
	sched_setaffinity.2 CPU_ZERO.3 \
	sched_setaffinity.2 sched_getaffinity.2
MLINKS+=sched_setparam.2 sched_getparam.2
MLINKS+=sched_setscheduler.2 sched_getscheduler.2
MLINKS+=select.2 FD_CLR.2 \
	select.2 FD_ISSET.2 \
	select.2 FD_SET.2 \
	select.2 FD_ZERO.2
MLINKS+=semctl.2 semctl.3
MLINKS+=semget.2 semget.3
MLINKS+=semop.2 semop.3
MLINKS+=send.2 sendmsg.2 \
	send.2 sendto.2
MLINKS+=setpgid.2 setpgrp.3
MLINKS+=setresuid.2 getresgid.2 \
	setresuid.2 getresuid.2 \
	setresuid.2 setresgid.2
MLINKS+=setuid.2 setegid.2 \
	setuid.2 seteuid.2 \
	setuid.2 setgid.2
MLINKS+=shmat.2 shmat.3 \
	shmat.2 shmdt.2
MLINKS+=shmctl.2 shmctl.3
MLINKS+=shmget.2 shmget.3
MLINKS+=sigwaitinfo.2 sigtimedwait.2
MLINKS+=stat.2 fstat.2 \
	stat.2 fstatat.2 \
	stat.2 lstat.2
MLINKS+=statfs.2 fstatfs.2
MLINKS+=statvfs.2 fstatvfs.2
MLINKS+=symlink.2 symlinkat.2
MLINKS+=syscall.2 __syscall.2
MLINKS+=syscap_get.2 syscap_set.2
MLINKS+=swapon.2 swapoff.2
MLINKS+=tls.2 get_tls_area.2 \
	tls.2 set_tls_area.2
MLINKS+=truncate.2 ftruncate.2
MLINKS+=umtx.2 umtx_sleep.2 \
	umtx.2 umtx_wakeup.2
MLINKS+=utimensat.2 futimens.2
MLINKS+=utimes.2 futimes.2 \
	utimes.2 lutimes.2 \
	utimes.2 futimesat.2
MLINKS+=varsym.2 varsym_get.2 \
	varsym.2 varsym_list.2 \
	varsym.2 varsym_set.2
MLINKS+=wait.2 wait3.2 \
	wait.2 wait4.2 \
	wait.2 wait6.2 \
	wait.2 waitid.2 \
	wait.2 waitpid.2
MLINKS+=write.2 extpwrite.2 \
	write.2 extpwritev.2 \
	write.2 pwrite.2 \
	write.2 pwritev.2 \
	write.2 writev.2
