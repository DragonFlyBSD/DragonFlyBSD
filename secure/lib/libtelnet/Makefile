#	$FreeBSD: src/secure/lib/libtelnet/Makefile,v 1.17.2.7 2002/07/22 14:21:51 ru Exp $
#	$DragonFly: src/secure/lib/libtelnet/Makefile,v 1.3 2005/07/28 19:48:40 joerg Exp $

LIB=		telnet

INTERNALLIB=	yes

SRCS=		genget.c getent.c misc.c encrypt.c auth.c \
		enc_des.c sra.c pk.c

CFLAGS+=	-DENCRYPTION -DAUTHENTICATION -DSRA \
		-I${TELNETDIR} -I${TELNETDIR}/libtelnet

WARNS?=		2

INCS=		${TELNETDIR}/arpa/telnet.h
INCSDIR=	${INCLUDEDIR}/arpa

.include <bsd.lib.mk>

.PATH:	${TELNETDIR}/libtelnet
