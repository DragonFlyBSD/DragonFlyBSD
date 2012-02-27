# $FreeBSD: src/sys/boot/i386/btx/btxldr/Makefile,v 1.12 2003/06/26 03:51:57 peter Exp $

CFLAGS+= -DLOADER_ADDRESS=${LOADER_ADDRESS}

# BTXLDR_VERBOSE=YES
.if defined(BTXLDR_VERBOSE)
CFLAGS+= -DBTXLDR_VERBOSE
.endif

all: btxldr

btxldr: btxldr.o
	${LD} ${LDFLAGS} -N -e start -Ttext ${LOADER_ADDRESS} \
	--script ${.CURDIR}/btxldr.ldscript -o btxldr.out btxldr.o
	objcopy -S -O binary btxldr.out ${.TARGET}

btxldr.o: btxldr.S
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET} -c

CLEANFILES+= btxldr btxldr.out btxldr.o

.include <bsd.prog.mk>
