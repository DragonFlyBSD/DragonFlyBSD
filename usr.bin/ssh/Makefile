LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	ssh
LINKS=	${BINDIR}/ssh ${BINDIR}/slogin
MAN=	ssh.1 ssh_config.5
MLINKS=	ssh.1 slogin.1

WARNS?=	3

# From vendor's Makefile
SKOBJS=	ssh-sk-client.o
SSHOBJS= ssh.o readconf.o clientloop.o sshtty.o \
	sshconnect.o sshconnect2.o mux.o ssh-pkcs11.o $(SKOBJS)

SRCS=	${SSHOBJS:.o=.c}

DPADD+=		${LIBLDNS} ${LIBCRYPTO} ${LIBZ}
LDADD+=		-lprivate_ldns -lprivate_crypto -lz
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
