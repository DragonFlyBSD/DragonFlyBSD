PROG=	fstyp
SRCS=	apfs.c cd9660.c exfat.c ext2fs.c fstyp.c hfsplus.c msdosfs.c ntfs.c ufs.c hammer.c hammer2.c

MAN=	fstyp.8

WARNS?=	2

CFLAGS+=-I${.CURDIR}/../../sys

.include <bsd.prog.mk>
