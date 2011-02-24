# Common defines for all of /sys/boot/i386/

.if !target(__pc32__)
__pc32__:

LOADER_ADDRESS?=	0x200000
CFLAGS+=		-ffreestanding -mpreferred-stack-boundary=2

# override CPU type, so that gcc doesn't insert mmx/sse instructions
FORCE_CPUTYPE=		i386

WARNS?=	1

.if defined(REALLY_X86_64)
.include "../pc64/Makefile.inc"
.endif
.endif
