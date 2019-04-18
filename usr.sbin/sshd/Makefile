PROG=	sshd
SRCS=	sshd.c

.PATH: ${.CURDIR}/../../crypto/openssh
SRCS+=	auth.c auth-options.c auth-pam.c auth-passwd.c auth-rhosts.c \
	auth2.c auth2-chall.c auth2-hostbased.c auth2-kbdint.c auth2-none.c \
	auth2-passwd.c auth2-pubkey.c groupaccess.c loginrec.c \
	monitor.c monitor_wrap.c platform.c \
	sandbox-rlimit.c servconf.c serverloop.c session.c \
	sftp-common.c sftp-server.c sshlogin.c sshpty.c uidswap.c

# not needed:
# audit-bsm.c audit-linux.c audit.c auth-bsdauth.c auth-krb5.c auth-shadow.c auth-sia.c
# auth2-gss.c gss-serv-krb5.c gss-serv.c md5crypt.c
# sandbox-capsicum.c sandbox-darwin.c sandbox-null.c sandbox-pledge.c
# sandbox-seccomp-filter.c sandbox-solaris.c sandbox-systrace.c

WARNS?=	3

MAN=	sshd.8 sshd_config.5

CFLAGS+= -DUSE_PAM
#CFLAGS+= -DHAVE_SECURITY_PAM_APPL_H -DHAVE_PAM_GETENVLIST -DHAVE_PAM_PUTENV

LDADD+=	-lopie
DPADD+=	${LIBOPIE}

LDADD+=	-lcrypt -lwrap ${MINUSLPAM}
DPADD+=	${LIBCRYPT} ${LIBWRAP} ${LIBPAM}

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
