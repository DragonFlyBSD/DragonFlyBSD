LIB=	c_rtld
INTERNALLIB=
INSTALL_PIC_ARCHIVE=
NOPROFILE=
STATIC_LOCALES=
CFLAGS+=-I${.CURDIR}/../libc/include
CFLAGS+=-I${.CURDIR}/../libc/resolv
CFLAGS+=-I${.CURDIR}/../libc/rpc
CFLAGS+=-I${.CURDIR}/../../include
CFLAGS+=-I${.CURDIR}/../libc/${MACHINE_ARCH}
CFLAGS+=-I${.OBJDIR}
CFLAGS+=-D__thread=
CFLAGS+=-D__LIBC_RTLD

PRECIOUSLIB=	yes

#
# This is a list of syscalls that are renamed as __sys_{syscall}
# so that libc_r can provide replacement functions.
#
HIDDEN_SYSCALLS= aio_suspend.o accept.o bind.o close.o connect.o dup.o dup2.o \
	execve.o fchflags.o fchmod.o fchown.o fcntl.o \
	flock.o fpathconf.o fstat.o fstatfs.o fsync.o getdirentries.o \
	getlogin.o getpeername.o getsockname.o getsockopt.o ioctl.o \
	kevent.o kqueue.o listen.o \
	msync.o nanosleep.o open.o poll.o read.o readv.o recvfrom.o \
	recvmsg.o sched_yield.o select.o sendfile.o sendmsg.o sendto.o \
	setsockopt.o shutdown.o sigaction.o \
	sigpending.o sigprocmask.o \
	sigsuspend.o sigtimedwait.o sigwaitinfo.o socket.o \
	socketpair.o wait4.o write.o writev.o

.include "${.CURDIR}/../libc/Makefile.inc"

.PATH:  ${.CURDIR}/../libc/inet
.PATH:  ${.CURDIR}/../libc/isc
.PATH:  ${.CURDIR}/../libc/nameser
.PATH:  ${.CURDIR}/../libc/resolv

.include <bsd.lib.mk>

# Disable warnings in contributed sources.
CWARNFLAGS:=	${.IMPSRC:N*dtoa*.c:C/^.+$/${CWARNFLAGS}/:C/^$/-w/}
