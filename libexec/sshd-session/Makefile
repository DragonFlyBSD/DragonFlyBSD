PROG=	sshd-session
SRCS=	sshd-session.c

.PATH: ${.CURDIR}/../../crypto/openssh
SRCS+=  auth-rhosts.c auth-passwd.c \
        audit.c audit-bsm.c audit-linux.c platform.c \
        sshpty.c sshlogin.c servconf.c serverloop.c \
        auth.c auth2.c auth2-methods.c auth-options.c session.c \
        auth2-chall.c groupaccess.c \
        auth-bsdauth.c auth2-hostbased.c auth2-kbdint.c \
        auth2-none.c auth2-passwd.c auth2-pubkey.c auth2-pubkeyfile.c \
        monitor.c monitor_wrap.c auth-krb5.c \
        auth2-gss.c gss-serv.c gss-serv-krb5.c \
        loginrec.c auth-pam.c auth-shadow.c auth-sia.c \
        sftp-server.c sftp-common.c \
        sandbox-null.c sandbox-rlimit.c sandbox-systrace.c sandbox-darwin.c \
        sandbox-seccomp-filter.c sandbox-capsicum.c sandbox-pledge.c \
        sandbox-solaris.c uidswap.c

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
