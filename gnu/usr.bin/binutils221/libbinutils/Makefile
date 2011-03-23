.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils

LIB=	binutils

SRCS+=	bucomm.c	\
	version.c	\
	filemode.c	\
	arparse.y	\
	arlex.l		\
	arsup.c		\
	rename.c	\
	binemul.c	\
	emul_vanilla.c	\
	rddbg.c		\
	debug.c		\
	stabs.c		\
	ieee.c		\
	rdcoff.c	\
	wrstabs.c	\
	dwarf.c		\
	prdbg.c		\
	unwind-ia64.c

WARNS=	0
CFLAGS+= -DTARGET=\"${TARGET_CANONICAL}\"
CFLAGS+= -Dbin_dummy_emulation=bin_vanilla_emulation
INTERNALLIB=	true

.include "Makefile.${TARGET_ARCH}"

.include <bsd.lib.mk>
