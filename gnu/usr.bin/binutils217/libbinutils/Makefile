# $DragonFly: src/gnu/usr.bin/binutils217/libbinutils/Makefile,v 1.2 2007/07/16 18:25:39 corecode Exp $

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils

LIB=	binutils
SRCS+=	arlex.c arparse.c ar.c arsup.c binemul.c bucomm.c budemang.c debug.c \
	dwarf.c emul_vanilla.c \
	filemode.c ieee.c rdcoff.c rddbg.c rename.c stabs.c unwind-ia64.c \
	wrstabs.c version.c
WARNS=	0
CFLAGS+= -DTARGET=\"${TARGET_ARCH}-just-dragonfly\"
CFLAGS+= -DBFD_VERSION_STRING=\"${VERSION}\"
CFLAGS+= -Dbin_dummy_emulation=bin_vanilla_emulation
CFLAGS+= -I${SRCDIR}/binutils
CFLAGS+= -I${SRCDIR}/bfd -I${.OBJDIR}/../libbfd
INTERNALLIB=	true

.include <bsd.lib.mk>
