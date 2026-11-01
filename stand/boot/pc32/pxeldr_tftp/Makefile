# Works like the normal loader except the kernel, modules, and other loader
# files are loaded via TFTP instead of NFS.
#
# $DragonFly: src/sys/boot/pc32/pxeldr_tftp/Makefile,v 1.1 2005/09/03 23:52:53 dillon Exp $

ORG=		0x7c00

LDR=		pxeldr
BOOT=		pxeboot_tftp
LOADER=		loader_tftp
PROG=		${BOOT}
MAN=		${BOOT}.8
STRIP=
BINDIR?=	/boot

.PATH: ${.CURDIR}/../pxeldr

.if exists(${.OBJDIR}/../${LOADER})
LOADERBIN=	${.OBJDIR}/../${LOADER}/loader_tftp.bin
.else
LOADERBIN=	${.CURDIR}/../${LOADER}/loader_tftp.bin
.endif

.include "${.CURDIR}/../pxeldr/Makefile"
