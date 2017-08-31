# $FreeBSD: src/lib/libstand/Makefile,v 1.14.2.10 2002/07/22 14:21:50 ru Exp $
#
# Originally from	$NetBSD: Makefile,v 1.21 1997/10/26 22:08:38 lukem Exp $
#
# Notes:
# - We don't use the libc strerror/sys_errlist because the string table is
#   quite large.
#

LIB?=		stand
NOPROFILE=	YES
NOPIC=		YES
INCS?=		stand.h
MAN?=		libstand.3
CSTD?=		c99
WARNS?=		2
NO_STRICT_ALIASING=

LIBSTAND_SRC?=	${.CURDIR}
LIBSTAND_ARCH?=	${MACHINE_ARCH}
LIBC_SRC=	${LIBSTAND_SRC}/../libc

# Mostly OK, some of the libc imports are a bit noisy
CFLAGS+=	-ffreestanding

# Disable stack protector
CFLAGS+=	-fno-stack-protector

.if (${CFLAGS:M-flto} && ${CCVER:Mgcc*})
CFLAGS+=	-fno-builtin	# LTO exposes too much stuff
.endif

.if ${LIBSTAND_ARCH} == "x86_64"
CFLAGS+=	-mno-red-zone -fPIC
.endif

.if ${LIBSTAND_ARCH} == "i386"
.if ${CCVER:Mgcc*}
CFLAGS+=	-mpreferred-stack-boundary=2
.endif
FORCE_CPUTYPE=	i386
CFLAGS+=	-m32
.endif

.if ${LIBSTAND_ARCH} == "i386" || ${LIBSTAND_ARCH} == "x86_64"
CFLAGS+=	-mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3 -msoft-float
.endif

# standalone components and stuff we have modified locally
SRCS+=	__main.c assert.c bcd.c bswap.c environment.c getopt.c gets.c \
	globals.c pager.c printf.c random.c \
	strdup.c strerror.c strtol.c strtoul.c sbrk.c \
	twiddle.c zalloc.c zalloc_malloc.c

# private (pruned) versions of libc string functions
SRCS+=	strcasecmp.c

# string functions from libc
.PATH:	${LIBSTAND_SRC}/../libc/string
.if ${LIBSTAND_ARCH} == "x86_64" || ${LIBSTAND_ARCH} == "i386"
SRCS+=	bcmp.c bcopy.c bzero.c ffs.c index.c memccpy.c memchr.c memcmp.c \
        memcpy.c memmove.c memset.c qdivrem.c rindex.c strcat.c strchr.c \
        strcmp.c strcpy.c strcspn.c strlen.c strncat.c strncmp.c strncpy.c \
	strpbrk.c strrchr.c strsep.c strspn.c strstr.c strtok.c swab.c

# math stuff, thus no -flto!
qdivrem.o:	qdivrem.c
	${CC} ${CFLAGS:N-flto} -c -o ${.TARGET} ${.IMPSRC}
.endif
.PATH:	${LIBSTAND_SRC}/../../sys/libkern
SRCS+=  icrc32.c

# _setjmp/_longjmp
.PATH:	${LIBSTAND_SRC}/${LIBSTAND_ARCH}
.if ${LIBSTAND_ARCH} == "x86_64" || ${LIBSTAND_ARCH} == "i386"
# really only required for i386
CFLAGS+=-I${LIBSTAND_SRC}/../libc/${MACHINE_ARCH}
.endif
SRCS+=	_setjmp.S

# decompression functionality from libbz2
BZ2DIR=	${LIBSTAND_SRC}/../../contrib/bzip2
.PATH:	${BZ2DIR}
CFLAGS+=-I${BZ2DIR} -DBZ_NO_STDIO -DBZ_NO_COMPRESS
SRCS+=	bzlib.c crctable.c decompress.c huffman.c randtable.c bzlib.h \
	bzlib_private.h

# check your belt - ugly bzip2 stuff ahead
.for file in bzlib.c
CLEANFILES+=   ${file}

${file}: ${BZ2DIR}/${file} ${file}.diff
	patch -o ${.TARGET} < ${.ALLSRC:M*.diff} ${.ALLSRC:M*.[ch]}
.endfor

.include "../libz/Makefile.stand"

# io routines
SRCS+=	closeall.c dev.c ioctl.c nullfs.c stat.c \
	fstat.c close.c lseek.c open.c read.c write.c readdir.c

# uuid routines
SRCS+=	uuid_from_string.c uuid_to_string.c

# uuid routines from libc
.PATH: ${LIBC_SRC}/uuid
SRCS+=	uuid_create_nil.c

# network routines
SRCS+=	arp.c ether.c inet_ntoa.c in_cksum.c net.c udp.c netif.c rpc.c

# network info services:
SRCS+=	bootp.c rarp.c bootparam.c

# boot filesystems
SRCS+=	ufs.c nfs.c cd9660.c tftp.c zipfs.c bzipfs.c gzipfs.c
SRCS+=	netif.c nfs.c
SRCS+=	dosfs.c ext2fs.c
SRCS+=	splitfs.c
SRCS+=	hammer1.c
SRCS+=	hammer2.c

.include <bsd.lib.mk>
