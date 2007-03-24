# From:	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/libexec/rshd/Makefile,v 1.15.2.3 2001/04/25 10:40:59 ru Exp $
# $DragonFly: src/libexec/rshd/Makefile,v 1.3 2007/03/24 21:52:14 swildner Exp $

PROG=	rshd
MAN=	rshd.8
WARNS?=	6

#CFLAGS+= -DCRYPT

# For login_cap handling
CFLAGS+=-DLOGIN_CAP
DPADD+=	${LIBUTIL}
LDADD+= -lutil

# IPv6 support
CFLAGS+= -DINET6

.include <bsd.prog.mk>
