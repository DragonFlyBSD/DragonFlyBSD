#	$NetBSD: Makefile,v 1.8 2004/01/01 02:44:09 mrg Exp $
# $DragonFly: src/usr.bin/gzip/Makefile,v 1.4 2005/04/22 06:52:42 joerg Exp $

PROG=		gzip
SRCS=		gzip.c
MAN=		gzip.1 gzexe.1 zdiff.1 zforce.1 zmore.1 znew.1

DPADD=		${LIBZ}
LDADD=		-lz
WARNS=		6

NOSHARED?=	yes

.if defined(NO_GZIP_UNCOMPRESS)
CFLAGS+=	-DNO_COMPRESS_SUPPORT
.endif
.if defined(GZIP_UNBZIP2)
DPADD+=		${LIBZ2}
LDADD+=		-lbz2
.else
CFLAGS+=	-DNO_BZIP2_SUPPORT
.endif

SCRIPTS=	gzexe zdiff zforce zmore znew

MLINKS+=	gzip.1 gunzip.1 \
		gzip.1 gzcat.1 \
		gzip.1 zcat.1 \
		zdiff.1 zcmp.1

LINKS+=		${BINDIR}/gzip ${BINDIR}/gunzip \
		${BINDIR}/gzip ${BINDIR}/gzcat \
		${BINDIR}/gzip ${BINDIR}/zcat \
		${BINDIR}/zdiff ${BINDIR}/zcmp

.include <bsd.prog.mk>
