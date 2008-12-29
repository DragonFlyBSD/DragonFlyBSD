# $FreeBSD: src/sys/boot/i386/boot2/Makefile,v 1.47 2003/06/26 03:51:57 peter Exp $
# $DragonFly: src/sys/boot/pc32/boot2/Makefile,v 1.18 2008/09/13 11:45:45 corecode Exp $

BINDIR?=	/boot
BINMODE=	444
CLEANFILES=	boot \
		sio.o

NM?=		nm
NXCFLAGS=
NXLDFLAGS=

.PATH: ${.CURDIR}/..

# A value of 0x80 enables LBA support.
B1FLAGS=	0x80

BOOT_COMCONSOLE_PORT?= 0x3f8
BOOT_COMCONSOLE_SPEED?= 9600
B2SIOFMT?=	0x3

.if exists(${.OBJDIR}/../btx)
BTX=	${.OBJDIR}/../btx
.else
BTX=	${.CURDIR}/../btx
.endif

# These origins are taken from bootasm.h.
#
REL1 = `${.OBJDIR}/bootasmdef.nx BOOT1_ORIGIN`
REL1D = `${.OBJDIR}/bootasmdef.nx -d BOOT1_ORIGIN`
ORG1 = `${.OBJDIR}/bootasmdef.nx MEM_BIOS_LADDR`
ORG1D = `${.OBJDIR}/bootasmdef.nx -d MEM_BIOS_LADDR`

# WARNING!  The ORG2 calculation is the origin of boot2.bin relative to 
# the start of the BTX *USER* address space, not the start of physical 
# memory.
#
ORG2=	`${.OBJDIR}/bootasmdef.nx BOOT2_VORIGIN`

# gcc3.x
#	-fno-guess-branch-probability
#	-mno-align-long-strings
CFLAGS=	-elf -ffreestanding -Os -fno-builtin \
	-fomit-frame-pointer \
	-std=c99 \
	-DBOOT2 \
	-I${.CURDIR}/../../common \
	-I${.CURDIR}/../../../../lib/libstand \
	-I${.CURDIR}/../btx/lib -I. \
	-Wall -Waggregate-return -Wbad-function-cast -Wcast-align \
	-Wmissing-declarations -Wmissing-prototypes -Wnested-externs \
	-Wpointer-arith -Wshadow -Wstrict-prototypes -Wwrite-strings \
	-D__BOOT2_HACK

# Unfortunately, unit-at-a-time creates issues as yet
# not tracked down, in boot2, so we have to turn it off.  Note that
# the problem has been narrowed down to the 'boot2' code.  The /boot/loader
# doesn't seem to have a problem.
#
CFLAGS+= -fno-unit-at-a-time

# Disable propolice on gcc that supports it
.if ${CCVER} == "gcc34"
CFLAGS+= -fno-stack-protector
.endif

NSECT_32?=	16
NSECT_64?=	18
LDFLAGS=-nostdlib -static -N --gc-sections

all: boot

boot: boot1_32 boot2_32
	cat ${.ALLSRC} > boot

.for s in _32 _64

_ADDCFLAGS$s:= -DNSECT=${NSECT$s}
_ts=	$s
.if ${_ts} == "_64"
_ADDCFLAGS$s+= -DDISKLABEL64
_ADDCFLAGS$s+= -DHAMMERFS
.else
_ADDCFLAGS$s+= -DUFS1_ONLY
.endif

all: boot1$s boot2$s

boot1$s: boot1$s.out
	objcopy -S -O binary boot1$s.out ${.TARGET}

boot1$s.out: boot1$s.o bootasmdef.nx
	${LD} ${LDFLAGS} -e start -Ttext ${ORG1} -o ${.TARGET} ${.ALLSRC:M*.o}

boot1$s.o: boot1.S
	${CC} ${CFLAGS} ${_ADDCFLAGS$s} -DFLAGS=${B1FLAGS} \
		${.ALLSRC} -o ${.TARGET} -c

boot2$s.o: boot2.c ${.CURDIR}/../../common/ufsread.c ${.CURDIR}/../../../../lib/libstand/hammerread.c
	${CC} ${CFLAGS} ${_ADDCFLAGS$s} -S -o boot2$s.s.tmp ${.ALLSRC:M*/boot2.c}
	sed -e '/align/d' -e '/nop/d' < boot2$s.s.tmp > boot2$s.s
	rm -f boot2$s.s.tmp
	${AS} ${AFLAGS} -o boot2$s.o boot2$s.s

boot2$s.h: boot1$s.out bootasmdef.nx
	@echo "NOTE: boot2 using xread from boot1 at ${REL1}"
	${NM} -t d ${.ALLSRC} | awk '/([0-9])+ T xread/ \
	 	{ x = $$1 - ORG1; \
		printf("#define XREADORG %#x\n", REL1 + x) }' \
	 	ORG1=${ORG1D} REL1=${REL1D} > boot2$s.h

boot2$s: boot2$s.ldr boot2$s.bin ${BTX}/btx/btx
	btxld -v -E ${ORG2} -f bin -b ${BTX}/btx/btx -l boot2$s.ldr \
		-o boot2$s.ld -P 1 boot2$s.bin
	stat boot2$s.ld | awk '{ x = (${NSECT$s} - 1) * 512 - $$8; \
		print x " bytes available"; if (x < 0) exit 1 }'
	dd if=boot2$s.ld of=${.TARGET} obs=$$(( (${NSECT$s} - 1) * 512)) conv=osync 2>/dev/null

boot2$s.ldr:
	dd if=/dev/zero of=${.TARGET} bs=512 count=1 2>/dev/null

boot2$s.bin: boot2$s.out
	objcopy -S -O binary boot2$s.out ${.TARGET}

boot2$s.out: boot2$s.o sio.o
	${LD} ${LDFLAGS} -Ttext ${ORG2} -o ${.TARGET} \
		${BTX}/lib/crt0.o ${.ALLSRC}

boot2$s.o: boot2$s.h

CLEANFILES+=	boot1$s boot1$s.out boot1$s.o \
		boot2$s boot2$s.ldr boot2$s.bin boot2$s.ld boot2$s.out boot2$s.o boot2$s.h boot2$s.s

.endfor

sio.o: sio.S
	${CC} -DSIOPRT=${BOOT_COMCONSOLE_PORT} -DSIOFMT=${B2SIOFMT} \
		-DSIOSPD=${BOOT_COMCONSOLE_SPEED} \
		${.ALLSRC} -o ${.TARGET} -c

install:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		boot ${DESTDIR}${BINDIR}/boot
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		boot1_32 ${DESTDIR}${BINDIR}/boot1
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		boot2_32 ${DESTDIR}${BINDIR}/boot2
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		boot1_64 ${DESTDIR}${BINDIR}/boot1_64
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		boot2_64 ${DESTDIR}${BINDIR}/boot2_64

.include <bsd.prog.mk>

.if defined(REALLY_AMD64)
boot2.o: machine

beforedepend ${OBJS}: machine

machine:
	${LN} -sf ${.CURDIR}/../../../i386/include machine

CLEANFILES+=	machine
.endif

CLEANFILES+=	bootasmdef.nx

