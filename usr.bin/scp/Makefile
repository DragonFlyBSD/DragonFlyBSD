LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	scp

WARNS?=	3

# From vendor's Makefile
SFTP_CLIENT_OBJS=sftp-common.o sftp-client.o sftp-glob.o ssherr-nolibcrypto.o
SCP_OBJS=	scp.o progressmeter.o $(SFTP_CLIENT_OBJS)

SRCS=	${SCP_OBJS:.o=.c}

.include <bsd.prog.mk>
