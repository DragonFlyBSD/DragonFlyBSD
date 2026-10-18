LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	ssh-keyscan

WARNS?=	3

# From vendor's Makefile
P11OBJS= ssh-pkcs11-client.o
SKOBJS=	ssh-sk-client.o
SSHKEYSCAN_OBJS=ssh-keyscan.o $(P11OBJS) $(SKOBJS)

SRCS=	${SSHKEYSCAN_OBJS:.o=.c}

DPADD+=		${LIBLDNS} ${LIBCRYPTO} ${LIBZ}
LDADD+=		-lprivate_ldns -lprivate_crypto -lz
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
