# $FreeBSD: src/usr.bin/objformat/Makefile,v 1.15 1999/12/24 15:18:19 green Exp $
# $DragonFly: src/usr.bin/objformat/Makefile,v 1.6 2004/10/26 11:27:46 joerg Exp $

PROG=		objformat
CFLAGS+=	-Wall
NOSHARED?=	YES
.if defined(TARGET_ARCH)
CFLAGS+=	-DTARGET_ARCH="${TARGET_ARCH}"
.endif

.if defined(CCVER_DEFAULT)
CFLAGS+=	-DCCVER_DEFAULT="\"${CCVER_DEFAULT}\""
.endif
.if defined(BINUTILSVER_DEFAULT)
CFLAGS+=	-DBINUTILSVER_DEFAULT="\"${BINUTILSVER_DEFAULT}\""
.endif
# These will exec /usr/libexec/${BINUTILSVER}/<objformat>/<name>
#
LINKS+=	${BINDIR}/objformat ${BINDIR}/addr2line
LINKS+=	${BINDIR}/objformat ${BINDIR}/ar
LINKS+=	${BINDIR}/objformat ${BINDIR}/as
LINKS+=	${BINDIR}/objformat ${BINDIR}/gasp
LINKS+=	${BINDIR}/objformat ${BINDIR}/gdb
LINKS+=	${BINDIR}/objformat ${BINDIR}/ld
LINKS+=	${BINDIR}/objformat ${BINDIR}/nm
LINKS+=	${BINDIR}/objformat ${BINDIR}/objcopy
LINKS+=	${BINDIR}/objformat ${BINDIR}/objdump
LINKS+=	${BINDIR}/objformat ${BINDIR}/ranlib
LINKS+=	${BINDIR}/objformat ${BINDIR}/size
LINKS+=	${BINDIR}/objformat ${BINDIR}/strings
LINKS+=	${BINDIR}/objformat ${BINDIR}/strip

# These will exec /usr/bin/${CCVER}/<name>
#
LINKS+=	${BINDIR}/objformat ${BINDIR}/CC
LINKS+=	${BINDIR}/objformat ${BINDIR}/cpp
LINKS+=	${BINDIR}/objformat ${BINDIR}/c++
LINKS+=	${BINDIR}/objformat ${BINDIR}/c++filt
LINKS+=	${BINDIR}/objformat ${BINDIR}/cc
LINKS+=	${BINDIR}/objformat ${BINDIR}/f77
LINKS+=	${BINDIR}/objformat ${BINDIR}/g++
LINKS+=	${BINDIR}/objformat ${BINDIR}/gcc
LINKS+=	${BINDIR}/objformat ${BINDIR}/gcov

.include <bsd.prog.mk>
