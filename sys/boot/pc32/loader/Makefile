# $FreeBSD: src/sys/boot/i386/loader/Makefile,v 1.66 2003/06/26 03:51:57 peter Exp $
#

PROG=		loader
STRIP=
NEWVERSWHAT=	"bootstrap loader" i386
BINDIR?=	/boot
INSTALLFLAGS=	-b

# Notes: the 'loader' client is brought in with a virtual base of (typically)
# 0xa000 and a code offset of 0x0.  The btxldr is loaded at 0x200000 (?).  
#
# MEM_BTX_USR_ARG is not used in the boot2->loader stage, only in the
# boot1->boot2 stage (XXX what about the use in cdboot and pxeboot?)
#

# architecture-specific loader code
SRCS=		main.c conf.c

# Enable PXE TFTP or NFS support, not both.
#CFLAGS+=	-DLOADER_TFTP_SUPPORT
CFLAGS+=	-DLOADER_NFS_SUPPORT

# Enable PnP and ISA-PnP code.
HAVE_PNP=	yes
HAVE_ISABUS=	yes

BOOT_DLOADER=	yes
CFLAGS+=	-I${.CURDIR}/../../dloader -I${.CURDIR}/../../dloader/i386
.if exists(${.OBJDIR}/../../dloader/libdloader.a)
LIBDLOADER=	${.OBJDIR}/../../dloader/libdloader.a
.else
LIBDLOADER=	${.CURDIR}/../../dloader/libdloader.a
.endif

.if defined(LOADER_BZIP2_SUPPORT)
CFLAGS+=	-DLOADER_BZIP2_SUPPORT
.endif
.if !defined(LOADER_NO_GZIP_SUPPORT)
CFLAGS+=	-DLOADER_GZIP_SUPPORT
.endif

# Always add MI sources 
.PATH:		${.CURDIR}/../../common
.include	<${.CURDIR}/../../common/Makefile.inc>
CFLAGS+=	-I${.CURDIR}/../../common
CFLAGS+=	-I${.CURDIR}/../../.. -I.

CLEANFILES+=	vers.c vers.o ${PROG}.list ${PROG}.bin ${PROG}.sym ${PROG}.help

CFLAGS+=		-Wall
COMPILER_LDFLAGS=	-nostdlib -static
COMPILER_LDFLAGS+=	-Wl,--script,${.CURDIR}/loader.ldscript

# i386 standalone support library
LIBI386=	${.OBJDIR}/../libi386/libi386.a
CFLAGS+=	-I${.CURDIR}/..

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

# BTX components
.if exists(${.OBJDIR}/../btx)
BTXDIR=		${.OBJDIR}/../btx
.else
BTXDIR=		${.CURDIR}/../btx
.endif
BTXLDR=		${BTXDIR}/btxldr/btxldr
BTXKERN=	${BTXDIR}/btx/btx
BTXCRT=		${BTXDIR}/lib/crt0.o
CFLAGS+=	-I${.CURDIR}/../btx/lib

# Debug me!
#CFLAGS+=	-g

vers.o:	${.CURDIR}/../../common/newvers.sh ${.CURDIR}/version
	sh ${.CURDIR}/../../common/newvers.sh ${.CURDIR}/version ${NEWVERSWHAT}
	${CC} ${CFLAGS} -c vers.c

${PROG}: ${PROG}.bin ${BTXLDR} ${BTXKERN} ${BTXCRT}
	btxld -v -f aout -e ${LOADER_ADDRESS} -o ${.TARGET} -l ${BTXLDR} \
		-b ${BTXKERN} ${PROG}.bin

${PROG}.bin: ${PROG}.sym
	cp ${.ALLSRC} ${.TARGET}
	strip -R .comment -R .note -R .note.gnu.gold-version ${.TARGET}

${PROG}.help: help.common help.i386
	cat ${.ALLSRC} | awk -f ${.CURDIR}/../../common/merge_help.awk > ${.TARGET}

.PATH: ${.CURDIR}/../../dloader
FILES=	${PROG}.help loader.conf loader-bootp.conf dloader.menu
FILESDIR_loader.conf=	/boot/defaults
FILESDIR_loader-bootp.conf=	/boot/defaults
FILESDIR_dloader.menu=	/boot/defaults

.if !exists(${DESTDIR}/boot/dloader.rc)
FILES+=	${.CURDIR}/dloader.rc
.endif

.include "../Makefile.inc"

# Cannot use ${OBJS} above this line
.include <bsd.prog.mk>

${PROG}.sym: ${OBJS} ${LIBI386} ${LIBSTAND} ${LIBDLOADER} vers.o
	${CC} ${COMPILER_LDFLAGS} -o ${.TARGET} ${BTXCRT} ${OBJS} vers.o \
		${LIBDLOADER} ${LIBI386} ${LIBSTAND}

# If it's not there, don't consider it a target
.if exists(${.CURDIR}/../../../i386/include)
beforedepend ${OBJS}: machine

machine:
	${LN} -sf ${.CURDIR}/../../../i386/include machine

.endif

CLEANFILES+=	machine
