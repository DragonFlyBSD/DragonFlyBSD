# This file automatically generates the asm .s files after importing a new
# version of OpenSSL.  You need to run it with MACHINE_ARCH=x86_64
# to get everything generated properly.

OPENSSL_SRC=	../../../../crypto/openssl

.PATH:	${OPENSSL_SRC}/crypto \
	${OPENSSL_SRC}/crypto/aes/asm \
	${OPENSSL_SRC}/crypto/bf/asm \
	${OPENSSL_SRC}/crypto/bn/asm \
	${OPENSSL_SRC}/crypto/camellia/asm \
	${OPENSSL_SRC}/crypto/des/asm \
	${OPENSSL_SRC}/crypto/md5/asm \
	${OPENSSL_SRC}/crypto/modes/asm \
	${OPENSSL_SRC}/crypto/perlasm \
	${OPENSSL_SRC}/crypto/rc4/asm \
	${OPENSSL_SRC}/crypto/ripemd/asm \
	${OPENSSL_SRC}/crypto/sha/asm \
	${OPENSSL_SRC}/crypto/whrlpool/asm

.if ${MACHINE_ARCH} == "x86_64"
# cpuid
SRCS=	x86_64cpuid.pl

# bn
SRCS+=	x86_64-mont.pl x86_64-mont5.pl x86_64-gf2m.pl modexp512-x86_64.pl

# aes
SRCS+=	aes-x86_64.pl vpaes-x86_64.pl bsaes-x86_64.pl \
	aesni-x86_64.pl aesni-sha1-x86_64.pl

# rc4
SRCS+=	rc4-x86_64.pl rc4-md5-x86_64.pl

# md5
SRCS+=	md5-x86_64.pl

# sha
SRCS+=	sha1-x86_64.pl sha256-x86_64.s sha512-x86_64.pl

# whrlpool
SRCS+=	wp-x86_64.pl

# camellia
SRCS+=	cmll-x86_64.pl

# modes
SRCS+=	ghash-x86_64.pl

PERLFLAGS=
.endif

all:	${SRCS:S/.pl$/.s/}

CLEANFILES+=	${SRCS:S/.pl$/.s/}
.SUFFIXES:	.pl

sha{256,512}-x86_64.s:	${OPENSSL_SRC}/crypto/sha/asm/sha512-x86_64.pl
	perl ${.ALLSRC} elf ${.TARGET}
	echo ".section .note.GNU-stack,\"\",%progbits" >>${.TARGET}

.pl.s:
	perl ${.IMPSRC} elf ${PERLFLAGS} > ${.TARGET}
	echo ".section .note.GNU-stack,\"\",%progbits" >>${.TARGET}

.include <bsd.prog.mk>
