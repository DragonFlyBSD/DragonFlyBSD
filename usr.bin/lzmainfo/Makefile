PROG=	lzmainfo

XZDIR=	${.CURDIR}/../../contrib/xz/src

.PATH: ${XZDIR}/lzmainfo
SRCS=	lzmainfo.c

.PATH: ${XZDIR}/common
SRCS+=	tuklib_progname.c tuklib_exit.c

CFLAGS+= -DHAVE_CONFIG_H -I${.CURDIR}/../../lib/liblzma
CFLAGS+= -I${XZDIR}/common

DPADD=	${LIBLZMA}
LDADD=	-llzma

.include <bsd.prog.mk>
