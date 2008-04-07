# $FreeBSD: src/secure/usr.bin/ssh/Makefile,v 1.4.2.8 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/usr.bin/ssh/Makefile,v 1.8 2008/04/07 01:20:18 pavalos Exp $
#

PROG=	ssh
LINKS=	${BINDIR}/ssh ${BINDIR}/slogin
MAN=	ssh.1 ssh_config.5
MLINKS=	ssh.1 slogin.1

SRCS=	ssh.c clientloop.c sshtty.c sshconnect.c sshconnect1.c sshconnect2.c
SRCS+=	readconf.c channels.c kex.c uidswap.c monitor_fdpass.c packet.c misc.c

.if defined(WANT_KERBEROS)
DISTRIBUTION=krb5
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
