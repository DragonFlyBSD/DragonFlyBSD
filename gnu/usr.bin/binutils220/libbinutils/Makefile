.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils

LIB=	binutils

SRCS+=	bucomm.c version.c filemode.c
SRCS+=	arparse.y arlex.l arsup.c rename.c binemul.c emul_vanilla.c
SRCS+=	rddbg.c debug.c stabs.c ieee.c rdcoff.c
SRCS+=	wrstabs.c
SRCS+=	dwarf.c prdbg.c unwind-ia64.c

WARNS=	0
CFLAGS+= -DTARGET=\"${TARGET_CANONICAL}\"
CFLAGS+= -Dbin_dummy_emulation=bin_vanilla_emulation
INTERNALLIB=	true

.include "Makefile.${TARGET_ARCH}"

.include <bsd.lib.mk>
