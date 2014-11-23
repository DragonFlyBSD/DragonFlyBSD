# $FreeBSD: src/secure/usr.bin/ssh/Makefile,v 1.4.2.8 2003/02/03 17:31:12 des Exp $

PROG=	ssh
LINKS=	${BINDIR}/ssh ${BINDIR}/slogin
MAN=	ssh.1 ssh_config.5
MLINKS=	ssh.1 slogin.1
WARNS?=	2

SRCS=	ssh.c readconf.c clientloop.c sshtty.c \
	sshconnect.c sshconnect1.c sshconnect2.c mux.c \
	roaming_common.c roaming_client.c

X11BASE?=	/usr/local
CFLAGS+= -DXAUTH_PATH=\"${X11BASE}/bin/xauth\"

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
