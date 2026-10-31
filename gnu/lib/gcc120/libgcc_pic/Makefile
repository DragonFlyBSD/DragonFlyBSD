.include "../Makefile.inc"
.include "../libgcc/Makefile.src"

LIB=		gcc_pic

NOPROFILE=	YES
# Mimic libgcc_s.so, this is a special case
NOSHARED=	YES

WARNS=		1

CFLAGS+=	-fpic

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

# XXX: workaround for ld.gold issue
CFLAGS+=	-D_AVOID_WEAKREF

OBJS=		${LIB2OBJS}
SRCS=		${LIB2ADD:T} ${LIB2ADDEH:T}

FLAGS_GROUPS=		sse2
sse2_FLAGS=		-msse2
sse2_FLAGS_FILES=	${sse2_files}

# generated sources
SRCS+=		unwind.h md-unwind-support.h gthr-default.h sfp-machine.h

SRCS+=		auto-target.h
CLEANFILES+=	auto-target.h
auto-target.h: ${.CURDIR}/../libgcc/auto-target.h
	cp ${.ALLSRC} ${.TARGET}

.include <bsd.lib.mk>
