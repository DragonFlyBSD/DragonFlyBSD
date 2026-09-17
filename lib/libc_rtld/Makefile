LIB=	c_rtld

INTERNALLIB=
INSTALL_PIC_ARCHIVE=
NOMAN=
NOPROFILE=
STATIC_LOCALES=
PRECIOUSLIB=	yes

WARNS=	2

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

# libc_rtld is deliberately assembled from a small, explicit libc subset
# to support libexec/rtld-elf.

SRCS=	rtld_libc.c

.PATH: ${.CURDIR}/../libc/gen
SRCS+=	closedir.c errlst.c getosreldate.c getprogname.c opendir.c \
	readdir.c seekdir.c sysctl.c telldir.c uname.c sigsetops.c

.PATH: ${.CURDIR}/../libc/stdlib
SRCS+=	getenv.c realpath.c

.PATH: ${.CURDIR}/../libc/string
SRCS+=	strtok.c strdup.c strlen.c stpncpy.c strchr.c strcspn.c strlcat.c \
	strlcpy.c strncmp.c strncpy.c strrchr.c strspn.c strstr.c

.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/string
SRCS+=	memmove.S bzero.S memcmp.S memcpy.S memset.S \
	strcat.S strcmp.S strcpy.S

.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/gen
SRCS+=	sigsetjmp.S

.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/sys
SRCS+=	cerror.S

.PATH: ${.CURDIR}/../libc/sys
SRCS+=	lseek.c mmap.c

SYSASM=		__getcwd.S __realpath.S __syscall.S __sysctl.S \
		access.S close.S exec_sys_register.S exec_sys_unregister.S \
		fcntl.S fstat.S fstatfs.S getdirentries.S getpid.S \
		issetugid.S lstat.S madvise.S mprotect.S munmap.S \
		open.S openat.S read.S readlink.S stat.S sigprocmask.S \
		umtx_sleep.S umtx_wakeup.S utrace.S write.S
PSEUDOASM=	_exit.S

SRCS+=		${SYSASM} ${PSEUDOASM}
CLEANFILES+=	${SYSASM} ${PSEUDOASM}

# Generate the syscalls source-files
NOTE_GNU_STACK=	'.section .note.GNU-stack,"",%%progbits\n'

${SYSASM}:
	printf '#include "SYS.h"\nRSYSCALL(${.PREFIX})\n' > ${.TARGET}
	printf ${NOTE_GNU_STACK} >> ${.TARGET}

${PSEUDOASM}:
	printf '#include "SYS.h"\nPSEUDO(${.PREFIX:S/_//})\n' > ${.TARGET}
	printf ${NOTE_GNU_STACK} >> ${.TARGET}

.include <bsd.lib.mk>
