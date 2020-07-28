PROG=	sshd
SRCS=	sshd.c

.PATH: ${.CURDIR}/../../crypto/openssh
SRCS+=	auth.c auth-options.c auth-pam.c auth-passwd.c auth-rhosts.c \
	auth2.c auth2-chall.c auth2-hostbased.c auth2-kbdint.c auth2-none.c \
	auth2-passwd.c auth2-pubkey.c groupaccess.c loginrec.c \
	monitor.c monitor_wrap.c platform.c \
	sandbox-rlimit.c servconf.c serverloop.c session.c \
	sftp-common.c sftp-realpath.c sftp-server.c sshlogin.c sshpty.c uidswap.c

# not needed:
# audit-bsm.c audit-linux.c audit.c auth-bsdauth.c auth-krb5.c auth-shadow.c auth-sia.c
# auth2-gss.c gss-serv-krb5.c gss-serv.c md5crypt.c
# sandbox-capsicum.c sandbox-darwin.c sandbox-null.c sandbox-pledge.c
# sandbox-seccomp-filter.c sandbox-solaris.c sandbox-systrace.c

# Custom password routine
SRCS+=	auth-passwd-custom.c
CFLAGS+= -DCUSTOM_SYS_AUTH_PASSWD
CFLAGS+= -DWTMPX_FILE=_PATH_WTMPX -DUTMPX_FILE=_PATH_UTMPX
CFLAGS+= -DUTXDB_LASTLOGIN=UTX_DB_LASTLOGX

WARNS?=	3

MAN=	sshd.8 sshd_config.5

CFLAGS+= -DUSE_PAM
#CFLAGS+= -DHAVE_SECURITY_PAM_APPL_H -DHAVE_PAM_GETENVLIST -DHAVE_PAM_PUTENV

.if defined(XAUTH_PATH)
CFLAGS+= -DXAUTH_PATH=\"${XAUTH_PATH}\"
.endif

LDADD+=	-lcrypt ${MINUSLPAM}
DPADD+=	${LIBCRYPT} ${LIBPAM}

CFLAGS+=	-I${.CURDIR}/../../lib/libssh -I${.CURDIR}/../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
DPADD+=		${LIBSSH} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}
LDADD+=		-lprivate_ssh -lprivate_crypto -lutil -lz
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
