# $FreeBSD: src/gnu/usr.bin/cc/Makefile.inc,v 1.63 2002/06/04 19:45:08 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/Attic/Makefile.inc,v 1.12 2004/06/16 01:14:07 dillon Exp $

BINDIR?=/usr/libexec/gcc3
MY_CCVER=gcc3
.include "../Makefile.inc"

# Sometimes this is .include'd several times...
.if !defined(__CC_MAKEFILE_INC__)
__CC_MAKEFILE_INC__= ${MFILE}

CFLAGS+=	-I${.CURDIR}/../cc_int -I${.OBJDIR}/../cc_int

GCCDIR=	${.CURDIR}/../../../../contrib/gcc-3.3

.include "Makefile.tgt"

# Machine description.
MD_FILE=	${GCCDIR}/gcc/config/${GCC_CPU}/${GCC_CPU}.md
target=		${TARGET_ARCH}-undermydesk-freebsd

CFLAGS+=	-DIN_GCC -DHAVE_CONFIG_H
CFLAGS+=	-DPREFIX=\"${TOOLS_PREFIX}/usr\"
CFLAGS+=	-DPREFIX1=\"${TOOLS_PREFIX}/usr\"
CFLAGS+=	-DPREFIX2=\"${USRDATA_PREFIX}/usr\"
#CFLAGS+=	-DWANT_COMPILER_INVARIANTS
CFLAGS+=	-DSTACK_PROTECTOR

# If building 64-bit longs for the i386, "_LARGE_LONG" should also be defined
# to get the proper sizes in limits.h
.if defined(LONG_TYPE_SIZE)
CFLAGS+=	-DLONG_TYPE_SIZE=${LONG_TYPE_SIZE}
.endif

.if ${TARGET_ARCH} != ${MACHINE_ARCH}
CFLAGS+=	-DCROSS_COMPILE
.endif

.if defined(WANT_FORCE_OPTIMIZATION_DOWNGRADE)
CFLAGS+= -DFORCE_OPTIMIZATION_DOWNGRADE=${WANT_FORCE_OPTIMIZATION_DOWNGRADE}
.endif

.if exists(${.OBJDIR}/../cc_tools)
CFLAGS+=	-I${.OBJDIR}/../cc_tools
.endif
CFLAGS+=	-I${.CURDIR}/../cc_tools
# This must go after the -I for cc_tools to resolve ambiguities for hash.h
# correctly.
CFLAGS+=	-I${GCCDIR} -I${GCCDIR}/include 
CFLAGS+=	-I${GCCDIR}/gcc -I${.OBJDIR}/../cc_int/config \
		-I${GCCDIR}/gcc/config


.if exists(${.OBJDIR}/../cc_int)
LIBCC_INT=	${.OBJDIR}/../cc_int/libcc_int.a
.else
LIBCC_INT=	${.CURDIR}/../cc_int/libcc_int.a
.endif

gcc_local.c : gcc.c
	cp ${.ALLSRC:M*c} ${.TARGET}.new
	patch ${.TARGET}.new ${.CURDIR}/../gcc.c.patch
	mv -f ${.TARGET}.new ${.TARGET}

version_local.c : version.c
	sed -e 's/\(const char version.*".*\) (/\1 [DragonFly] (propolice, /' < ${.ALLSRC:M*c} > ${.TARGET}.new
	mv -f ${.TARGET}.new ${.TARGET}

CLEANFILES+=	version_local.c gcc_local.c gcc_local.c.new.orig

.endif # !__CC_MAKEFILE_INC__
