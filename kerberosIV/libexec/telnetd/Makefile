# $FreeBSD: src/kerberosIV/libexec/telnetd/Makefile,v 1.7.2.4 2002/04/13 11:00:51 markm Exp $
# $DragonFly: src/kerberosIV/libexec/telnetd/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

# Do not define -DKLUDGELINEMODE, as it does not interact well with many
# telnet implementations.

PROG=		telnetd
MAN=		telnetd.8

SRCS=		global.c slc.c state.c sys_term.c telnetd.c \
		termstat.c utility.c authenc.c

CFLAGS+=	-DLINEMODE -DUSE_TERMIO -DDIAGNOSTICS -DOLD_ENVIRON \
		-DENV_HACK -DAUTHENTICATION -DENCRYPTION \
		-I${TELNETDIR} -DINET6
CFLAGS+=	-DKRB4

WARNS?=		2

DPADD=		${LIBUTIL} ${LIBTERMCAP} ${LIBTELNET} ${LIBMP} ${LIBCRYPTO} \
		${LIBKRB} ${LIBCOM_ERR} \
		${LIBCRYPT} ${LIBPAM}
LDADD=		-lutil -ltermcap ${LIBTELNET} -lmp -lcrypto \
		-L${KRBOBJDIR} -lkrb -lcom_err \
		-lcrypt ${MINUSLPAM}

.include <bsd.prog.mk>

.PATH:	${TELNETDIR}/telnetd
