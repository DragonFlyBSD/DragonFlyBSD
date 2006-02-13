# $FreeBSD: src/secure/lib/libssh/Makefile,v 1.2.2.7 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/lib/libssh/Makefile,v 1.8 2006/02/13 14:47:58 corecode Exp $

LIB=	ssh
SHLIB_MAJOR= 4
SRCS=	acss.c authfd.c authfile.c bufaux.c buffer.c canohost.c channels.c \
	cipher.c cipher-acss.c cipher-aes.c cipher-bf1.c cipher-ctr.c \
	cipher-3des1.c cleanup.c compat.c compress.c crc32.c deattack.c \
	fatal.c hostfile.c log.c match.c moduli.c nchan.c packet.c \
	readpass.c rsa.c ttymodes.c xmalloc.c atomicio.c \
	key.c dispatch.c kex.c mac.c uuencode.c misc.c rijndael.c ssh-dss.c \
	ssh-rsa.c dh.c kexdh.c kexgex.c kexdhc.c kexgexc.c scard.c msg.c \
	progressmeter.c dns.c scard-opensc.c getrrsetbyname.c
# Portability layer
SRCS+=	bsd-misc.c entropy.c
# FreeBSD additions
SRCS+=	version.c

SRCS+=	openbsd-compat,port-tun.c.patch

.if defined(WANT_KERBEROS)
CFLAGS+= -DKRB5 -DHEIMDAL
.endif

DPADD+=	${LIBZ}
LDADD+=	-lz

.include "../../Makefile.ssh.common"
.include <bsd.lib.mk>

.PATH: ${SSHDIR}/openbsd-compat
