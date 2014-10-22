# $FreeBSD: src/sys/boot/common/Makefile.inc,v 1.16 2003/06/26 03:51:57 peter Exp $

SRCS+=	bcache.c boot.c commands.c console.c devopen.c
SRCS+=	interp_backslash.c interp_parse.c ls.c misc.c 
SRCS+=	module.c panic.c rel_open.c

.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64"
SRCS+=	load_elf32.c load_elf32_obj.c reloc_elf32.c
SRCS+=	load_elf64.c load_elf64_obj.c reloc_elf64.c
.endif

.if defined(LOADER_NET_SUPPORT)
SRCS+=	dev_net.c
.endif

# Machine-independant ISA PnP
.if HAVE_ISABUS
SRCS+=	isapnp.c
.endif
.if HAVE_PNP
SRCS+=	pnp.c
.endif

.if BOOT_DLOADER
SRCS+=	do_dloader.c
#MAN+=	../dloader/loader.dloader.8
.endif

MAN+=	loader.8
