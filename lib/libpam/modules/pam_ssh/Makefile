# PAM module for SSH
# $FreeBSD: head/lib/libpam/modules/pam_ssh/Makefile 312452 2017-01-20 03:27:47Z ngie $

LIB=	pam_ssh
SRCS=	pam_ssh.c
MAN=	pam_ssh.8
WARNS?=	3

.if 0
# Subset of libssh.a, needed to start ssh-agent and load keys.
.PATH: ${.CURDIR}/../../../../crypto/openssh
SRCS+=	authfd.c authfile.c sshkey.c
# Support functions for three above.
SRCS+=	addrmatch.c atomicio.c bitmap.c cleanup.c digest-openssl.c hash.c
SRCS+=	fatal.c krl.c log.c match.c misc.c platform-misc.c verify.c
SRCS+=	cipher.c cipher-chachapoly.c chacha.c poly1305.c ssh-dss.c ssh-rsa.c
SRCS+=	ssh-ecdsa.c ssh-ed25519.c ed25519.c fe25519.c ge25519.c sc25519.c
SRCS+=	sshbuf.c sshbuf-getput-basic.c sshbuf-getput-crypto.c sshbuf-misc.c
SRCS+=	ssherr.c xmalloc.c
# Support functions from compat.
.PATH: ${.CURDIR}/../../../libssh/openbsd-compat
SRCS+=	bcrypt_pbkdf.c bsd-signal.c port-net.c realpath.c
CFLAGS+=	-I${.CURDIR}/../../../libssh
.endif

LDADD=		-lprivate_ssh -lprivate_crypto -lcrypt
DPADD=		${LIBSSH} ${LIBCRYPTO} ${LIBCRYPT}

CFLAGS+=	-I${.CURDIR}/../../../../crypto/openssh
CFLAGS+=	-I${.CURDIR}/../../../../crypto/libressl/include
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.lib.mk>
