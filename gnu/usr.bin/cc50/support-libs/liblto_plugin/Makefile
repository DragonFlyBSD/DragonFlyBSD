# liblto_plugin.so needs to linked to a PIC version of libiberty.
# A special tailed version of libiberty has been created for this purpose

GCC_NO_LIBS=	# defined
.include "Makefile.headers"
.include "../Makefile.inc"
.PATH: ${GCCDIR}/lto-plugin
.PATH: ${GCCDIR}/libiberty
.PATH: ${GCCDIR}/include
.PATH: ${GCCDIR}/gcc/cp
.PATH: ${GCCDIR}/gcc/c
.PATH: ${GCCDIR}/gcc/objc
.PATH: ${GCCDIR}/libcpp/include

LIB=		lto_plugin
SHLIB_MAJOR=	0
NOPROFILE=	yes
NOINSTALLLIB=	yes

# GCC driver looks for LTO plugin library in libexec, not lib
TARGET_SHLIBDIR=	/usr/libexec/gcc50

# Plugin support file locations
PLUGIN_RESOURCE_DIR=	/usr/lib/gcc50/plugin
PLUGIN_HEADER_DIR=	${PLUGIN_RESOURCE_DIR}/include

SRCS=		lto-plugin.c
LDADD=		../libiberty-pic/libiberty_pic.a

FILESGROUPS+=	LVL1_ LVL2_ LVL2_1_ LVL2_2_ LVL2_3_ LVL2_4_ LVL3_
LVL1_DIR=	${PLUGIN_RESOURCE_DIR}
LVL2_DIR=	${PLUGIN_HEADER_DIR}
LVL2_1_DIR=	${PLUGIN_HEADER_DIR}/c-family
LVL2_2_DIR=	${PLUGIN_HEADER_DIR}/cp
LVL2_3_DIR=	${PLUGIN_HEADER_DIR}/objc
LVL2_4_DIR=	${PLUGIN_HEADER_DIR}/config
LVL3_DIR=	${PLUGIN_HEADER_DIR}/config/i386

LVL1_=		${OTOPDIR}/cc_tools/tools/gtype.state
LVL2_=		${PLUGIN_HEADERS}
LVL2_1_=	${PLUGIN_HEADERS_CFAMILY}
LVL2_2_=	${PLUGIN_HEADERS_CP}
LVL2_3_=	${PLUGIN_HEADERS_OBJC}
LVL2_4_=	${PLUGIN_HEADERS_CONFIG}
LVL3_=		${PLUGIN_HEADERS_CONFIG_I386}

.include <bsd.lib.mk>
