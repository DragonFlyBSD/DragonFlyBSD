# $FreeBSD: src/sys/boot/powerpc/loader/Makefile,v 1.11 2003/08/16 02:48:20 obrien Exp $
# $DragonFly: src/sys/boot/powerpc/loader/Makefile,v 1.2 2005/07/07 11:49:57 corecode Exp $

BASE=		loader
PROG=		${BASE}
NOMAN=
STRIP=
NEWVERSWHAT=	"bootstrap loader" OpenFirmware/PowerPC
BINDIR?=	/boot
INSTALLFLAGS=	-b

LOADER_DISK_SUPPORT?=	yes
LOADER_NET_SUPPORT?=	yes

CFLAGS+=	-ffreestanding
# load address
RELOC?=		0x6c0000
CFLAGS+=	-DRELOC=${RELOC}

# architecture-specific loader code
SRCS=		conf.c metadata.c

# Pull in common loader code
.PATH:		${.CURDIR}/../../ofw/common
.include	<${.CURDIR}/../../ofw/common/Makefile.inc>

.if defined(LOADER_DISK_SUPPORT)
CFLAGS+=	-DLOADER_DISK_SUPPORT
.endif
.if defined(LOADER_NET_SUPPORT)
CFLAGS+=	-DLOADER_NET_SUPPORT
.endif

###BOOT_DLOADER=	yes
###CFLAGS+=	-I${.CURDIR}/../../dloader -I${.CURDIR}/../../dloader/powerpc
###.if exists(${.OBJDIR}/../../dloader/libdloader.a)
###LIBDLOADER=	${.OBJDIR}/../../dloader/libdloader.a
###.else
###LIBDLOADER=	${.CURDIR}/../../dloader/libdloader.a
###.endif
BOOT_DEFAULT=	yes

# Always add MI sources 
.PATH:		${.CURDIR}/../../common
.include	<${.CURDIR}/../../common/Makefile.inc>
CFLAGS+=	-I${.CURDIR}/../../common
CFLAGS+=	-I${.CURDIR}/../../.. -I.

CLEANFILES+=	vers.c vers.o ${BASE}.list ${BASE}.bin ${BASE}.sym ${BASE}.help

LDFLAGS=	-nostdlib -static -Ttext ${RELOC}

# OpenFirmware standalone support library
LIBOFW=		${.OBJDIR}/../../ofw/libofw/libofw.a
CFLAGS+=	-I${.CURDIR}/../../ofw/libofw

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

# OpenFirmware is expecting ELF components
CFLAGS+=	-elf

# Debug me!
CFLAGS+=	-g
LDFLAGS+=	-g

vers.o:	${.CURDIR}/../../common/newvers.sh ${.CURDIR}/version
	sh ${.CURDIR}/../../common/newvers.sh ${.CURDIR}/version ${NEWVERSWHAT}
	${CC} -c vers.c

${BASE}.help: help.common help.ofw
	cat ${.ALLSRC} | awk -f ${.CURDIR}/../../common/merge_help.awk > ${.TARGET}

beforeinstall:
.if exists(${.OBJDIR}/loader.help)
	${INSTALL} -C -o ${BINOWN} -g ${BINGRP} -m 444 \
		${.OBJDIR}/${BASE}.help ${DESTDIR}/boot
.else
	${INSTALL} -C -o ${BINOWN} -g ${BINGRP} -m 444 \
		${.CURDIR}/${BASE}.help ${DESTDIR}/boot
.endif
.if !exists(${DESTDIR}/boot/dloader.rc)
	${INSTALL} -C -o ${BINOWN} -g ${BINGRP} -m 444 \
		${.CURDIR}/../../dloader/dloader.rc ${DESTDIR}/boot
.endif

#	${INSTALL} -C -o ${BINOWN} -g ${BINGRP} -m 444 \
#		${.CURDIR}/../../forth/loader.4th ${DESTDIR}/boot
#	${INSTALL} -C -o ${BINOWN} -g ${BINGRP} -m 444 \
#		${.CURDIR}/../../forth/support.4th ${DESTDIR}/boot
#	${INSTALL} -C -o ${BINOWN} -g ${BINGRP} -m 444 \
#		${.CURDIR}/../../forth/loader.conf ${DESTDIR}/boot/defaults

${PROG}: ${OBJS} ${LIBOFW} ${LIBSTAND} ${LIBDLOADER} start.o vers.o
	${LD} ${LDFLAGS} -o ${.TARGET} start.o ${OBJS} \
		vers.o ${LIBDLOADER} ${LIBOFW} ${LIBSTAND}

machine:
	${LN} -sf ${.CURDIR}/../../../powerpc/include machine

# Cannot use ${OBJS} above this line
.include <bsd.prog.mk>

beforedepend ${OBJS}: machine

CLEANFILES+=	machine setdefs.h setdef0.c setdef1.c setdef0.o setdef1.o \
		start.o
