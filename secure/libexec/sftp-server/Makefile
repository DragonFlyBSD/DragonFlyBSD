# $FreeBSD: src/secure/libexec/sftp-server/Makefile,v 1.1.2.3 2002/07/03 22:13:22 des Exp $
# $DragonFly: src/secure/libexec/sftp-server/Makefile,v 1.4 2008/04/07 01:20:18 pavalos Exp $

PROG=   sftp-server
SRCS=   sftp-server.c sftp-common.c sftp-server-main.c roaming_dummy.c
MAN=	sftp-server.8
WARNS?=	0

# Portability layer
SRCS+=	fmt_scaled.c

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
