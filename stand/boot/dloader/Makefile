# The DragonFly Loader

SRCS=	subs.c cmds.c

WORLD_CCOPTLEVEL=	1	# Force -O1, -O2 implodes

# Disable stack protector
CFLAGS+=	-fno-stack-protector

# For i386, dloader32/Makefile takes care of the extra CFLAGS
.if ${MACHINE_ARCH} != "i386"
CFLAGS+=	-fPIC -mno-red-zone
CFLAGS+=	-ffreestanding
CFLAGS+=	-D_STANDALONE
.endif

.if ${MACHINE_ARCH} == "x86_64"
# From: sys/platform/*/conf/kern.mk
CFLAGS+=	-mno-mmx -mno-sse -msoft-float -mno-fp-ret-in-387
.endif

.if HAVE_PNP
CFLAGS+=	-DHAVE_PNP
.endif
.ifmake testmain
CFLAGS+=	-DTESTMAIN -D_TESTMAIN
SRCS+=		testmain.c
PROG=		testmain
OBJS+=		rel_open.o

rel_open.o: ../common/rel_open.c

.include <bsd.prog.mk>
.else
OBJS+=		stack_protector.o
LIB?=		dloader
INTERNALLIB=	yes

stack_protector.o: ../../../sys/libkern/stack_protector.c

.include <bsd.lib.mk>
.endif

CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.CURDIR}/../common
CFLAGS+=	-I${.CURDIR}/../../lib
