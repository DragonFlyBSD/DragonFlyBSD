# $FreeBSD: src/kerberos5/libexec/telnetd/Makefile,v 1.15.2.2 2003/04/24 19:14:00 nectar Exp $
# $DragonFly: src/kerberos5/libexec/telnetd/Attic/Makefile,v 1.4 2005/01/11 13:22:41 joerg Exp $

# Do not define -DKLUDGELINEMODE, as it does not interact well with many
# telnet implementations.

PROG=		telnetd
MAN=		telnetd.8

SRCS=		global.c slc.c state.c sys_term.c telnetd.c \
		termstat.c utility.c authenc.c

CFLAGS+=	-DLINEMODE -DUSE_TERMIO -DDIAGNOSTICS -DOLD_ENVIRON \
		-DENV_HACK -DAUTHENTICATION -DENCRYPTION \
		-I${TELNETDIR} -DINET6
CFLAGS+=	-DKRB5 -DFORWARD -Dnet_write=telnet_net_write

WARNS?=		2

DPADD=		${LIBUTIL} ${LIBTERMCAP} ${LIBTELNET} ${LIBCRYPTO} \
		${LIBKRB5} ${LIBASN1} ${LIBROKEN} ${LIBCOM_ERR} \
		${LIBCRYPT} ${LIBPAM}
LDADD=		-lutil -ltermcap ${LIBTELNET} -lcrypto \
		-L${KRB5OBJDIR} -lkrb5 -L${ASN1OBJDIR} -lasn1 \
		-L${ROKENOBJDIR} -lroken -lcom_err \
		-lcrypt ${MINUSLPAM}

.include <bsd.prog.mk>

.PATH:	${TELNETDIR}/telnetd
