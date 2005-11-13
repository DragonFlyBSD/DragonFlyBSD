# $FreeBSD: src/secure/usr.bin/sftp/Makefile,v 1.1.2.3 2002/07/03 22:20:30 des Exp $
# $DragonFly: src/secure/usr.bin/sftp/Makefile,v 1.4 2005/11/13 11:58:30 corecode Exp $

PROG=	sftp
SRCS=   sftp.c sftp-client.c sftp-common.c sftp-glob.c

DPADD+=	${LIBEDIT}
LDADD+=	-ledit

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
