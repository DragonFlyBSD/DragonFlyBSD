# This file automatically generates the asm .s files after importing a new
# version of OpenSSL.  You need to run it with MACHINE_ARCH=i386 and x86_64
# to get everything generated properly.

OPENSSL_SRC=	../../../../crypto/openssl

.PATH:	${OPENSSL_SRC}/crypto \
	${OPENSSL_SRC}/crypto/aes/asm \
	${OPENSSL_SRC}/crypto/bf/asm \
	${OPENSSL_SRC}/crypto/bn/asm \
	${OPENSSL_SRC}/crypto/camellia/asm \
	${OPENSSL_SRC}/crypto/des/asm \
	${OPENSSL_SRC}/crypto/md5/asm \
	${OPENSSL_SRC}/crypto/perlasm \
	${OPENSSL_SRC}/crypto/rc4/asm \
	${OPENSSL_SRC}/crypto/ripemd/asm \
	${OPENSSL_SRC}/crypto/sha/asm \
	${OPENSSL_SRC}/crypto/whrlpool/asm

.if ${MACHINE_ARCH} == "i386"
# cpuid
SRCS=	x86cpuid.pl

# aes
SRCS+=	aes-586.pl

# bf
SRCS+=	bf-586.pl

# bn
SRCS+=	bn-586.pl co-586.pl x86-mont.pl

# camellia
SRCS+=	cmll-x86.pl

# des
SRCS+=	des-586.pl crypt586.pl

# md5
SRCS+=	md5-586.pl

# rc4
SRCS+=	rc4-586.pl

# ripemd
SRCS+=	rmd-586.pl

# sha
SRCS+=	sha1-586.pl sha256-586.pl sha512-586.pl

# whrlpool
SRCS+=	wp-mmx.pl

PERLFLAGS=	${CFLAGS}

.elif ${MACHINE_ARCH} == "x86_64"
# cpuid
SRCS=	x86_64cpuid.pl

# aes
SRCS+=	aes-x86_64.pl

# bn
SRCS+=	x86_64-mont.pl

# camellia
SRCS+=	cmll-x86_64.pl

# md5
SRCS+=	md5-x86_64.pl

# rc4
SRCS+=	rc4-x86_64.pl

# sha
SRCS+=	sha1-x86_64.pl sha256-x86_64.s sha512-x86_64.pl

# whrlpool
SRCS+=	wp-x86_64.pl

PERLFLAGS=
.endif

all:	${SRCS:S/.pl$/.s/}

CLEANFILES+=	${SRCS:S/.pl$/.s/}
.SUFFIXES:	.pl

sha{256,512}-x86_64.s:	${OPENSSL_SRC}/crypto/sha/asm/sha512-x86_64.pl
	perl ${.ALLSRC} elf ${.TARGET}

.pl.s:
	perl ${.IMPSRC} elf ${PERLFLAGS} > ${.TARGET}

.include <bsd.prog.mk>
