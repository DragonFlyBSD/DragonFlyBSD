GCC_NO_LIBS=	# defined
.include "Makefile.headers"
.include "../Makefile.inc"
.PATH: ${GCCDIR}/lto-plugin
.PATH: ${GCCDIR}/libiberty
.PATH: ${GCCDIR}/include
.PATH: ${GCCDIR}/gcc/cp
.PATH: ${GCCDIR}/libcpp/include

LIB=		lto_plugin

SHLIB_MAJOR=	0
NOPROFILE=	yes
NOINSTALLLIB=	yes

# GCC driver looks for LTO plugin library in libexec, not lib
TARGET_LIBDIR=		/usr/libexec/gcc47
TARGET_SHLIBDIR=	/usr/libexec/gcc47

# Plugin support file locations
PLUGIN_RESOURCE_DIR=	/usr/lib/gcc47/plugin
PLUGIN_HEADER_DIR=	${PLUGIN_RESOURCE_DIR}/include

CFLAGS+=	-I${GCCDIR}/include
CFLAGS+=	-I${GCCDIR}/libiberty
CFLAGS+=	-DHAVE_CONFIG_H
SRCS=		lto-plugin.c
SRCS+=		hashtab.c \
		xmalloc.c \
		simple-object.c \
		simple-object-elf.c \
		simple-object-mach-o.c \
		simple-object-coff.c \
		xstrerror.c \
		pex-unix.c \
		pex-common.c \
		argv.c \
		safe-ctype.c \
		xexit.c \
		xstrdup.c \
		make-temp-file.c \
		concat.c

FILESGROUPS+=	LVL1_ LVL2_ LVL2_1_ LVL2_2_ LVL2_3_ LVL2_4_ LVL3_
LVL1_DIR=	${PLUGIN_RESOURCE_DIR}
LVL2_DIR=	${PLUGIN_HEADER_DIR}
LVL2_1_DIR=	${PLUGIN_HEADER_DIR}/c-family
LVL2_2_DIR=	${PLUGIN_HEADER_DIR}/cp
LVL2_3_DIR=	${PLUGIN_HEADER_DIR}/objc
LVL2_4_DIR=	${PLUGIN_HEADER_DIR}/config
LVL3_DIR=	${PLUGIN_HEADER_DIR}/config/i386

LVL1_=		${.OBJDIR}/../cc_tools/tools/gtype.state
LVL2_=		${PLUGIN_HEADERS}
LVL2_1_=	${PLUGIN_HEADERS_CFAMILY}
LVL2_2_=	${PLUGIN_HEADERS_CP}
LVL2_3_=	${PLUGIN_HEADERS_OBJC}
LVL2_4_=	${PLUGIN_HEADERS_CONFIG}
LVL3_=		${PLUGIN_HEADERS_CONFIG_I386}

.include <bsd.lib.mk>
