# $FreeBSD: src/sys/boot/i386/boot2/Makefile,v 1.47 2003/06/26 03:51:57 peter Exp $

BINDIR?=	/boot
BINMODE=	444
CLEANFILES=	boot \
		sio.o

CSTD?=		c99

NM?=		nm
NXCFLAGS=
NXLDFLAGS=

.PATH: ${.CURDIR}/..
.PATH: ${.CURDIR}/../../common
.PATH: ${.CURDIR}/../../../../lib/libstand
.PATH: ${.CURDIR}/../../../libkern

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

CFLAGS=	-Os \
	-fomit-frame-pointer \
	-fno-unwind-tables \
	-fno-asynchronous-unwind-tables \
	-DBOOT2 \
	-I${.CURDIR}/../../common \
	-I${.CURDIR}/../../../../lib/libstand \
	-I${.CURDIR}/../btx/lib -I. \
	-Wall -Waggregate-return -Wbad-function-cast -Wcast-align \
	-Wmissing-declarations -Wmissing-prototypes -Wnested-externs \
	-Wpointer-arith -Wshadow -Wstrict-prototypes -Wwrite-strings

DEBUG_FLAGS=

# Tell gcc that it shouldn't do fancy optimizations for newer processors.
# Otherwise it winds up creating larger code and we won't be able to fit boot2.
CFLAGS+= -march=i386

# Unfortunately, unit-at-a-time creates issues as yet
# not tracked down, in boot2, so we have to turn it off.  Note that
# the problem has been narrowed down to the 'boot2' code.  The /boot/loader
# doesn't seem to have a problem.  Added to BOOT2_GCC_FLAGS since clang
# doesn't recognize this flag.  The no-guess-branch-probability flag is also
# gcc-specific so it was moved to BOOT2_GCC_FLAGS as well.
#
CFLAGS+= ${BOOT2_GCC_FLAGS}

# boot2 area in 32 bit disklabel is 16 sectors (8K)
#
# boot2 area in 64 bit disklabel is 64 sectors (32K).
# However, the boot1 btx loader puts boot2 at a physical address
# of MEM_BTX_USR+BOOT2_VORIGIN which is typically 0xC000, so we
# cannot make boot2 any larger then 16KB or we overflow the segment.
#
NSECT_32?=	16
NSECT_64?=	30
LDFLAGS=	-nostdlib -static -N --gc-sections
LINKSCRIPT=	--script ${.CURDIR}/boot2.ldscript

all: boot

boot: boot1_32 boot2_32
	cat ${.ALLSRC} > boot

.for s in _32 _64

_ADDCFLAGS$s:= -DNSECT=${NSECT$s}
_ts=	$s
.if ${_ts} == "_64"
_ADDCFLAGS$s+= -DDISKLABEL64
_ADDCFLAGS$s+= -DHAMMER2FS -DUFS
_ADDOBJS$s+= hammer2$s.o ufsread$s.o icrc32$s.o
.else
_ADDCFLAGS$s+= -DUFS -DUFS1_ONLY
_ADDOBJS$s+= ufsread$s.o
.endif

all: boot1$s boot2$s

boot1$s: boot1$s.out
	objcopy -S -O binary boot1$s.out ${.TARGET}

boot1$s.out: boot1$s.o bootasmdef.nx
	${LD} ${LDFLAGS} -e start -Ttext ${ORG1} -o ${.TARGET} ${.ALLSRC:M*.o}

boot1$s.o: boot1.S
	${CC} ${CFLAGS} ${_ADDCFLAGS$s} -DFLAGS=${B1FLAGS} \
		${ASSEMBLER_FLAGS} ${.ALLSRC} -o ${.TARGET} -c

#${.CURDIR}/../../common/ufsread.c ${.CURDIR}/../../../../lib/libstand/hammer2.c

boot2$s.s: boot2.c boot2$s.h ufsread.c hammer2.c icrc32.c
	${CC} ${CFLAGS} ${_ADDCFLAGS$s} -S -o boot2$s.s.tmp ${.CURDIR}/boot2.c
	sed -e '/align/d' -e '/nop/d' < ${.TARGET}.tmp > ${.TARGET}
	rm -f ${.TARGET}.tmp

boot2$s.o: boot2$s.s
	${AS} ${AFLAGS} -o ${.TARGET} boot2$s.s

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

boot2$s.out: boot2$s.o sio.o ${_ADDOBJS$s}
	${LD} ${LDFLAGS} -Ttext ${ORG2} ${LINKSCRIPT} -o ${.TARGET} \
		${BTX}/lib/crt0.o ${.ALLSRC}

CLEANFILES+=	boot1$s boot1$s.out boot1$s.o \
		boot2$s boot2$s.ldr boot2$s.bin \
		boot2$s.ld boot2$s.out boot2$s.o boot2$s.h boot2$s.s \
		${_ADDOBJS$s}

hammer2$s.o: hammer2.c
	${CC} ${CFLAGS} ${_ADDCFLAGS$s} ${.ALLSRC} -o ${.TARGET} -c

ufsread$s.o: ufsread.c
	${CC} ${CFLAGS} ${_ADDCFLAGS$s} ${.ALLSRC} -o ${.TARGET} -c

icrc32$s.o: icrc32.c
	${CC} ${CFLAGS} ${_ADDCFLAGS$s} ${.ALLSRC} -o ${.TARGET} -c

.endfor

sio.o: sio.S
	${CC} ${CFLAGS} -DSIOPRT=${BOOT_COMCONSOLE_PORT} -DSIOFMT=${B2SIOFMT} \
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

.if defined(REALLY_X86_64)
# For x86_64, we need to fake up a complete machine/ tree
# so that types for -m32 appear correctly.

boot2.o: machine

beforedepend ${OBJS}: machine cpu

machine::
	mkdir -p machine
	${LN} -sf ${.CURDIR}/../../../cpu/i386/include/* machine
	${LN} -sf ${.CURDIR}/../../../platform/pc32/include/* machine

cpu::
	${LN} -hsf ${.CURDIR}/../../../cpu/i386/include cpu

CLEANDIRS+=	machine cpu
.endif

CLEANFILES+=	bootasmdef.nx

