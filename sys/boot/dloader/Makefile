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

.if defined(REALLY_X86_64)
CFLAGS+=	-I.
.endif

CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.CURDIR}/../common

.if defined(REALLY_X86_64)
${SRCS:M*.c:R:S/$/.o/g}: machine

beforedepend ${OBJS}: machine

machine:
	${LN} -sf ${.CURDIR}/../../i386/include machine

CLEANFILES+=	machine
.endif
