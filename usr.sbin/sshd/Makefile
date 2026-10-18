LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	sshd
MAN=	sshd.8 sshd_config.5 moduli.5

WARNS?=	3

# From vendor's Makefile
P11OBJS= ssh-pkcs11-client.o
SKOBJS=	ssh-sk-client.o
SSHDOBJS=sshd.o \
	platform-listen.o \
	servconf.o sshpty.o srclimit.o groupaccess.o auth2-methods.o \
	dns.o fatal.o compat.o utf8.o authfd.o canohost.o \
	$(P11OBJS) $(SKOBJS)

SRCS=	${SSHDOBJS:.o=.c}

DPADD+=		${LIBCRYPT} ${LIBPAM} ${LIBLDNS} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}
LDADD+=		-lcrypt ${MINUSLPAM} -lprivate_ldns -lprivate_crypto -lutil -lz
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
