# $FreeBSD: head/sys/boot/efi/loader/arch/amd64/Makefile.inc 281434 2015-04-11 10:36:48Z andrew $

SRCS+=	amd64_tramp.S \
	start.S \
	framebuffer.c \
	elf64_freebsd.c

.PATH:	${.CURDIR}/../../pc32/libi386
SRCS+=	nullconsole.c \
	comconsole.c

# Disable stack protector
CFLAGS+=	-fno-stack-protector

CFLAGS+=	-fPIC
CFLAGS+=	-I${.CURDIR}/../../../contrib/edk2/Include
CFLAGS+=	-I${.CURDIR}/../../../contrib/edk2/Include/X64
LDFLAGS+=	-Wl,-znocombreloc

FLAGS_GROUPS=	inttypes
inttypes_FLAGS=	-DACPI_USE_SYSTEM_INTTYPES
inttypes_FLAGS_FILES=elf64_freebsd.c
