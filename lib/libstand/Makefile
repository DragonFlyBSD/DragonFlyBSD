# $FreeBSD: src/lib/libstand/Makefile,v 1.14.2.10 2002/07/22 14:21:50 ru Exp $
# $DragonFly: src/lib/libstand/Makefile,v 1.6 2004/09/28 19:34:18 joerg Exp $
#
# Originally from	$NetBSD: Makefile,v 1.21 1997/10/26 22:08:38 lukem Exp $
#
# Notes:
# - We don't use the libc strerror/sys_errlist because the string table is
#   quite large.
#

LIB=		stand
NOPROFILE=	YES
NOPIC=		YES
INCS=		stand.h
MAN=		libstand.3

# Mostly OK, some of the libc imports are a bit noisy
CFLAGS+=	-Wall -ffreestanding

.if ${MACHINE_ARCH} == "amd64"
CFLAGS+=	-m32 -mpreferred-stack-boundary=2
.endif
.if ${MACHINE_ARCH} == "i386"
CFLAGS+=	-mpreferred-stack-boundary=2
.endif


# standalone components and stuff we have modified locally
SRCS+=	__main.c assert.c bcd.c bswap.c environment.c getopt.c gets.c \
	globals.c pager.c printf.c strdup.c strerror.c strtol.c random.c \
	sbrk.c twiddle.c zalloc.c zalloc_malloc.c

# private (pruned) versions of libc string functions
SRCS+=	strcasecmp.c

# string functions from libc
.PATH:	${.CURDIR}/../libc/string
.if ${MACHINE_ARCH} == "i386"
SRCS+=	bcmp.c bcopy.c bzero.c ffs.c index.c memccpy.c memchr.c memcmp.c \
        memcpy.c memmove.c memset.c qdivrem.c rindex.c strcat.c strchr.c \
        strcmp.c strcpy.c strcspn.c strlen.c strncat.c strncmp.c strncpy.c \
	strpbrk.c strrchr.c strsep.c strspn.c strstr.c strtok.c swab.c
.endif
.PATH:  ${.CURDIR}/../libc/quad
.if ${MACHINE_ARCH} == "i386"
SRCS+=  ucmpdi2.c
.endif

.if ${MACHINE_ARCH} == "i386"
# _setjmp/_longjmp
.PATH:	${.CURDIR}/${MACHINE_ARCH}
SRCS+=	_setjmp.S
# really only required for i386
CFLAGS+=-I${.CURDIR}/../libc/${MACHINE_ARCH}
.endif

# decompression functionality from libbz2
.PATH:	${.CURDIR}/../../contrib/bzip2
CFLAGS+=-I. -DBZ_NO_STDIO -DBZ_NO_COMPRESS
SRCS+=	_bzlib.c _crctable.c _decompress.c _huffman.c _randtable.c _bzlib.h \
	_bzlib_private.h

# check your belt - ugly bzip2 stuff ahead
.for file in bzlib.c bzlib.h bzlib_private.h
CLEANFILES+=   _${file} _${file}.orig

_${file}: ${file} ${file}.diff
	patch -s --suffix=.orig -o ${.TARGET} < ${.ALLSRC:M*.diff} ${.ALLSRC:M*.[ch]}
.endfor

.for file in crctable.c decompress.c huffman.c randtable.c
CLEANFILES+=    _${file}

_${file}: ${file}
	sed "s|bzlib_private\.h|_bzlib_private.h|" ${.ALLSRC} > ${.TARGET}
.endfor

# decompression functionality from libz
.PATH:	${.CURDIR}/../libz
CFLAGS+=-DHAVE_MEMCPY
SRCS+=	adler32.c crc32.c infblock.c infcodes.c inffast.c inflate.c \
	inftrees.c infutil.c zutil.c 

# io routines
SRCS+=	closeall.c dev.c ioctl.c nullfs.c stat.c \
	fstat.c close.c lseek.c open.c read.c write.c readdir.c

# network routines
SRCS+=	arp.c ether.c inet_ntoa.c in_cksum.c net.c udp.c netif.c rpc.c

# network info services:
SRCS+=	bootp.c rarp.c bootparam.c

# boot filesystems
SRCS+=	ufs.c nfs.c cd9660.c tftp.c zipfs.c bzipfs.c gzipfs.c
SRCS+=	netif.c nfs.c
SRCS+=	dosfs.c ext2fs.c
SRCS+=	splitfs.c

.include <bsd.lib.mk>
