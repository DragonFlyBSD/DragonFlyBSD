#
# $FreeBSD: src/usr.sbin/tcpdmatch/Makefile,v 1.2.2.2 2001/04/25 12:11:01 ru Exp $
# $DragonFly: src/usr.sbin/tcpdmatch/Makefile,v 1.3 2004/01/31 06:56:46 dillon Exp $
#

PROG=	tcpdmatch
MAN=	tcpdmatch.8
SRCS=	tcpdmatch.c fakelog.c inetcf.c scaffold.c
CSTD?=	gnu89
WARNS?=	0

CFLAGS=	-DREAL_DAEMON_DIR=\"/usr/libexec\" \
	-DSEVERITY=LOG_INFO -DRFC931_TIMEOUT=10
.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

DPADD=	${LIBWRAP}
LDADD=	-lwrap

.PATH:	${.CURDIR}/../../contrib/tcp_wrappers

.include <bsd.prog.mk>
