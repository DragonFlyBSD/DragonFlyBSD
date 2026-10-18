LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	ssh-keysign
MAN=	ssh-keysign.8

BINOWN=	root
BINMODE=4555

WARNS=	3

# From vendor's Makefile
P11OBJS= ssh-pkcs11-client.o
SKOBJS=	ssh-sk-client.o
SSHKEYSIGN_OBJS=ssh-keysign.o readconf.o uidswap.o $(P11OBJS) $(SKOBJS)

SRCS=	${SSHKEYSIGN_OBJS:.o=.c}

DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
