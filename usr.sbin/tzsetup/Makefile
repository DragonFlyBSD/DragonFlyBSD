# $FreeBSD: src/usr.sbin/tzsetup/Makefile,v 1.10.2.1 2001/04/25 12:11:07 ru Exp $
# $DragonFly: src/usr.sbin/tzsetup/Makefile,v 1.2 2003/06/17 04:30:03 dillon Exp $

PROG=	tzsetup
MAN=	tzsetup.8
CFLAGS+=	-I${.CURDIR}
LDADD+=	-ldialog -lncurses -lmytinfo
DPADD+=	${LIBDIALOG} ${LIBNCURSES} ${LIBMYTINFO}

.include <bsd.prog.mk>
