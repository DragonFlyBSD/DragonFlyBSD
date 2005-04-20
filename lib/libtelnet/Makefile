#	From: @(#)Makefile	8.2 (Berkeley) 12/15/93
# $FreeBSD: src/lib/libtelnet/Makefile,v 1.10.2.2 2002/07/19 18:46:26 ru Exp $
# $DragonFly: src/lib/libtelnet/Makefile,v 1.3 2005/04/20 20:07:19 joerg Exp $

LIB=		telnet

INTERNALLIB=	yes

SRCS=		genget.c getent.c misc.c

WARNS?=		2

unifdef:
	-cd ${.CURDIR} ; \
	( for i in *.[ch] ; do \
		unifdef -UAUTHENTICATION -UENCRYPTION \
			${.CURDIR}/../../crypto/telnet/libtelnet/$$i \
				> ${.CURDIR}/$$i ; \
		echo $$i ; \
	done )

.include <bsd.lib.mk>
