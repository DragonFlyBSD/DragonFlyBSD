# $DragonFly: src/gnu/usr.bin/cc34/Makefile.inc,v 1.2 2004/06/21 03:48:06 dillon Exp $

BINDIR?=	/usr/libexec/gcc34
.include "../Makefile.inc"

GCCDIR=		${.CURDIR}/${TOP_PREFIX}/../../../../contrib/gcc-3.4
OTOPDIR=	${.OBJDIR}/${TOP_PREFIX}/..
STOPDIR=	${.CURDIR}/${TOP_PREFIX}/..

TOOLDIR=	${OTOPDIR}/cc_tools
SRC_TOOLDIR=	${STOPDIR}/cc_tools

CFLAGS+=	-I${OTOPDIR}/cc_prep -I${STOPDIR}/cc_prep
CFLAGS+=	-I${GCCDIR}/gcc -I${GCCDIR}/gcc/config -I${GCCDIR}/include

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
.PATH:	${OTOPDIR}/cc_prep ${STOPDIR}/cc_prep ${GCCDIR}/gcc ${GCCDIR}/libiberty
.endif
