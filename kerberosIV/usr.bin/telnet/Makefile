# $FreeBSD: src/kerberosIV/usr.bin/telnet/Makefile,v 1.8.2.3 2002/04/13 11:00:52 markm Exp $
# $DragonFly: src/kerberosIV/usr.bin/telnet/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

PROG=		telnet

SRCS=		authenc.c commands.c main.c network.c ring.c sys_bsd.c \
		telnet.c terminal.c utilities.c

CFLAGS+=	-DKLUDGELINEMODE -DUSE_TERMIO -DENV_HACK -DOPIE \
		-DENCRYPTION -DAUTHENTICATION -DIPSEC -DINET6 \
		-I${TELNETDIR} -I${TELNETDIR}/libtelnet/

CFLAGS+=	-DKRB4

WARNS?=		2

DPADD=		${LIBTERMCAP} ${LIBTELNET} ${LIBMP} \
		${LIBKRB} ${LIBCOM_ERR} \
		${LIBCRYPTO} ${LIBCRYPT} ${LIBIPSEC} ${LIBPAM}
LDADD=		-ltermcap ${LIBTELNET} -lmp \
		-L${KRBOBJDIR} -lkrb -lcom_err \
		-lcrypto -lcrypt -lipsec ${MINUSLPAM}

.include <bsd.prog.mk>

.PATH:	${TELNETDIR}/telnet
