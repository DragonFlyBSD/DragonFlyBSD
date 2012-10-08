GCC_NO_LIBS=	# defined
.include "../Makefile.inc"
.PATH: ${GCCDIR}/lto-plugin

LIB=		lto_plugin

SHLIB_MAJOR=	0
NOPROFILE=	yes
NOINSTALLLIB=	yes

# GCC driver looks for LTO plugin library in libexec, not lib
TARGET_SHLIBDIR=	/usr/libexec/gcc47

CFLAGS+=	-I${GCCDIR}/include
CFLAGS+=	-DHAVE_CONFIG_H
SRCS=		lto-plugin.c

.include <bsd.lib.mk>
