#	$OpenBSD: Makefile,v 1.4 2003/11/20 23:23:09 avsm Exp $
#	@(#)Makefile	8.2 (Berkeley) 4/4/94
#	$DragonFly: src/libexec/ftp-proxy/Makefile,v 1.1 2004/09/21 21:25:28 joerg Exp $

PROG=	ftp-proxy
SRCS=	ftp-proxy.c getline.c util.c
MAN=	ftp-proxy.8
WARNS?=	6

CFLAGS+= -DLIBWRAP
LDADD+=	-lwrap
DPADD+=	${LIBWRAP}

.include <bsd.prog.mk>
