# $DragonFly: src/gnu/usr.bin/cc41/Makefile.inc,v 1.2 2007/01/20 03:51:33 corecode Exp $

.if !target(__<cc.Makefile.inc>__)
__<cc.Makefile.inc>__:

BINDIR?=	/usr/libexec/gcc41
.include "../Makefile.inc"

GCCDIR=		${.CURDIR}/${TOP_PREFIX}/../../../../contrib/gcc-4.1
OTOPDIR=	${.OBJDIR}/${TOP_PREFIX}/..
STOPDIR=	${.CURDIR}/${TOP_PREFIX}/..

TOOLDIR=	${OTOPDIR}/cc_tools
SRC_TOOLDIR=	${STOPDIR}/cc_tools

CSTD?=		gnu89

CFLAGS+=	-I${OTOPDIR}/cc_prep -I${STOPDIR}/cc_prep -I${STOPDIR}/cc_prep/config
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

LIBBACKEND=	${.OBJDIR}/../libbackend/libbackend.a
LIBCPP=		${.OBJDIR}/../libcpp/libcpp.a

.if !defined(GCC_NO_LIBERTY)
LDADD+=		${.OBJDIR}/../libiberty/libiberty.a
DPADD+=		${.OBJDIR}/../libiberty/libiberty.a
.endif

.endif
