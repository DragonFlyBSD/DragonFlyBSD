# $FreeBSD: src/sys/boot/sparc64/loader/Makefile,v 1.15 2002/12/20 04:32:10 jake Exp $
# $DragonFly: src/sys/boot/sparc64/loader/Makefile,v 1.2 2005/07/07 11:49:57 corecode Exp $

BASE=		loader
PROG=		${BASE}
STRIP=
NEWVERSWHAT=	"bootstrap loader" sparc64
BINDIR?=	/boot
INSTALLFLAGS=	-b

# Architecture-specific loader code
SRCS=		locore.S main.c metadata.c vers.c

LOADER_DISK_SUPPORT?=	yes
LOADER_UFS_SUPPORT?=	yes
LOADER_CD9660_SUPPORT?=	yes
LOADER_NET_SUPPORT?=	yes
LOADER_NFS_SUPPORT?=	yes
LOADER_TFTP_SUPPORT?=	yes
LOADER_GZIP_SUPPORT?=	yes
LOADER_BZIP2_SUPPORT?=	no

.if ${LOADER_DISK_SUPPORT} == "yes"
CFLAGS+=	-DLOADER_DISK_SUPPORT
.endif
.if ${LOADER_UFS_SUPPORT} == "yes"
CFLAGS+=	-DLOADER_UFS_SUPPORT
.endif
.if ${LOADER_CD9660_SUPPORT} == "yes"
CFLAGS+=	-DLOADER_CD9660_SUPPORT
.endif
.if ${LOADER_GZIP_SUPPORT} == "yes"
CFLAGS+=	-DLOADER_GZIP_SUPPORT
.endif
.if ${LOADER_BZIP2_SUPPORT} == "yes"
CFLAGS+=	-DLOADER_BZIP2_SUPPORT
.endif
.if ${LOADER_NET_SUPPORT} == "yes"
CFLAGS+=	-DLOADER_NET_SUPPORT
.endif
.if ${LOADER_NFS_SUPPORT} == "yes"
CFLAGS+=	-DLOADER_NFS_SUPPORT
.endif
.if ${LOADER_TFTP_SUPPORT} == "yes"
CFLAGS+=	-DLOADER_TFTP_SUPPORT
.endif

BOOT_DLOADER=	yes
CFLAGS+=	-I${.CURDIR}/../../dloader -I${.CURDIR}/../../dloader/sparc64
.if exists(${.OBJDIR}/../../dloader/libdloader.a)
LIBDLOADER=	${.OBJDIR}/../../dloader/libdloader.a
.else
LIBDLOADER=	${.CURDIR}/../../dloader/libdloader.a
.endif

# Always add MI sources 
.PATH:		${.CURDIR}/../../common
.include	<${.CURDIR}/../../common/Makefile.inc>
CFLAGS+=	-I${.CURDIR}/../../common
CFLAGS+=	-I${.CURDIR}/../../.. -I.

CLEANFILES+=	vers.c ${BASE}.help

CFLAGS+=	-ffreestanding
LDFLAGS=	-nostdlib -static

# Openfirmware standalone support library
LIBOFW=		${.OBJDIR}/../../ofw/libofw/libofw.a
CFLAGS+=	-I${.CURDIR}/../../ofw/libofw/

# where to get libstand from
#XXX need a better way to do this
LIBSTAND=	${.CURDIR}/../../../../lib/libstand/libstand.a
.if !exists(${LIBSTAND})
LIBSTAND=	${.OBJDIR}/../../../../lib/libstand/libstand.a
.if !exists(${LIBSTAND})
LIBSTAND=	-lstand
.endif
.endif
CFLAGS+=	-I${.CURDIR}/../../../../lib/libstand/

LDADD=		${LIBDLOADER} ${LIBOFW} ${LIBSTAND}

vers.c:	${.CURDIR}/../../common/newvers.sh ${.CURDIR}/version
	sh ${.CURDIR}/../../common/newvers.sh ${.CURDIR}/version ${NEWVERSWHAT}

${BASE}.help: help.common help.sparc64
	cat ${.ALLSRC} | \
	    awk -f ${.CURDIR}/../../common/merge_help.awk > ${.TARGET}

.PATH: ${.CURDIR}/../../dloader
FILES=	${BASE}.help dloader.conf
FILESDIR_loader.conf=	/boot/defaults

.if !exists(${DESTDIR}/boot/rloader.rc)
FILES+= rloader.rc
.endif

# There are no things relevant to all boot parts of FreeBSD/sparc64 yet.
#.include <${.CURDIR}/../Makefile.inc>

.include <bsd.prog.mk>

.if exists(${.CURDIR}/../../../sparc64/include)
beforedepend ${OBJS}: machine

machine:
	${LN} -sf ${.CURDIR}/../../../sparc64/include machine
.endif

CLEANFILES+=	machine
