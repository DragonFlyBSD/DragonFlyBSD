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
	./cryptoapi_test -t cryptodev
	./cryptoapi_test -t cryptoapi

run-bench: cryptoapi_test
	@./cryptoapi_test -t cryptodev -b 1000000 2>&1 >/dev/null
	@./cryptoapi_test -t cryptoapi -b 1000000 2>&1 >/dev/null

commit-test-fixtures: cryptoapi_test fixtures.csv
	./cryptoapi_test -t cryptoapi -o fixtures.csv

.include <bsd.prog.mk>
