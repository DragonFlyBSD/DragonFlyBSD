LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	sftp

WARNS?=	3

# From vendor's Makefile
SFTP_CLIENT_OBJS=sftp-common.o sftp-client.o sftp-glob.o ssherr-nolibcrypto.o
SFTP_OBJS=	sftp.o sftp-usergroup.o progressmeter.o $(SFTP_CLIENT_OBJS)

SRCS=	${SFTP_OBJS:.o=.c}

DPADD+=		${LIBEDIT} ${LIBNCURSES}
LDADD+=		-lprivate_edit -lprivate_ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
