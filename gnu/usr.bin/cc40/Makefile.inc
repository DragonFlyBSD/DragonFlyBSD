# $DragonFly: src/gnu/usr.bin/cc40/Attic/Makefile.inc,v 1.1 2005/06/05 22:43:19 corecode Exp $

BINDIR?=	/usr/libexec/gcc40
.include "../Makefile.inc"

GCCDIR=		${.CURDIR}/${TOP_PREFIX}/../../../../contrib/gcc-4.0
OTOPDIR=	${.OBJDIR}/${TOP_PREFIX}/..
STOPDIR=	${.CURDIR}/${TOP_PREFIX}/..

TOOLDIR=	${OTOPDIR}/cc_tools
SRC_TOOLDIR=	${STOPDIR}/cc_tools

CFLAGS+=	-I${OTOPDIR}/cc_prep -I${STOPDIR}/cc_prep
CFLAGS+=	-I${GCCDIR}/gcc -I${GCCDIR}/gcc/config -I${GCCDIR}/include
CFLAGS+=	-I${GCCDIR}/libcpp/include

CFLAGS+=	-DIN_GCC -DHAVE_CONFIG_H
CFLAGS+=	-DPREFIX=\"${TOOLS_PREFIX}/usr\"
CFLAGS+=	-DPREFIX1=\"${TOOLS_PREFIX}/usr\"
CFLAGS+=	-DPREFIX2=\"${USRDATA_PREFIX}/usr\"

.include "Makefile.tgt"

.if ${TARGET_ARCH} != ${MACHINE_ARCH}
CFLAGS+=	-DCROSS_COMPILE
.endif

MD_FILE=	${GCCDIR}/gcc/config/${GCC_CPU}/${GCC_CPU}.md

.if defined(GCC_LANG_DIR)
.PATH:	${GCCDIR}/${GCC_LANG_DIR}
.endif

.if !defined(GCC_NO_PATH)
.PATH:	${OTOPDIR}/cc_prep ${STOPDIR}/cc_prep ${GCCDIR}/gcc
.endif

.if !defined(GCC_NO_LIBERTY)
LDADD+=	-L../libiberty -liberty
.endif
