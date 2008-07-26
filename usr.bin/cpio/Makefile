# $DragonFly: src/usr.bin/cpio/Makefile,v 1.2 2008/07/26 16:25:31 pavalos Exp $

PROG=	bsdcpio
SRCS=	cmdline.c cpio.c err.c matching.c pathmatch.c
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBZ}
LDADD=	-larchive -lbz2 -lz

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive-2/cpio
.PATH: ${CONTRIBDIR}
CFLAGS+=	-I${.OBJDIR} -I${.CURDIR}/../../lib/libarchive -I${CONTRIBDIR}
CFLAGS+=	-DPLATFORM_CONFIG_H=\"config.h\"

.if !defined(WITH_GCPIO)
SYMLINKS=	${BINDIR}/bsdcpio ${BINDIR}/cpio
MLINKS=	bsdcpio.1 cpio.1
.endif

.include <bsd.prog.mk>
