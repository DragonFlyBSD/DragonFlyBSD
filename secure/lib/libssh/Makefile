# $FreeBSD: src/secure/lib/libssh/Makefile,v 1.2.2.7 2003/02/03 17:31:12 des Exp $
# $DragonFly: src/secure/lib/libssh/Makefile,v 1.3 2003/08/05 07:45:42 asmodai Exp $

LIB=	ssh
SRCS=	authfd.c authfile.c bufaux.c buffer.c canohost.c channels.c \
	cipher.c compat.c compress.c crc32.c deattack.c fatal.c \
	hostfile.c log.c match.c mpaux.c nchan.c packet.c readpass.c \
	rsa.c tildexpand.c ttymodes.c xmalloc.c atomicio.c \
	key.c dispatch.c kex.c mac.c uuencode.c misc.c \
	rijndael.c ssh-dss.c ssh-rsa.c dh.c kexdh.c kexgex.c \
	scard.c monitor_wrap.c monitor_fdpass.c msg.c xmmap.c
# Portability layer
SRCS+=	bsd-misc.c entropy.c
# FreeBSD additions
SRCS+=	version.c

.if defined(COMPAT_GETADDRINFO)
SRCS+=	getaddrinfo.c getnameinfo.c name6.c rcmd.c bindresvport.c
.endif

CFLAGS+= -I${SSHDIR}
.if defined(MAKE_KERBEROS5)
CFLAGS+= -DKRB5 -DHEIMDAL
.endif

NOLINT=	true

DPADD=	${LIBCRYPTO} ${LIBZ}
LDADD=	-lcrypto -lz

.include <bsd.lib.mk>

.PATH: ${SSHDIR} ${SSHDIR}/openbsd-compat
