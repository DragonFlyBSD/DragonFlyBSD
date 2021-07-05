# $FreeBSD: src/sys/modules/crypto/Makefile,v 1.6 2007/05/09 19:37:01 gnn Exp $

.PATH:	${.CURDIR}/../opencrypto
.PATH:	${.CURDIR}
.PATH:	${.CURDIR}/blowfish
.PATH:	${.CURDIR}/des
.PATH:	${.CURDIR}/rijndael
.PATH:	${.CURDIR}/sha2
.PATH:	${.CURDIR}/camellia
.PATH:	${.CURDIR}/twofish
.PATH:	${.CURDIR}/serpent

KMOD	= crypto
SRCS	= crypto.c cryptodev_if.c gmac.c
SRCS	+= criov.c cryptosoft.c xform.c
SRCS	+= cast.c deflate.c rmd160.c rijndael-alg-fst.c rijndael-api.c
SRCS	+= skipjack.c bf_enc.c bf_skey.c
SRCS	+= des_ecb.c des_enc.c des_setkey.c
SRCS	+= sha1.c sha2.c
SRCS	+= cryptodev_if.h bus_if.h device_if.h
SRCS	+= opt_ddb.h
SRCS	+= camellia.c camellia-api.c
SRCS	+= twofish.c
SRCS	+= serpent.c

SUBDIR	= rc4

.include <bsd.kmod.mk>
