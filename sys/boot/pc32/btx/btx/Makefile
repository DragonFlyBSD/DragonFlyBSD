# $FreeBSD: src/sys/boot/i386/btx/btx/Makefile,v 1.13 2002/09/17 01:48:54 peter Exp $
# $DragonFly: src/sys/boot/pc32/btx/btx/Makefile,v 1.4 2004/07/18 23:40:04 dillon Exp $

.if defined(PAGING)
AFLAGS+=	-DPAGING
.endif

.if defined(BOOT_BTX_NOHANG)
BOOT_BTX_FLAGS=0x1
.else
BOOT_BTX_FLAGS=0x0
.endif

AFLAGS+=	-DBTX_FLAGS=${BOOT_BTX_FLAGS}

.if defined(BTX_SERIAL)
BOOT_COMCONSOLE_PORT?= 0x3f8
BOOT_COMCONSOLE_SPEED?= 9600
B2SIOFMT?=	0x3

AFLAGS+=	-DBTX_SERIAL -DSIOPRT=${BOOT_COMCONSOLE_PORT} \
		-DSIOFMT=${B2SIOFMT} -DSIOSPD=${BOOT_COMCONSOLE_SPEED}
.endif

ORG=	0x9000

all: btx

btx: btx.o
	${LD} -N -e start -Ttext ${ORG} -o btx.out btx.o
	objcopy -S -O binary btx.out ${.TARGET}

btx.o: btx.S
	${CC} ${CFLAGS} ${AFLAGS} ${.IMPSRC} -o ${.TARGET} -c

CLEANFILES+= btx btx.out btx.o

.include <bsd.prog.mk>
