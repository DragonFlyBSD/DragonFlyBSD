# $FreeBSD: src/secure/libexec/telnetd/Makefile,v 1.19.2.4 2002/07/03 22:20:25 des Exp $
# $DragonFly: src/secure/libexec/telnetd/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $

# Do not define -DKLUDGELINEMODE, as it does not interact well with many
# telnet implementations.

PROG=		telnetd
MAN=		telnetd.8

SRCS=		global.c slc.c state.c sys_term.c telnetd.c \
		termstat.c utility.c authenc.c

CFLAGS+=	-DLINEMODE -DUSE_TERMIO -DDIAGNOSTICS -DOLD_ENVIRON \
		-DENV_HACK -DAUTHENTICATION -DENCRYPTION \
		-I${TELNETDIR} -DINET6

WARNS?=		2
WFORMAT?=	0

DPADD=		${LIBUTIL} ${LIBTERMCAP} ${LIBTELNET} ${LIBMP} ${LIBCRYPTO} \
		${LIBCRYPT} ${LIBPAM}
LDADD=		-lutil -ltermcap ${LIBTELNET} -lmp -lcrypto \
		-lcrypt ${MINUSLPAM}

.include <bsd.prog.mk>

.PATH:	${TELNETDIR}/telnetd
