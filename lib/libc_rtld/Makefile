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
CFLAGS+=-Wno-sign-compare
CFLAGS+=-Wno-unused-variable
CFLAGS+=-Wno-cast-qual
CFLAGS+=-Dsnprintf=rtld_snprintf

PRECIOUSLIB=	yes

# libc_rtld is deliberately assembled from a small, explicit closure.  Do not
# include libc/Makefile.inc here: that would pull in the full libc, including
# the thread, upmap, locale, stdio, and resolver implementations.

.PATH: ${.CURDIR}/../libc/gen
.PATH: ${.CURDIR}/../libc/stdlib
.PATH: ${.CURDIR}/../libc/string
.PATH: ${.CURDIR}/../libc/sys
.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/gen
.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/string
.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/sys

SRCS=	./rtld_libc.c
SRCS+=	./closedir.c ./errlst.c ./getenv.c ./getosreldate.c
SRCS+=	./getprogname.c ./opendir.c ./readdir.c ./seekdir.c ./sysctl.c
SRCS+=	./telldir.c ./strtok.c ./uname.c ./realpath.c
SRCS+=	./sigsetops.c
SRCS+=	./strdup.c ./strlen.c ./memmove.S
SRCS+=	./bzero.S ./memcmp.S ./memcpy.S ./memset.S ./strcat.S ./strcmp.S ./strcpy.S
SRCS+=	./stpncpy.c ./strchr.c ./strcspn.c ./strlcat.c ./strlcpy.c ./strncmp.c
SRCS+=	./strncpy.c ./strrchr.c ./strspn.c ./strstr.c
SRCS+=	./lseek.c ./mmap.c
SRCS+=	./cerror.S ./sigsetjmp.S

SYSASM=	__getcwd.S __realpath.S __syscall.S __sysctl.S
SYSASM+=	access.S close.S exec_sys_register.S exec_sys_unregister.S
SYSASM+=	fcntl.S fstat.S fstatfs.S getdirentries.S getpid.S
SYSASM+=	issetugid.S lstat.S madvise.S mprotect.S munmap.S
SYSASM+=	open.S openat.S read.S readlink.S stat.S sigprocmask.S
SYSASM+=	umtx_sleep.S umtx_wakeup.S utrace.S write.S
SRCS+=	${SYSASM:S,^,./,}
CLEANFILES+=	${SYSASM}

PSEUDOASM=	_exit.S
SRCS+=	${PSEUDOASM:S,^,./,}
CLEANFILES+=	${PSEUDOASM}

${SYSASM:S,^,./,}:
	printf '#include "SYS.h"\nRSYSCALL(${.TARGET:T:R})\n' > ${.TARGET}
	printf ${NOTE_GNU_STACK} >> ${.TARGET}

${PSEUDOASM:S,^,./,}:
	printf '#include "SYS.h"\nPSEUDO(${.TARGET:T:R:S/_//})\n' > ${.TARGET}
	printf ${NOTE_GNU_STACK} >> ${.TARGET}

NOTE_GNU_STACK='.section .note.GNU-stack,"",%%progbits\n'

.include <bsd.lib.mk>

# Disable warnings in contributed sources.
CWARNFLAGS:=	${.IMPSRC:N*dtoa*.c:C/^.+$/${CWARNFLAGS}/:C/^$/-w/}
CWARNFLAGS+=	-Wno-cast-qual -Wno-sign-compare -Wno-unused-variable
