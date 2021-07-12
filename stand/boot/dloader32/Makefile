.MAKEFLAGS:  MACHINE_ARCH=i386 MACHINE_PLATFORM=pc32 REALLY_X86_64=true

.PATH:	${.CURDIR}/../dloader

.if exists(../${MACHINE_PLATFORM}/Makefile.inc)
.include "../${MACHINE_PLATFORM}/Makefile.inc"
.endif

LIB=	dloader32

.include "../dloader/Makefile"

CFLAGS+=	-I${.CURDIR}/../dloader
