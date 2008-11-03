# $FreeBSD: src/secure/usr.bin/telnet/Makefile,v 1.21.2.4 2002/07/03 22:20:33 des Exp $
# $DragonFly: src/secure/usr.bin/telnet/Makefile,v 1.4 2008/11/03 00:25:45 pavalos Exp $

PROG=		telnet

SRCS=		authenc.c commands.c main.c network.c ring.c sys_bsd.c \
		telnet.c terminal.c utilities.c

CFLAGS+=	-DKLUDGELINEMODE -DUSE_TERMIO -DENV_HACK -DOPIE \
		-DENCRYPTION -DAUTHENTICATION -DIPSEC -DINET6 \
		-I${TELNETDIR} -I${TELNETDIR}/libtelnet/

WARNS?=		0

DPADD=		${LIBTERMCAP} ${LIBTELNET} \
		${LIBCRYPTO} ${LIBCRYPT} ${LIBIPSEC} ${LIBPAM}
LDADD=		-ltermcap ${LIBTELNET} \
		-lcrypto -lcrypt -lipsec ${MINUSLPAM}

.include <bsd.prog.mk>

.PATH:	${TELNETDIR}/telnet
