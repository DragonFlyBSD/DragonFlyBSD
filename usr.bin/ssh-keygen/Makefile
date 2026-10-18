LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	ssh-keygen

WARNS?=	3

# From vendor's Makefile
SKOBJS=	ssh-sk-client.o
SSHKEYGEN_OBJS=	ssh-keygen.o sshsig.o ssh-pkcs11.o $(SKOBJS)

SRCS=	${SSHKEYGEN_OBJS:.o=.c}

DPADD+=		${LIBLDNS} ${LIBCRYPTO}
LDADD+=		-lprivate_ldns -lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
