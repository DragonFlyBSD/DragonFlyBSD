# $FreeBSD: src/usr.sbin/pkg_install/lib/Makefile,v 1.16 2004/07/28 07:19:15 kan Exp $
# $DragonFly: src/usr.sbin/pkg_install/lib/Attic/Makefile,v 1.4 2004/07/30 04:46:13 dillon Exp $

LIB=	install
INTERNALLIB= YES
SRCS=	file.c msg.c plist.c str.c exec.c global.c pen.c match.c \
	deps.c version.c pkgwrap.c url.c

CFLAGS+= ${DEBUG}

WARNS?=	3
WFORMAT?=	1

.include <bsd.lib.mk>
