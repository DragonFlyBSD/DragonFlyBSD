#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/uuencode/Makefile,v 1.2.14.4 2002/10/20 14:38:53 fanf Exp $
# $DragonFly: src/usr.bin/uuencode/Makefile,v 1.3 2004/12/24 15:13:56 liamfoy Exp $

PROG=	uuencode
MAN=	uuencode.1 uuencode.format.5
LINKS=	${BINDIR}/uuencode ${BINDIR}/b64encode
MLINKS=	uuencode.1 uudecode.1 \
	uuencode.format.5 uuencode.5 \
	uuencode.1 b64encode.1 \
	b64encode.1 b64decode.1

WARNS?=	6
.include <bsd.prog.mk>
