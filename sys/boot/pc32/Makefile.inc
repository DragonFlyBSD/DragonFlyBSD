# Common defines for all of /sys/boot/i386/

.if !target(__pc32__)
__pc32__:

LOADER_ADDRESS?= 0x200000
CFLAGS+=	-ffreestanding -mpreferred-stack-boundary=2
CFLAGS+=	-mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3 -msoft-float

# override CPU type, so that gcc doesn't insert mmx/sse instructions
FORCE_CPUTYPE=	i386

WARNS?=	2

.if defined(REALLY_X86_64)
.include "../pc64/Makefile.inc"
.endif
.endif
