# $FreeBSD: src/gnu/usr.bin/binutils/libbinutils/Makefile,v 1.8.2.5 2002/09/01 23:39:16 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils214/libbinutils/Attic/Makefile,v 1.1 2004/02/01 08:53:04 dillon Exp $

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils

LIB=	binutils
SRCS+=	arlex.l arparse.y ar.c arsup.c binemul.c bucomm.c budemang.c debug.c \
	emul_vanilla.c \
	filemode.c ieee.c rdcoff.c rddbg.c rename.c stabs.c unwind-ia64.c \
	wrstabs.c version.c
WARNS=	0
CFLAGS+= -DTARGET=\"${TARGET_ARCH}-unknown-dragonfly\"
CFLAGS+= -DBFD_VERSION_STRING=\"${VERSION}\"
CFLAGS+= -Dbin_dummy_emulation=bin_vanilla_emulation
CFLAGS+= -I${SRCDIR}/binutils
CFLAGS+= -I${SRCDIR}/bfd -I${.OBJDIR}/../libbfd
INTERNALLIB=	true

.include <bsd.lib.mk>
