#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/rmt/Makefile,v 1.6.8.1 2001/04/25 12:10:43 ru Exp $
# $DragonFly: src/usr.sbin/rmt/Makefile,v 1.3 2005/07/07 11:49:57 corecode Exp $

PROG=	rmt
MAN=	rmt.8
WARNS?=	3

# called from /usr/src/etc/Makefile
etc-rmt:
	rm -f ${DESTDIR}/etc/rmt
	${LN} -s ${BINDIR}/rmt ${DESTDIR}/etc/rmt

.include <bsd.prog.mk>
