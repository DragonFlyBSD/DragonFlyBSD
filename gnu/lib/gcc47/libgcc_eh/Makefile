.include "../Makefile.inc"
.PATH: ${GCCDIR}/libgcc
.PATH: ${GCCDIR}/libgcc/config/i386

LIB=		gcc_eh

CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${GCCDIR}/libgcc
CFLAGS+=	-I../csu
CFLAGS+=	-fbuilding-libgcc
CFLAGS+=	-fno-stack-protector
CFLAGS+=	-fexceptions
CFLAGS+=	-fvisibility=hidden
CFLAGS+=	-DIN_GCC
CFLAGS+=	-DIN_LIBGCC2
CFLAGS+=	-DHAVE_CC_TLS
CFLAGS+=	-DHIDE_EXPORTS

# LIB2ADDEH, gcc/Makefile
SRCS+=	unwind-dw2.c \
	unwind-dw2-fde-dip.c \
	unwind-sjlj.c \
	unwind-c.c \
	emutls.c

#generated sources
SRCS+=	unwind.h md-unwind-support.h gthr-default.h

unwind.h: unwind-generic.h
	cp ${.ALLSRC} ${.TARGET}

md-unwind-support.h: dragonfly-unwind.h
	cp ${.ALLSRC} ${.TARGET}

gthr-default.h: gthr-posix.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	unwind.h md-unwind-support.h gthr-default.h

.include <bsd.lib.mk>
