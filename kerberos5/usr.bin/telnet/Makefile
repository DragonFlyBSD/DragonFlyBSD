# $FreeBSD: src/kerberos5/usr.bin/telnet/Makefile,v 1.16.2.2 2003/04/24 19:14:00 nectar Exp $
# $DragonFly: src/kerberos5/usr.bin/telnet/Attic/Makefile,v 1.4 2005/01/11 13:22:41 joerg Exp $

PROG=		telnet

SRCS=		authenc.c commands.c main.c network.c ring.c sys_bsd.c \
		telnet.c terminal.c utilities.c

CFLAGS+=	-DKLUDGELINEMODE -DUSE_TERMIO -DENV_HACK -DOPIE \
		-DENCRYPTION -DAUTHENTICATION -DIPSEC -DINET6 \
		-I${TELNETDIR} -I${TELNETDIR}/libtelnet/

CFLAGS+=	-DKRB5 -DFORWARD -Dnet_write=telnet_net_write

WARNS?=		2

DPADD=		${LIBTERMCAP} ${LIBTELNET} \
		${LIBKRB5} ${LIBASN1} ${LIBCOM_ERR} ${LIBROKEN} \
		${LIBCRYPTO} ${LIBCRYPT} ${LIBIPSEC} ${LIBPAM}
LDADD=		-ltermcap ${LIBTELNET} \
		-L${KRB5OBJDIR} -lkrb5 -L${ASN1OBJDIR} -lasn1 -lcom_err \
		-L${ROKENOBJDIR} -lroken \
		-lcrypto -lcrypt -lipsec ${MINUSLPAM}

.include <bsd.prog.mk>

.PATH:	${TELNETDIR}/telnet
