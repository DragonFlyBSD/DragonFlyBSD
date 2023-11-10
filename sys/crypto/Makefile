# $FreeBSD: src/sys/modules/crypto/Makefile,v 1.6 2007/05/09 19:37:01 gnn Exp $

KMOD=	crypto

.PATH:	${.CURDIR}/../opencrypto
# excluded: cryptodev.c
SRCS=	cast.c criov.c crypto.c cryptosoft.c deflate.c gmac.c rmd160.c \
	skipjack.c xform.c
SRCS+=	cryptodev_if.c cryptodev_if.h

SRCS+=	sha1.c

.PATH:	${.CURDIR}/blowfish
SRCS+=	bf_enc.c bf_skey.c

.PATH:	${.CURDIR}/camellia
SRCS+=	camellia.c camellia-api.c

.PATH:	${.CURDIR}/chacha20
SRCS+=	chacha.c

.PATH:	${.CURDIR}/des
SRCS+=	des_ecb.c des_enc.c des_setkey.c

.PATH:	${.CURDIR}/rijndael
SRCS+=	rijndael-alg-fst.c rijndael-api.c

.PATH:	${.CURDIR}/serpent
SRCS+=	serpent.c

.PATH:	${.CURDIR}/sha2
SRCS+=	sha2.c

.PATH:	${.CURDIR}/twofish
SRCS+=	twofish.c

SRCS+=	bus_if.h device_if.h opt_ddb.h

SUBDIR=	rc4

.include <bsd.kmod.mk>
