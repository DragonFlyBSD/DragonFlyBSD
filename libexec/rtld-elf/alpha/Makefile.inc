# $FreeBSD: src/libexec/rtld-elf/alpha/Makefile.inc,v 1.1.4.1 2002/09/02 02:10:20 obrien Exp $
# $DragonFly: src/libexec/rtld-elf/alpha/Attic/Makefile.inc,v 1.2 2003/06/17 04:27:08 dillon Exp $

CFLAGS+=	-mno-fp-regs
LDFLAGS+=	-Wl,--script,${.CURDIR}/alpha/ld.so.script
