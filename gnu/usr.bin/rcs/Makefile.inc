# Location of librcs

.if exists(${.OBJDIR}/../lib)
LIBRCS=	${.OBJDIR}/../lib/librcs.a
.else
LIBRCS=	${.CURDIR}/../lib/librcs.a
.endif

# by default, don't emit commitid phrase into RCS files
.if RCS_EMIT_COMMITID
CFLAGS+=	-DRCS_EMIT_COMMITID
.endif

WARNS?=	2

.include "../Makefile.inc"
