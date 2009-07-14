# $FreeBSD: src/secure/usr.bin/ssh/Makefile,v 1.4.2.8 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/usr.bin/ssh/Makefile,v 1.9 2008/09/28 03:19:46 pavalos Exp $
#

PROG=	ssh
LINKS=	${BINDIR}/ssh ${BINDIR}/slogin
MAN=	ssh.1 ssh_config.5
MLINKS=	ssh.1 slogin.1

SRCS=	channels.c clientloop.c kex.c misc.c monitor_fdpass.c mux.c packet.c \
	readconf.c ssh.c sshconnect.c sshconnect1.c sshconnect2.c sshtty.c \
	uidswap.c

.if defined(WANT_KERBEROS)
CFLAGS+= -DKRB5 -DHEIMDAL
LDADD+=	 -lkrb5 -lasn1 -lcom_err -lmd -L${.OBJDIR}/../../../kerberos5/lib/libroken -lroken -lcrypt
DPADD+=	 ${LIBKRB5} ${LIBCOM_ERR} ${LIBASN1} ${LIBMD} ${LIBCRYPT}
.endif

.if defined(X11BASE)
CFLAGS+= -DXAUTH_PATH=\"${X11BASE}/bin/xauth\"
.endif

LDADD+=	-lutil -lz
DPADD+=	${LIBUTIL} ${LIBZ}

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
