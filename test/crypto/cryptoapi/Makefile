PROG = cryptoapi_test
WARNS = 6
NOMAN = YES
BINDIR=	/usr/local/bin

CFLAGS+=	-I${.CURDIR}/../../../sys

SRCS+=		crypto-dev.c
SRCS+=		crypto-api.c
SRCS+=		cryptoapi_test.c

.PATH:		${.CURDIR}/../../../sys/crypto/cryptoapi
SRCS+=		cryptoapi.c
.PATH:		${.CURDIR}/../../../sys/crypto/rijndael
SRCS+=		rijndael-alg-fst.c rijndael-api.c
.PATH:		${.CURDIR}/../../../sys/crypto/serpent
SRCS+=		serpent.c
.PATH:		${.CURDIR}/../../../sys/crypto/twofish
SRCS+=		twofish.c

run-tests: cryptoapi_test
	sysctl kern.cryptodevallowsoft=1
	./cryptoapi_test fixtures.csv cryptodev | diff - expected_output.md
	./cryptoapi_test fixtures.csv cryptoapi | diff - expected_output.md

.include <bsd.prog.mk>
