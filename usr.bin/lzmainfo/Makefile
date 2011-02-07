PROG=	lzmainfo

XZDIR=	${.CURDIR}/../../contrib/xz/src

.PATH: ${XZDIR}/lzmainfo
SRCS=	lzmainfo.c

.PATH: ${XZDIR}/common
SRCS+=	tuklib_progname.c tuklib_exit.c

CFLAGS+= -DHAVE_CONFIG_H
CFLAGS+= -I${.CURDIR}/../../lib/liblzma -I${XZDIR}/common

DPADD=	${LIBLZMA}
LDADD=	-llzma

.include <bsd.prog.mk>
