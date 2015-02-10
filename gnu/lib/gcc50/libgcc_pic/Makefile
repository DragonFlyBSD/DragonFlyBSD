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
CFLAGS+=	-fbuilding-libgcc
CFLAGS+=	-fno-stack-protector
CFLAGS+=	-fexceptions
CFLAGS+=	-DIN_GCC
CFLAGS+=	-DIN_LIBGCC2
CFLAGS+=	-DHAVE_CC_TLS
CFLAGS+=	-DSHARED

# FUNCS and SOFTFUNCS defined in Makefile.src
OBJS=		${FUNCS:S/$/.o/}
SRCS=		${SOFTFUNCS:S/$/.c/}

# LIB2ADDEH, gcc/Makefile
SRCS+=	unwind-dw2.c \
	unwind-dw2-fde-dip.c \
	unwind-sjlj.c \
	unwind-c.c \
	emutls.c

# generated sources
unwind.h: unwind-generic.h
	cp ${.ALLSRC} ${.TARGET}

gthr-default.h: gthr-posix.h
	cp ${.ALLSRC} ${.TARGET}

md-unwind-support.h: dragonfly-unwind.h
	cp ${.ALLSRC} ${.TARGET}

sfp-machine.h: ${GCCDIR}/libgcc/config/i386/sfp-machine.h
	cp ${.ALLSRC} ${.TARGET}

enable-execute-stack.c: enable-execute-stack-mprotect.c
	cp ${.ALLSRC} ${.TARGET}

auto-target.h: ${.CURDIR}/../libgcc/auto-target.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	unwind.h gthr-default.h sfp-machine.h md-unwind-support.h \
		enable-execute-stack.c auto-target.h

beforedepend:	unwind.h gthr-default.h sfp-machine.h md-unwind-support.h \
		enable-execute-stack.c auto-target.h

.include <bsd.lib.mk>
