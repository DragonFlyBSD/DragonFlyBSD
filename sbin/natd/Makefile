# $FreeBSD: src/sbin/natd/Makefile,v 1.4.2.1 2001/04/25 10:58:41 ru Exp $
# $DragonFly: src/sbin/natd/Makefile,v 1.3 2004/01/31 06:56:41 dillon Exp $

PROG		= natd
SRCS		= natd.c icmp.c
CFLAGS	       += -Wall
LDADD		= -lalias
DPADD		= ${LIBALIAS}
MAN		= natd.8

.include <bsd.prog.mk>
