LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	ssh-agent

WARNS?=	3

# From vendor's Makefile
P11OBJS= ssh-pkcs11-client.o
SKOBJS=	ssh-sk-client.o
SSHAGENT_OBJS=	ssh-agent.o $(P11OBJS) $(SKOBJS)

SRCS=	${SSHAGENT_OBJS:.o=.c}

DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
