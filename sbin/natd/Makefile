# $FreeBSD: src/sbin/natd/Makefile,v 1.4.2.1 2001/04/25 10:58:41 ru Exp $
# $DragonFly: src/sbin/natd/Makefile,v 1.2 2003/06/17 04:27:34 dillon Exp $

MAINTAINER	= ru@FreeBSD.org
MAINTAINER     += ari@suutari.iki.fi
PROG		= natd
SRCS		= natd.c icmp.c
CFLAGS	       += -Wall
LDADD		= -lalias
DPADD		= ${LIBALIAS}
MAN		= natd.8

.include <bsd.prog.mk>
