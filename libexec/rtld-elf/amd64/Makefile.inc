# $DragonFly: src/libexec/rtld-elf/amd64/Makefile.inc,v 1.1 2006/07/27 00:40:35 corecode Exp $
CFLAGS+=	-elf
LDFLAGS+=	-elf
# Uncomment this to build the dynamic linker as an executable instead
# of a shared library:
#LDSCRIPT=	${.CURDIR}/${MACHINE_ARCH}/elf_rtld.x
