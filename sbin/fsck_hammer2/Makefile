PROG= fsck_hammer2
SRCS= fsck_hammer2.c test.c
MAN=  fsck_hammer2.8

CFLAGS+= -I${.CURDIR}/../../sys -I${.CURDIR}/../hammer2
CFLAGS+= -DXXH_NAMESPACE=h2_
CFLAGS+= -DHAMMER2_USE_OPENSSL
CFLAGS+= ${PRIVATELIB_CFLAGS}

DPADD=   ${LIBCRYPTO}
LDADD=   -lprivate_crypto
LDFLAGS+=${PRIVATELIB_LDFLAGS}

.PATH: ${.CURDIR}/../../sys/libkern ${.CURDIR}/../../sys/vfs/hammer2/xxhash
SRCS+= icrc32.c xxhash.c
.PATH: ${.CURDIR}/../hammer2
SRCS+= subs.c

.include <bsd.prog.mk>
