# $FreeBSD: src/secure/usr.sbin/sshd/Makefile,v 1.5.2.9 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/usr.sbin/sshd/Makefile,v 1.6 2004/09/05 11:53:58 dillon Exp $
#

PROG=	sshd
SRCS=	sshd.c auth-rhosts.c auth-passwd.c auth-rsa.c auth-rh-rsa.c \
	sshpty.c sshlogin.c servconf.c serverloop.c uidswap.c \
	auth.c auth1.c auth2.c auth-options.c session.c \
	auth-chall.c auth2-chall.c groupaccess.c auth-skey.c \
	auth-bsdauth.c auth2-pam-freebsd.c auth2-hostbased.c \
	auth2-kbdint.c auth2-none.c auth2-passwd.c auth2-pubkey.c \
	monitor_mm.c monitor.c monitor_wrap.c monitor_fdpass.c \
	kexdhs.c kexgexs.c misc.c dh.c kex.c channels.c \
	bsd-closefrom.c

# Portability layer
SRCS+=	loginrec.c xmmap.c
MAN=	sshd.8 sshd_config.5

# Passwd routine
SRCS+=	auth-passwd-freebsd.c
CFLAGS+=-DCUSTOM_SYS_AUTH_PASSWD

SRCS+=	auth-skey.c.patch auth.h.patch auth2.c.patch \
	loginrec.c.patch monitor.c.patch \
	myproposal.h.patch servconf.c.patch session.c.patch sshd.c.patch 

SRCS+=	sshd.8.no_obj.patch sshd_config.5.no_obj.patch sshd_config.no_obj.patch

.if defined(MAKE_KERBEROS5)
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

# There's a privilege separation bug when PAM authentication is
# used and posix threads are disabled.  Both descriptors of the socketpair
# used for the communication between the [net] and the [pam] process are
# left open, and when a timeout kills one procses the other does not see
# it because it still has both end points open.  This eventually causes
# sshd to start rejecting connections.  So, enable posix threading
# here even though I don't want to use it.
#
OPENSSH_USE_POSIX_THREADS=1

.if defined(OPENSSH_USE_POSIX_THREADS) && !defined(NOLIBC_R)
CFLAGS+=-DUSE_POSIX_THREADS
LDADD+= -lc_r
DPADD+= ${LIBC_R}
.endif

etc-ssh: sshd_config
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 ${.ALLSRC} ${DESTDIR}/etc/ssh

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>

.PATH: ${SSHDIR}/openbsd-compat

