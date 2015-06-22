.include "../Makefile.block1"

.PATH: ${SRCDIR}/binutils

LIB=	bu
SRCS=	bucomm.c version.c filemode.c

CFLAGS+=	-DHAVE_CONFIG_H -I${SRCDIR}/bfd \
		-Dbin_dummy_emulation=bin_vanilla_emulation \
		-DTARGET=\"${TARGET_CANONICAL}\"
INTERNALLIB=	true

${SRCS}: bfd.h

CLEANFILES=	bfd.h

.include <bsd.lib.mk>
	