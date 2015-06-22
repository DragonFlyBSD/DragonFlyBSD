GCC_NO_PATH=	yes
GCC_NO_LIBS=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libbacktrace
.PATH: ${GCCDIR}/libgcc

LIB=		backtrace
INTERNALLIB=	YES

CFLAGS+=	-I${GCCDIR}/libbacktrace

SRCS=		atomic.c \
		dwarf.c \
		fileline.c \
		posix.c \
		print.c \
		sort.c \
		state.c \
		backtrace.c \
		simple.c \
		elf.c \
		mmapio.c \
		mmap.c

backtrace.o:	unwind.h

unwind.h: unwind-generic.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES=	unwind.h

.include <bsd.lib.mk>
