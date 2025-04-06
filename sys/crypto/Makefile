# $FreeBSD: src/sys/modules/crypto/Makefile,v 1.6 2007/05/09 19:37:01 gnn Exp $

KMOD=	crypto

.PATH:	${.CURDIR}/../opencrypto
# excluded: cryptodev.c
SRCS=	cast.c criov.c crypto.c cryptosoft.c deflate.c gmac.c \
	skipjack.c xform.c
SRCS+=	cryptodev_if.c cryptodev_if.h

SRCS+=	sha1.c
SRCS+=	chachapoly.c

.PATH:	${.CURDIR}/rmd160
SRCS+=	rmd160.c

.PATH:	${.CURDIR}/blake2
SRCS+=	blake2s.c

.PATH:	${.CURDIR}/blowfish
SRCS+=	bf_enc.c bf_skey.c

.PATH:	${.CURDIR}/camellia
SRCS+=	camellia.c camellia-api.c

.PATH:	${.CURDIR}/chacha20
SRCS+=	chacha.c

.PATH:	${.CURDIR}/curve25519
SRCS+=	curve25519.c

.PATH:	${.CURDIR}/des
SRCS+=	des_ecb.c des_enc.c des_setkey.c

.PATH:	${.CURDIR}/poly1305
SRCS+=	poly1305.c

.PATH:	${.CURDIR}/rijndael
SRCS+=	rijndael-alg-fst.c rijndael-api.c

.PATH:	${.CURDIR}/aesni
SRCS+=	aesencdec_${MACHINE_ARCH}.S aeskeys_${MACHINE_ARCH}.S aesni_wrap.c

.PATH:	${.CURDIR}/serpent
SRCS+=	serpent.c

.PATH:	${.CURDIR}/sha2
SRCS+=	sha2.c

.PATH:	${.CURDIR}/siphash
SRCS+=	siphash.c

.PATH:	${.CURDIR}/twofish
SRCS+=	twofish.c

SRCS+=	bus_if.h device_if.h opt_ddb.h

SUBDIR=	rc4

.include <bsd.kmod.mk>
