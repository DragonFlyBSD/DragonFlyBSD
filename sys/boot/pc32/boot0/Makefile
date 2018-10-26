# $FreeBSD: src/sys/boot/i386/boot0/Makefile,v 1.17 2002/09/17 01:48:54 peter Exp $
.if ${MACHINE_ARCH} == "x86_64"
.MAKEFLAGS:  MACHINE_ARCH=i386 MACHINE_PLATFORM=pc32 REALLY_X86_64=true
.else
.MAKEFLAGS:  REALLY_X86_64=false
.endif

PROG?=		boot0
NOMAN=
STRIP=
BINDIR?=	/boot
BINMODE=	444

NXCFLAGS=
NXLDFLAGS=

.PATH: ${.CURDIR}/..

# The default set of flags compiled into boot0.  This enables update (writing
# the modified boot0 back to disk after running so that the selection made is
# saved), packet mode (detect and use the BIOS EDD extensions if we try to
# boot past the 1024 cylinder limit), and booting from all valid slices.
BOOT_BOOT0_FLAGS?=	0x8f

# The number of timer ticks to wait for a keypress before assuming the default
# selection.  Since there are 18.2 ticks per second, the default value of
# 0xb6 (182d) corresponds to 10 seconds.
BOOT_BOOT0_TICKS?=	0xb6

# Comm settings for boot0sio.
# Bit(s) Description
# 7-5    data rate (110,150,300,600,1200,2400,4800,9600 bps)
# 4-3    parity (00 or 10 = none, 01 = odd, 11 = even)
# 2      stop bits (set = 2, clear = 1)
# 1-0    data bits (00 = 5, 01 = 6, 10 = 7, 11 = 8)
.if !defined(COMSPEED)
BOOT_COMCONSOLE_SPEED?= 115200
.endif

.if ${BOOT_COMCONSOLE_SPEED} == 115200
COMSPEED=			"1 << 4 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 57600
COMSPEED=			"2 << 4 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 38400
COMSPEED=			"3 << 4 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 19200
COMSPEED=			"6 << 4 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 9600
COMSPEED=			"7 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 4800
COMSPEED=			"6 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 2400
COMSPEED=			"5 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 1200
COMSPEED=			"4 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 600
COMSPEED=			"3 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 300
COMSPEED=			"2 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 150
COMSPEED=			"1 << 5 + 3"
.elif ${BOOT_COMCONSOLE_SPEED} == 110
COMSPEED=			"0 << 5 + 3"
.else
COMSPEED=			"1 << 4 + 3"
.endif

CFLAGS+=-DFLAGS=${BOOT_BOOT0_FLAGS} \
	-DTICKS=${BOOT_BOOT0_TICKS} \
	-DCOMSPEED=${COMSPEED}

# pull BOOT0_ORIGIN out of bootasm.h
#
REL0 = `${.OBJDIR}/../asmdef/bootasmdef.nx BOOT0_ORIGIN`

${PROG}: boot0.o bootasm.h
	@echo "NOTE: boot0 origin set to ${REL0}"
	${LD} ${LDFLAGS:N-Wl,*} -N -e start -Ttext ${REL0} -o boot0.out boot0.o
	objcopy -S -O binary boot0.out ${.TARGET}

boot0.o: boot0.S
	${CC} -c ${CFLAGS} ${ASSEMBLER_FLAGS} ${.IMPSRC} -o ${.TARGET}

CLEANFILES+= boot0.out boot0.o

.include <bsd.prog.mk>
