GCC_NO_PATH=	yes
GCC_NO_LIBS=	yes
.include "../Makefile.inc"
.PATH: ${GCCDIR}/lto-plugin

LIB=		lto_plugin
SHLIB_MAJOR=	0
NOPROFILE=	yes
NOINSTALLLIB=	yes
# For gcc -print-prog-name=liblto_plugin.so to work
LIBMODE=	555

# GCC driver looks for LTO plugin library in libexec, not lib
TARGET_LIBDIR=		/usr/libexec/gcc${GCCSHORTVER}
TARGET_SHLIBDIR=	${TARGET_LIBDIR}

SRCS=		lto-plugin.c
VERSION_MAP=	${GCCDIR}/lto-plugin/lto-plugin.map

# liblto_plugin.so needs to be linked to a PIC version of libiberty.
# A specially tailored version of libiberty has been created for this purpose.
LIBIBERTY=	../libiberty_pic/libiberty_pic.a
LDADD=		${LIBIBERTY}
DPADD=		${LIBIBERTY}

# include helpers to decide if lto plugin needs to be installed and where
.include "../../../Makefile.buo"
.include "../../../Makefile.cco"
# SYMLINK lto plugin to primary/backup binutils bfd-plugin dirs
.if defined(IS_PRIMARY)
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
