# $FreeBSD: src/secure/lib/libssh/Makefile,v 1.2.2.7 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/lib/libssh/Makefile,v 1.4 2004/07/31 19:03:00 geekgod Exp $

LIB=	ssh
SRCS=	acss.c authfd.c authfile.c bufaux.c buffer.c canohost.c channels.c \
	cipher.c cipher-acss.c cipher-aes.c cipher-bf1.c cipher-ctr.c \
	cipher-3des1.c cleanup.c compat.c compress.c crc32.c deattack.c \
	fatal.c hostfile.c log.c match.c moduli.c mpaux.c nchan.c packet.c \
	readpass.c rsa.c tildexpand.c ttymodes.c xmalloc.c atomicio.c \
	key.c dispatch.c kex.c mac.c uuencode.c misc.c rijndael.c ssh-dss.c \
	ssh-rsa.c dh.c kexdh.c kexgex.c kexdhc.c kexgexc.c scard.c msg.c \
	progressmeter.c dns.c scard-opensc.c getrrsetbyname.c vis.c
# Portability layer
SRCS+=	bsd-misc.c entropy.c
# FreeBSD additions
SRCS+=	version.c

# Patches
SRCS+=	log.c.patch

.if defined(MAKE_KERBEROS5)
CFLAGS+= -DKRB5 -DHEIMDAL
.endif

DPADD+=	${LIBZ}
LDADD+=	-lz

etc-ssh:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 ${SSHDIR}/moduli ${DESTDIR}/etc/ssh

.include "../../Makefile.ssh.common"
.include <bsd.lib.mk>

.PATH: ${SSHDIR}/openbsd-compat
