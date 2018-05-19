# $FreeBSD: src/sys/boot/i386/cdboot/Makefile,v 1.5 2002/09/17 01:48:55 peter Exp $

ORG=		0x7c00

PROG=		cdboot
NOMAN=
STRIP=
BINDIR?=	/boot
CFLAGS+=	${ASSEMBLER_FLAGS}

${PROG}: ${PROG}.o
	${LD} ${LDFLAGS:N-Wl,*} -N -e start -Ttext ${ORG} -o ${PROG}.out ${PROG}.o
	objcopy -S -O binary ${PROG}.out ${.TARGET}

CLEANFILES+=	${PROG}.o ${PROG}.out

.include <bsd.prog.mk>
