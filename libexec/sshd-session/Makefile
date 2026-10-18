LIBSSH_RELDIR=	../../lib/libssh
.include "${LIBSSH_RELDIR}/Makefile.common"

PROG=	sshd-session
NOMAN=

WARNS?=	3

# From vendor's Makefile
P11OBJS= ssh-pkcs11-client.o
SKOBJS=	ssh-sk-client.o
SSHD_SESSION_OBJS=sshd-session.o auth-rhosts.o auth-passwd.o \
	audit.o audit-bsm.o audit-linux.o platform.o \
	sshpty.o sshlogin.o servconf.o serverloop.o \
	auth.o auth2.o auth2-methods.o auth-options.o session.o \
	auth2-chall.o groupaccess.o \
	auth-bsdauth.o auth2-hostbased.o auth2-kbdint.o \
	auth2-none.o auth2-passwd.o auth2-pubkey.o auth2-pubkeyfile.o \
	monitor.o monitor_wrap.o auth-krb5.o \
	auth2-gss.o gss-serv.o gss-serv-krb5.o \
	loginrec.o auth-pam.o auth-shadow.o auth-sia.o \
	sftp-server.o sftp-common.o \
	uidswap.o platform-listen.o $(P11OBJS) $(SKOBJS)

SRCS=	${SSHD_SESSION_OBJS:.o=.c}

DPADD+=		${LIBCRYPT} ${LIBPAM} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}
LDADD+=		-lcrypt ${MINUSLPAM} -lprivate_crypto -lutil -lz
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

FLAGS_GROUPS=		warn
warn_FLAGS=		-Wno-discarded-qualifiers
warn_FLAGS_FILES=	session.c

.include <bsd.prog.mk>
