CSUDIR=	${.CURDIR}/${MACHINE_ARCH}
SED_FIX_NOTE= -i "" -e '/\.note\.tag/s/progbits/note/'
# Since gcc-8.2 (pr77609) gcc no longer emits @progbits for .note.tag section.
# If no section type was set, append @note type for legacy reasons.
SED_FIX_NOTE+= -e '/\.note\.tag.*"a"$$/s/$$/,@note/'

.include "${.CURDIR}/${MACHINE_ARCH}/Makefile.csu"

OBJS+=	${SRCS:M*.[cS]:R:C/$/.o/g}

all: ${OBJS}

realinstall:
	${INSTALL} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${OBJS} ${DESTDIR}${LIBDIR}


.include <bsd.lib.mk>
