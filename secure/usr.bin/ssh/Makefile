# $FreeBSD: src/secure/usr.bin/ssh/Makefile,v 1.4.2.8 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/usr.bin/ssh/Makefile,v 1.4 2004/07/31 19:03:01 geekgod Exp $
#

PROG=	ssh
LINKS=	${BINDIR}/ssh ${BINDIR}/slogin
MAN=	ssh.1 ssh_config.5
MLINKS=	ssh.1 slogin.1

SRCS=	ssh.c clientloop.c sshtty.c sshconnect.c sshconnect1.c sshconnect2.c

SRCS+=	readconf.c.patch ssh.c.patch ${.CURDIR}/../../usr.sbin/sshd/myproposal.h.patch
SRCS+=	ssh.1.no_obj.patch ssh_config.no_obj.patch ssh_config.5.no_obj.patch

.if defined(MAKE_KERBEROS5)
DISTRIBUTION=krb5
CFLAGS+= -DKRB5 -DHEIMDAL
LDADD+=	 -lkrb5 -lasn1 -lcom_err -lmd -L${.OBJDIR}/../../../kerberos5/lib/libroken -lroken -lcrypt
DPADD+=	 ${LIBKRB5} ${LIBCOM_ERR} ${LIBASN1} ${LIBMD} ${LIBCRYPT}
.endif

.if defined(X11BASE)
CFLAGS+= -DXAUTH_PATH=\"${X11BASE}/bin/xauth\"
.endif

LDADD+=	-lutil -lz
DPADD+=	${LIBUTIL} ${LIBZ}

etc-ssh: ssh_config
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 ${.ALLSRC} ${DESTDIR}/etc/ssh

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
