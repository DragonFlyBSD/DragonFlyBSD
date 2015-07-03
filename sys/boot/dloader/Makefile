# The DragonFly Loader
#
#
.if exists(../${MACHINE_PLATFORM}/Makefile.inc)
.include "../${MACHINE_PLATFORM}/Makefile.inc"
.endif

.PATH: ${.CURDIR}/${MACHINE_ARCH}
SRCS=	subs.c config.c cmds.c

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
LIB=		dloader
INTERNALLIB=	yes

stack_protector.o: ../../libkern/stack_protector.c

.include <bsd.lib.mk>
.endif

CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.CURDIR}/../common
