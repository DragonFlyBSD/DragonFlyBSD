#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/libexec/tftpd/Makefile,v 1.7.2.1 2001/04/25 10:41:03 ru Exp $
# $DragonFly: src/libexec/tftpd/Makefile,v 1.2 2003/06/17 04:27:08 dillon Exp $

PROG=	tftpd
SRCS=	tftpd.c tftpsubs.c
DPADD=	${LIBUTIL}
LDADD=	-lutil
MAN=	tftpd.8
CFLAGS+=-I${.CURDIR}/../../usr.bin/tftp
.PATH:	${.CURDIR}/../../usr.bin/tftp

.include <bsd.prog.mk>
