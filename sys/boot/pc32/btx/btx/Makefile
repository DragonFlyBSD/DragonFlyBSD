# $FreeBSD: src/sys/boot/i386/btx/btx/Makefile,v 1.13 2002/09/17 01:48:54 peter Exp $

.if defined(BOOT_BTX_NOHANG)
BOOT_BTX_FLAGS=0x1
.else
BOOT_BTX_FLAGS=0x0
.endif

CFLAGS+=	-DBTX_FLAGS=${BOOT_BTX_FLAGS}

.if defined(BTX_SERIAL)
BOOT_COMCONSOLE_PORT?= 0x3f8
BOOT_COMCONSOLE_SPEED?= 115200
B2SIOFMT?=	0x3

CFLAGS+=	-DBTX_SERIAL -DSIOPRT=${BOOT_COMCONSOLE_PORT} \
		-DSIOFMT=${B2SIOFMT} -DSIOSPD=${BOOT_COMCONSOLE_SPEED}
.endif

all: btx

btx: btx.o
	${LD} ${LDFLAGS:N-Wl,*} -N -e start \
	--script ${.CURDIR}/btx.ldscript -o btx.out btx.o
	objcopy -S -O binary btx.out ${.TARGET}

btx.o: btx.S
	${CC} ${CFLAGS} ${.IMPSRC} -o ${.TARGET} -c

CLEANFILES+= btx btx.out btx.o

.include <bsd.prog.mk>
