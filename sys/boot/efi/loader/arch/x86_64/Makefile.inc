# $FreeBSD: head/sys/boot/efi/loader/arch/amd64/Makefile.inc 281434 2015-04-11 10:36:48Z andrew $

SRCS+=	amd64_tramp.S \
	start.S \
	framebuffer.c \
	elf64_freebsd.c

.PATH:	${.CURDIR}/../../i386/libi386
SRCS+=	nullconsole.c \
	comconsole.c

CFLAGS+=	-fPIC
LDFLAGS+=	-Wl,-znocombreloc
