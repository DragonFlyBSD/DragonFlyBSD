# $FreeBSD: src/sys/boot/i386/pxeldr/Makefile,v 1.9 2003/09/03 08:12:20 phk Exp $
# $DragonFly: src/sys/boot/pc32/pxeldr/Makefile,v 1.5 2004/07/18 23:40:09 dillon Exp $

ORG=		0x7c00

LDR=		pxeldr
BOOT=		pxeboot
LOADER=		loader
PROG=		${BOOT}
MAN=		${BOOT}.8
STRIP=
BINDIR?=	/boot

.if defined(BOOT_PXELDR_PROBE_KEYBOARD)
AFLAGS+=	-DPROBE_KEYBOARD
.endif

.if defined(BOOT_PXELDR_ALWAYS_SERIAL)
AFLAGS+=	-DALWAYS_SERIAL
.endif


.if exists(${.OBJDIR}/../loader)
LOADERBIN=	${.OBJDIR}/../loader/loader.bin
.else
LOADERBIN=	${.CURDIR}/../loader/loader.bin
.endif

# BTX components
.if exists(${.OBJDIR}/../btx)
BTXDIR=		${.OBJDIR}/../btx
.else
BTXDIR=		${.CURDIR}/../btx
.endif
BTXLDR=		${BTXDIR}/btxldr/btxldr
BTXKERN=	${BTXDIR}/btx/btx
BTXCRT=		${BTXDIR}/lib/crt0.o

${BOOT}: ${LDR} ${LOADER}
	cat ${LDR} ${LOADER} > ${.TARGET}.tmp
	dd if=${.TARGET}.tmp of=${.TARGET} obs=2k conv=osync
	rm ${.TARGET}.tmp

${LOADER}: ${LOADERBIN} ${BTXLDR} ${BTXKERN} ${BTXCRT}
	btxld -v -f aout -e ${LOADER_ADDRESS} -o ${.TARGET} -l ${BTXLDR} \
	    -b ${BTXKERN} ${LOADERBIN}

${LDR}: ${LDR}.o
	${LD} -N -e start -Ttext ${ORG} -o ${LDR}.out ${LDR}.o
	objcopy -S -O binary ${LDR}.out ${.TARGET}

${LDR}.o: ${LDR}.S
	${CC} ${CFLAGS} ${AFLAGS} ${.IMPSRC} -o ${LDR}.o -c

CLEANFILES+= ${LDR} ${LDR}.out ${LDR}.o ${BOOT}.tmp ${LOADER}

.include <bsd.prog.mk>
