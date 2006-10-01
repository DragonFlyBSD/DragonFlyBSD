# $FreeBSD: src/secure/usr.sbin/sshd/Makefile,v 1.5.2.9 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/usr.sbin/sshd/Makefile,v 1.12.4.1 2006/10/01 21:22:28 victor Exp $
#

PROG=	sshd
SRCS=	sshd.c auth-rhosts.c auth-pam.c auth-passwd.c auth-rsa.c \
	auth-rh-rsa.c sshpty.c sshlogin.c servconf.c serverloop.c uidswap.c \
	auth.c auth1.c auth2.c auth-options.c session.c \
	auth-chall.c auth2-chall.c groupaccess.c auth-skey.c \
	auth-bsdauth.c auth2-hostbased.c \
	auth2-kbdint.c auth2-none.c auth2-passwd.c auth2-pubkey.c \
	monitor_mm.c monitor.c monitor_wrap.c monitor_fdpass.c \
	kexdhs.c kexgexs.c misc.c dh.c kex.c channels.c

# Portability layer
SRCS+=	loginrec.c xmmap.c
MAN=	sshd.8 sshd_config.5

# Passwd routine
SRCS+=	auth-passwd-freebsd.c
CFLAGS+=-DCUSTOM_SYS_AUTH_PASSWD

# Flags for PAM
CFLAGS+=-DUSE_PAM -DHAVE_SECURITY_PAM_APPL_H \
	-DHAVE_PAM_GETENVLIST -DHAVE_PAM_PUTENV

SRCS+=	auth-skey.c.patch auth.h.patch auth2.c.patch \
	deattack.c.patch deattack.h.patch defines.h.patch \
	log.c.patch log.h.patch loginrec.c.patch monitor.c.patch \
	myproposal.h.patch packet.c.patch servconf.c.patch \
	session.c.patch sshd.c.patch 

SRCS+=	sshd.8.no_obj.patch sshd_config.5.no_obj.patch

# Fix problems with patch infraestructure
CFLAGS+= -I${.OBJDIR} -I${.CURDIR} -I${CONTRIBDIR} -I${CONTRIBDIR}/openbsd-compat/ -I-

.if defined(WANT_KERBEROS)
DISTRIBUTION=krb5
CFLAGS+= -DKRB5 -DHEIMDAL
SRCS+=	auth-krb5.c
LDADD+=	 -lkrb5 -lasn1 -lcom_err -lmd -L${.OBJDIR}/../../../kerberos5/lib/libroken -lroken
DPADD+=	 ${LIBKRB5} ${LIBCOM_ERR} ${LIBASN1} ${LIBMD}
.endif

LDADD+=	-lopie -lmd
DPADD+=	${LIBOPIE} ${LIBMD}

.if defined(X11BASE)
CFLAGS+= -DXAUTH_PATH=\"${X11BASE}/bin/xauth\"
.endif

LDADD+=	-lcrypt -lutil -lz -lwrap ${MINUSLPAM}
DPADD+=	${LIBCRYPT} ${LIBUTIL} ${LIBZ} ${LIBWRAP} ${LIBPAM}

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>

.PATH: ${SSHDIR}/openbsd-compat

