LIB=	c_rtld
INTERNALLIB=
INSTALL_PIC_ARCHIVE=
NOMAN=
NOPROFILE=
STATIC_LOCALES=
CFLAGS+=-I${.CURDIR}
CFLAGS+=-I${.CURDIR}/../libc/include
CFLAGS+=-I${.CURDIR}/../libc/resolv
CFLAGS+=-I${.CURDIR}/../libc/rpc
CFLAGS+=-I${.CURDIR}/../../include
CFLAGS+=-I${.CURDIR}/../libc/${MACHINE_ARCH}
CFLAGS+=-I${.OBJDIR}
CFLAGS+=-D__thread=
CFLAGS+=-D__LIBC -D__LIBC_RTLD
CFLAGS+=-Dsnprintf=rtld_snprintf

PRECIOUSLIB=	yes

# libc_rtld is deliberately assembled from a small, explicit closure.  Do not
# include libc/Makefile.inc here: that would pull in the full libc, including
# the thread, upmap, locale, stdio, and resolver implementations.

SRCS=	rtld_libc.c

.PATH: ${.CURDIR}/../libc/gen
SRCS+=	closedir.c errlst.c getosreldate.c getprogname.c opendir.c
SRCS+=	readdir.c seekdir.c sysctl.c telldir.c uname.c sigsetops.c

.PATH: ${.CURDIR}/../libc/stdlib
SRCS+=	getenv.c realpath.c

.PATH: ${.CURDIR}/../libc/string
SRCS+=	strtok.c strdup.c strlen.c stpncpy.c strchr.c strcspn.c strlcat.c strlcpy.c
SRCS+=	strncmp.c strncpy.c strrchr.c strspn.c strstr.c

.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/string
SRCS+=	memmove.S bzero.S memcmp.S memcpy.S memset.S
SRCS+=	strcat.S strcmp.S strcpy.S

.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/gen
SRCS+=	sigsetjmp.S

.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/sys
SRCS+=	cerror.S

.PATH: ${.CURDIR}/../libc/sys
SRCS+=	lseek.c mmap.c

SYSASM=	__getcwd.S __realpath.S __syscall.S __sysctl.S
SYSASM+=	access.S close.S exec_sys_register.S exec_sys_unregister.S
SYSASM+=	fcntl.S fstat.S fstatfs.S getdirentries.S getpid.S
SYSASM+=	issetugid.S lstat.S madvise.S mprotect.S munmap.S
SYSASM+=	open.S openat.S read.S readlink.S stat.S sigprocmask.S
SYSASM+=	umtx_sleep.S umtx_wakeup.S utrace.S write.S
SRCS+=	${SYSASM}
CLEANFILES+=	${SYSASM}

PSEUDOASM=	_exit.S
SRCS+=	${PSEUDOASM}
CLEANFILES+=	${PSEUDOASM}

${SYSASM}:
	printf '#include "SYS.h"\nRSYSCALL(${.TARGET:T:R})\n' > ${.TARGET}
	printf ${NOTE_GNU_STACK} >> ${.TARGET}

${PSEUDOASM}:
	printf '#include "SYS.h"\nPSEUDO(${.TARGET:T:R:S/_//})\n' > ${.TARGET}
	printf ${NOTE_GNU_STACK} >> ${.TARGET}

NOTE_GNU_STACK='.section .note.GNU-stack,"",%%progbits\n'

.include <bsd.lib.mk>
