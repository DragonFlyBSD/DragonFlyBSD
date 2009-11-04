# $FreeBSD: src/sys/boot/ficl/Makefile,v 1.35 2003/06/30 19:08:49 ru Exp $
# $DragonFly: src/sys/boot/ficl/Makefile,v 1.10 2008/03/30 18:11:58 swildner Exp $
#
.if exists(../${MACHINE_PLATFORM}/Makefile.inc)
.include "../${MACHINE_PLATFORM}/Makefile.inc"
.endif

.PATH: ${.CURDIR}/${MACHINE_ARCH}
BASE_SRCS=	dict.c ficl.c fileaccess.c float.c loader.c math64.c \
		prefix.c search.c stack.c tools.c vm.c words.c

SRCS=		${BASE_SRCS} sysdep.c softcore.c

CLEANFILES=	softcore.c testmain testmain.o
CFLAGS+=	-ffreestanding
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
LIB=		ficl
INTERNALLIB=	yes

stack_protector.o: ../../libkern/stack_protector.c

.include <bsd.lib.mk>
.endif

# Standard softwords
.PATH: ${.CURDIR}/softwords
SOFTWORDS=	softcore.fr jhlocal.fr marker.fr freebsd.fr ficllocal.fr \
		ifbrack.fr
# Optional OO extension softwords
#SOFTWORDS+=	oo.fr classes.fr

.if defined(REALLY_X86_64)
CFLAGS+=	-m32 -I.
.endif

CFLAGS+=	-I${.CURDIR} -I${.CURDIR}/${MACHINE_ARCH} -I${.CURDIR}/../common

softcore.c: ${SOFTWORDS} softcore.awk
	(cd ${.CURDIR}/softwords; cat ${SOFTWORDS} \
	    | awk -f softcore.awk -v datestamp="`LC_ALL=C date`") > ${.TARGET}

.if defined(REALLY_X86_64)
${SRCS:M*.c:R:S/$/.o/g}: machine

beforedepend ${OBJS}: machine

machine:
	${LN} -sf ${.CURDIR}/../../i386/include machine

CLEANFILES+=	machine
.endif
