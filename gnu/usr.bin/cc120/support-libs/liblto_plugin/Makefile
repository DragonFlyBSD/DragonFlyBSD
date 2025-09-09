# include helpers to decide if lto plugin needs to be installed and where
.include "../../../Makefile.buo"
.include "../../../Makefile.cco"

# liblto_plugin.so needs to linked to a PIC version of libiberty
# A special tailed version of libiberty has been created for this purpose

GCC_NO_LIBS=	# defined
.include "Makefile.headers"
.include "../Makefile.inc"
.PATH: ${GCCDIR}/lto-plugin
.PATH: ${GCCDIR}/libiberty
.PATH: ${GCCDIR}/include
.PATH: ${GCCDIR}/gcc/ada/gcc-interface
.PATH: ${GCCDIR}/gcc/cp
.PATH: ${GCCDIR}/gcc/c
.PATH: ${GCCDIR}/gcc/objc
.PATH: ${GCCDIR}/libcpp/include

LIB=		lto_plugin
SHLIB_MAJOR=	0
NOPROFILE=	yes
NOINSTALLLIB=	yes
# For gcc -print-prog-name=liblto_plugin.so to work
LIBMODE=	555

# GCC driver looks for LTO plugin library in libexec, not lib
TARGET_LIBDIR=		/usr/libexec/gcc120
TARGET_SHLIBDIR=	/usr/libexec/gcc120

# Plugin support file locations
PLUGIN_RESOURCE_DIR=	/usr/lib/gcc120/plugin
PLUGIN_HEADER_DIR=	${PLUGIN_RESOURCE_DIR}/include

SRCS=		lto-plugin.c
LDADD=		../libiberty_pic/libiberty_pic.a

FILESGROUPS+=	LVL1_ LVL2_ LVL2_1_ LVL2_2_ LVL2_3_ LVL2_4_ LVL3_ LVL3_ADA_
LVL1_DIR=	${PLUGIN_RESOURCE_DIR}
LVL2_DIR=	${PLUGIN_HEADER_DIR}
LVL2_1_DIR=	${PLUGIN_HEADER_DIR}/c-family
LVL2_2_DIR=	${PLUGIN_HEADER_DIR}/cp
LVL2_3_DIR=	${PLUGIN_HEADER_DIR}/objc
LVL2_4_DIR=	${PLUGIN_HEADER_DIR}/config
LVL3_DIR=	${PLUGIN_HEADER_DIR}/config/i386
LVL3_ADA_DIR=	${PLUGIN_HEADER_DIR}/ada/gcc-interface

LVL1_=		${STOPDIR}/cc_prep/config/dragonfly-native.h \
		${OTOPDIR}/cc_tools/tools/gtype.state
LVL2_=		${PLUGIN_HEADERS} ${STOPDIR}/cc_prep/config.h
LVL2_1_=	${PLUGIN_HEADERS_CFAMILY}
LVL2_2_=	${PLUGIN_HEADERS_CP}
LVL2_3_=	${PLUGIN_HEADERS_OBJC}
LVL2_4_=	${PLUGIN_HEADERS_CONFIG}
LVL3_=		${PLUGIN_HEADERS_CONFIG_I386}
LVL3_ADA_=	${PLUGIN_HEADERS_ADA_GCC}

# SYMLINK lto plugin to primary/backup binutils bfd-plugin dirs
.if ${CC_PRIMARY} == "gcc120" # we are default gcc compiler
LTOPLUGIN=	liblto_plugin.so
.if exists(${DESTDIR}/usr/libexec/${BU_PRIMARY}/bfd-plugins)
BFDPLUGINS=	/usr/libexec/${BU_PRIMARY}/bfd-plugins
SYMLINKS+=	../../${CC_PRIMARY}/${LTOPLUGIN} ${BFDPLUGINS}/${LTOPLUGIN}
.endif
.if exists(${DESTDIR}/usr/libexec/${BU_BACKUP}/bfd-plugins)
BFDPLUGINSB=	/usr/libexec/${BU_BACKUP}/bfd-plugins
SYMLINKS+=	../../${CC_PRIMARY}/${LTOPLUGIN} ${BFDPLUGINSB}/${LTOPLUGIN}
.endif
.endif

.include <bsd.lib.mk>
