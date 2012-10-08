.include "../Makefile.inc"
.include "../libgcc/Makefile.src"
.PATH: ${GCCDIR}/libgcc
.PATH: ${GCCDIR}/libgcc/soft-fp
.PATH: ${GCCDIR}/libgcc/config/i386

LIB=	gcc

INSTALL_PIC_ARCHIVE=	yes
NOPROFILE=		yes
NOINSTALLLIB=		yes

CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${GCCDIR}/libgcc
CFLAGS+=	-I../csu
CFLAGS+=	-DIN_LIBGCC2
CFLAGS+=	-DIN_GCC
CFLAGS+=	-DSHARED
CFLAGS+=	-DHAVE_CC_TLS
CFLAGS+=	-fbuilding-libgcc
CFLAGS+=	-fno-stack-protector
CFLAGS+=	-nodefaultlibs

# FUNCS and SOFTFUNCS defined in Makefile.src
SRCS+=		${FUNCS:S/$/.c/} ${SOFTFUNCS:S/$/.c/}
CLEANFILES+=	${FUNCS:S/$/.c/}

# LIB2ADDEH, gcc/Makefile
SRCS+=	unwind-dw2.c \
	unwind-dw2-fde-dip.c \
	unwind-sjlj.c \
	unwind-c.c \
	emutls.c

# generated sources
SRCS+=	unwind.h gthr-default.h sfp-machine.h md-unwind-support.h

unwind.h: unwind-generic.h
	cp ${.ALLSRC} ${.TARGET}

gthr-default.h: gthr-posix.h
	cp ${.ALLSRC} ${.TARGET}

md-unwind-support.h: dragonfly-unwind.h
	cp ${.ALLSRC} ${.TARGET}

sfp-machine.h: ${GCCDIR}/libgcc/config/i386/sfp-machine.h
	cp ${.ALLSRC} ${.TARGET}

enable-execute-stack.c: enable-execute-stack-freebsd.c
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	unwind.h gthr-default.h sfp-machine.h md-unwind-support.h \
		enable-execute-stack.c

.include <bsd.lib.mk>
