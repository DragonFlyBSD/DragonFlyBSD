LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	ssh-pkcs11-helper
MAN=	ssh-pkcs11-helper.8

WARNS=	3

# From vendor's Makefile
SKOBJS=	ssh-sk-client.o
P11HELPER_OBJS=	ssh-pkcs11-helper.o ssh-pkcs11.o $(SKOBJS)

SRCS=	${P11HELPER_OBJS:.o=.c}

DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
