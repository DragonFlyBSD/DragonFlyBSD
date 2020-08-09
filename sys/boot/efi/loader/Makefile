# $FreeBSD: head/sys/boot/efi/loader/Makefile 296769 2016-03-12 21:44:33Z emaste $

MAN=

MK_SSP=		no

PROG=		loader.sym
INTERNALPROG=
WARNS?=		3

# architecture-specific loader code
SRCS=	autoload.c \
	bootinfo.c \
	conf.c \
	copy.c \
	devicename.c \
	efi_main.c \
	i386_module.c \
	main.c \
	self_reloc.c \
	smbios.c \
	vers.c

.PATH: ${.CURDIR}/arch/${MACHINE}
# For smbios.c and i386_module.c
.PATH: ${.CURDIR}/../../pc32/libi386
.include "${.CURDIR}/arch/${MACHINE}/Makefile.inc"

BOOT_DLOADER=	yes

# DO NOT USE -static, this is a special case
.undef NOSHARED

CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.CURDIR}/arch/${MACHINE}
CFLAGS+=	-I${.CURDIR}/../include
CFLAGS+=	-I${.CURDIR}/../../../contrib/dev/acpica/source/include
CFLAGS+=	-I${.CURDIR}/../../..
CFLAGS+=	-I${.CURDIR}/../../pc32/libi386
CFLAGS+=	-I${.CURDIR}/../../dloader
CFLAGS+=	-DNO_PCI -DEFI

# make buildenv doesn't set DESTDIR, this means LIBSTAND
# will be wrong when crossbuilding.
.if exists(${.OBJDIR}/../../../../lib/libstand/libstand.a)
LIBSTAND=	${.OBJDIR}/../../../../lib/libstand/libstand.a
.endif

.if exists(${.OBJDIR}/../../dloader/libdloader.a)
LIBDLOADER=	${.OBJDIR}/../../dloader/libdloader.a
.endif

.if defined(EFI_STAGING_SIZE)
CFLAGS+=	-DEFI_STAGING_SIZE=${EFI_STAGING_SIZE}
.endif

# Always add MI sources
.PATH:		${.CURDIR}/../../common
.include	"${.CURDIR}/../../common/Makefile.inc"
CFLAGS+=	-I${.CURDIR}/../../common

FILES+=	loader.efi
FILESMODE_loader.efi=	${BINMODE}

LDSCRIPT=	${.CURDIR}/arch/${MACHINE}/ldscript.${MACHINE}
LDFLAGS+=	-Wl,-T${LDSCRIPT} -Wl,-Bsymbolic -shared

CLEANFILES+=	vers.c loader.efi

NEWVERSWHAT=	"EFI loader" ${MACHINE}

vers.c:	${.CURDIR}/../../common/newvers.sh ${.CURDIR}/../../efi/loader/version
	sh ${.CURDIR}/../../common/newvers.sh ${.CURDIR}/version ${NEWVERSWHAT}

NM?=		nm
OBJCOPY?=	objcopy

.if ${MACHINE_ARCH} == "x86_64"
EFI_TARGET=	pei-x86-64
.else
EFI_TARGET=	binary
.endif

all: loader.efi

loader.efi: ${PROG}
	if ${NM} ${.ALLSRC} | grep ' U '; then \
		echo "Undefined symbols in ${.ALLSRC}"; \
		exit 1; \
	fi
	# No "-j .eh_frame", otherwise the loader doesn't run.
	${OBJCOPY} -j .peheader -j .text -j .sdata -j .data \
		-j .dynamic -j .dynsym -j .rel.dyn \
		-j .rela.dyn -j .reloc -j set_Xcommand_set \
		--subsystem efi-app \
		--output-target=${EFI_TARGET} ${.ALLSRC} ${.TARGET}

LIBEFI=		${.OBJDIR}/../libefi/libefi.a

DPADD=		${LIBEFI} ${LIBSTAND} ${LIBDLOADER} ${LDSCRIPT}
LDADD=		${LIBEFI} ${LIBSTAND} ${LIBDLOADER}

.include <bsd.prog.mk>
