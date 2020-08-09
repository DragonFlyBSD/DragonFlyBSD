# $FreeBSD: head/sys/boot/efi/boot1/Makefile 296769 2016-03-12 21:44:33Z emaste $

MAN=

PROG=		boot1.sym
INTERNALPROG=
WARNS?=		6

# architecture-specific loader code
SRCS=	boot1.c self_reloc.c start.S ufs_module.c

# DO NOT USE -static, this is a special case
.undef NOSHARED

CFLAGS+=	-I.
CFLAGS+=	-I${.CURDIR}/../include
CFLAGS+=	-I${.CURDIR}/../../../contrib/dev/acpica/source/include
CFLAGS+=	-I${.CURDIR}/../../../contrib/edk2/Include
CFLAGS+=	-I${.CURDIR}/../../..
CFLAGS+=	-DEFI_UFS_BOOT

.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+=	-I${.CURDIR}/../../../contrib/edk2/Include/X64
.endif

# Disable stack protector
CFLAGS+=	-fno-stack-protector

.ifdef(EFI_DEBUG)
CFLAGS+=	-DEFI_DEBUG
.endif

# Always add MI sources and REGULAR efi loader bits
.PATH:		${.CURDIR}/../loader/arch/${MACHINE}
.PATH:		${.CURDIR}/../loader
.PATH:		${.CURDIR}/../../common
CFLAGS+=	-I${.CURDIR}/../../common

FILES=	boot1.efi
FILESMODE_boot1.efi=	${BINMODE}

LDSCRIPT=	${.CURDIR}/../loader/arch/${MACHINE}/ldscript.${MACHINE}
LDFLAGS+=	-Wl,-T${LDSCRIPT} -Wl,-Bsymbolic -shared

.if ${MACHINE_ARCH} == "aarch64"
CFLAGS+=	-msoft-float -mgeneral-regs-only
.endif
.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+=	-fPIC
LDFLAGS+=	-Wl,-znocombreloc
.endif

.if exists(${.OBJDIR}/../libefi/libefi.a)
LIBEFI=		${.OBJDIR}/../libefi/libefi.a
.endif
.if exists(${.OBJDIR}/../../../../lib/libstand/libstand.a)
LIBSTAND=	${.OBJDIR}/../../../../lib/libstand/libstand.a
.endif

#
# Add libstand for the runtime functions used by the compiler - for example
# __aeabi_* (arm) or __divdi3 (i386).
# as well as required string and memory functions for all platforms.
#
DPADD+=		${LIBEFI} ${LIBSTAND}
LDADD+=		${LIBEFI} ${LIBSTAND}

DPADD+=		${LDSCRIPT}

NM?=		nm
OBJCOPY?=	objcopy

.if ${MACHINE_ARCH} == "x86_64"
EFI_TARGET=	pei-x86-64
.else
EFI_TARGET=	binary
.endif

all:	boot1.efi

boot1.efi: ${PROG}
	if ${NM} ${.ALLSRC} | grep ' U '; then \
		echo "Undefined symbols in ${.ALLSRC}"; \
		exit 1; \
	fi
	${OBJCOPY} -j .peheader -j .text -j .sdata -j .data \
		-j .dynamic -j .dynsym -j .rel.dyn \
		-j .rela.dyn -j .reloc \
		--subsystem efi-app \
		--output-target=${EFI_TARGET} ${.ALLSRC} ${.TARGET}

boot1.o: ${.CURDIR}/../../common/ufsread.c

# The following inserts our objects into a template FAT file system
# created by generate-fat.sh

# XXX generate-fat.sh needs porting
#.include "${.CURDIR}/Makefile.fat"
#BOOT1_MAXSIZE?=	131072

#boot1.efifat: boot1.efi
#	@set -- `ls -l boot1.efi`; \
#	x=$$(($$5-${BOOT1_MAXSIZE})); \
#	if [ $$x -ge 0 ]; then \
#	    echo "boot1 $$x bytes too large; regenerate FAT templates?" >&2 ;\
#	    exit 1; \
#	fi
#	echo ${.OBJDIR}
#	uudecode ${.CURDIR}/fat-${MACHINE}.tmpl.bz2.uu
#	mv fat-${MACHINE}.tmpl.bz2 ${.TARGET}.bz2
#	bzip2 -f -d ${.TARGET}.bz2
#	dd if=boot1.efi of=${.TARGET} seek=${BOOT1_OFFSET} conv=notrunc \
#	    status=none

CLEANFILES= boot1.efi

.include <bsd.prog.mk>
