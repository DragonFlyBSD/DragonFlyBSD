# $FreeBSD: src/secure/usr.bin/sftp/Makefile,v 1.1.2.3 2002/07/03 22:20:30 des Exp $
# $DragonFly: src/secure/usr.bin/sftp/Makefile,v 1.5 2008/09/28 03:19:46 pavalos Exp $

PROG=	sftp
SRCS=	progressmeter.c sftp.c sftp-client.c sftp-common.c sftp-glob.c roaming_dummy.c
WARNS?=	0

# Portability layer
SRCS+=	fmt_scaled.c

DPADD+=	${LIBEDIT} ${LIBNCURSES}
LDADD+=	-ledit -lncurses

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>

.PATH:	${SSHDIR}/openbsd-compat
