#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/rmt/Makefile,v 1.6.8.1 2001/04/25 12:10:43 ru Exp $
# $DragonFly: src/usr.sbin/rmt/Makefile,v 1.2 2003/06/17 04:30:02 dillon Exp $

PROG=	rmt
MAN=	rmt.8

# called from /usr/src/etc/Makefile
etc-rmt:
	rm -f ${DESTDIR}/etc/rmt
	ln -s ${BINDIR}/rmt ${DESTDIR}/etc/rmt

.include <bsd.prog.mk>
