LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	sftp-server
MAN=	sftp-server.8

WARNS?=	3

# From vendor's Makefile
SFTPSERVER_OBJS=sftp-common.o sftp-server.o sftp-server-main.o ssherr-nolibcrypto.o

SRCS=	${SFTPSERVER_OBJS:.o=.c}

.include <bsd.prog.mk>
