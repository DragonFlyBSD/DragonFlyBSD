# $FreeBSD: src/sys/boot/i386/boot2/Makefile,v 1.47 2003/06/26 03:51:57 peter Exp $
# $DragonFly: src/sys/boot/pc32/boot2/Makefile,v 1.11 2004/08/06 20:50:35 dillon Exp $

PROG=		boot2
NOMAN=
STRIP=
BINDIR?=	/boot
BINMODE=	444
CLEANFILES=	boot boot1 boot1.out boot1.o \
		boot2.ldr boot2.bin boot2.ld boot2.out boot2.o boot2.h \
		boot2.s sio.o

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

# Decide Level of UFS support.  UFS1_AND_UFS2 doesn't fit.

# BOOT2_UFS?=	UFS2_ONLY
#BOOT2_UFS?=	UFS1_AND_UFS2
BOOT2_UFS?=	UFS1_ONLY

# gcc3.x
#	-fno-guess-branch-probability
#	-mno-align-long-strings
CFLAGS=	-elf -ffreestanding -Os -fno-builtin -fno-stack-protector \
	-fomit-frame-pointer \
	-D${BOOT2_UFS} \
	-I${.CURDIR}/../../common \
	-I${.CURDIR}/../btx/lib -I. \
	-Wall -Waggregate-return -Wbad-function-cast -Wcast-align \
	-Wmissing-declarations -Wmissing-prototypes -Wnested-externs \
	-Wpointer-arith -Wshadow -Wstrict-prototypes -Wwrite-strings

# -mrtd will produce bad code (missing stack pops) when combined with
# gcc-3.4's (default on) unit-at-a-time optimization.  See the code
# generated for 'xputc' as an example.
#
.if ${CCVER} == "gcc2"
CFLAGS+= -mrtd
.endif

# Unfortunately, unit-at-a-time creates other issues as well, as yet
# not tracked down, in boot2, so we have to turn it off.  Note that
# the problem has been narrowed down to the 'boot2' code.  The /boot/loader
# doesn't seem to have a problem.
#
.if ${CCVER} != "gcc2"
CFLAGS+= -fno-unit-at-a-time
.endif

LDFLAGS=-nostdlib -static -N --gc-sections

all: boot1 boot2 boot

boot: boot1 boot2
	cat boot1 boot2 > boot

boot1: boot1.out
	objcopy -S -O binary boot1.out ${.TARGET}

boot1.out: boot1.o bootasmdef.nx
	${LD} ${LDFLAGS} -e start -Ttext ${ORG1} -o ${.TARGET} boot1.o

boot1.o: boot1.S
	${CC} ${CFLAGS} -DFLAGS=${B1FLAGS} ${AFLAGS} \
		${.IMPSRC} -o ${.TARGET} -c

boot2.o: boot2.c ${.CURDIR}/../../common/ufsread.c
	${CC} ${CFLAGS} -S -o boot2.s.tmp ${.IMPSRC}
	sed -e '/align/d' -e '/nop/d' < boot2.s.tmp > boot2.s
	rm -f boot2.s.tmp
	${AS} ${AFLAGS} -o boot2.o boot2.s

boot2.h: boot1.out bootasmdef.nx
	@echo "NOTE: boot2 using xread from boot1 at ${REL1}"
	${NM} -t d ${.ALLSRC} | awk '/([0-9])+ T xread/ \
	 	{ x = $$1 - ORG1; \
		printf("#define XREADORG %#x\n", REL1 + x) }' \
	 	ORG1=${ORG1D} REL1=${REL1D} > boot2.h

boot2: boot2.ldr boot2.bin ${BTX}/btx/btx
	btxld -v -E ${ORG2} -f bin -b ${BTX}/btx/btx -l boot2.ldr \
		-o boot2.ld -P 1 boot2.bin
	@ls -l boot2.ld | awk '{ x = 7680 - $$5; \
		print x " bytes available"; if (x < 0) exit 1 }'
	dd if=boot2.ld of=${.TARGET} obs=7680 conv=osync 2>/dev/null

boot2.ldr:
	dd if=/dev/zero of=${.TARGET} bs=512 count=1 2>/dev/null

boot2.bin: boot2.out
	objcopy -S -O binary boot2.out ${.TARGET}

boot2.out: boot2.o sio.o
	${LD} ${LDFLAGS} -Ttext ${ORG2} -o ${.TARGET} \
		${BTX}/lib/crt0.o ${.ALLSRC}

boot2.o: boot2.h

sio.o: sio.S
	${CC} -DSIOPRT=${BOOT_COMCONSOLE_PORT} -DSIOFMT=${B2SIOFMT} \
		-DSIOSPD=${BOOT_COMCONSOLE_SPEED} \
		${AFLAGS} ${.IMPSRC} -o ${.TARGET} -c

install:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		boot ${DESTDIR}${BINDIR}/boot
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		boot1 ${DESTDIR}${BINDIR}/boot1
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		boot2 ${DESTDIR}${BINDIR}/boot2

.include <bsd.prog.mk>

.if defined(REALLY_AMD64)
boot2.o: machine

beforedepend ${OBJS}: machine

machine:
	ln -sf ${.CURDIR}/../../../i386/include machine

CLEANFILES+=	machine
.endif

CLEANFILES+=	bootasmdef.nx

