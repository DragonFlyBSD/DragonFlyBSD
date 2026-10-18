LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	ssh-sk-helper
MAN=	ssh-sk-helper.8

WARNS=	3

# From vendor's Makefile
SKHELPER_OBJS=	ssh-sk-helper.o ssh-sk.o sk-usbhid.o ssherr-nolibcrypto.o

SRCS=	${SKHELPER_OBJS:.o=.c}

DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
