# $DragonFly: src/gnu/usr.bin/cvs/Makefile.inc,v 1.4 2005/01/25 14:00:18 joerg Exp $
#

.if !defined(CVSDIR)

CVSDIR=		$(.CURDIR)/../../../../contrib/cvs-1.12.9

.if exists(${.OBJDIR}/../lib)
LIBCVSDIR=	${.OBJDIR}/../lib
.else
LIBCVSDIR=	${.CURDIR}/../lib
.endif

LIBCVS=		${LIBCVSDIR}/libcvs.a

.if exists(${.OBJDIR}/../libdiff)
LIBDIFFDIR=	${.OBJDIR}/../libdiff
.else
LIBDIFFDIR=	${.CURDIR}/../libdiff
.endif

LIBDIFF=	${LIBDIFFDIR}/libdiff.a

.if exists(${.CURDIR}/../../Makefile.inc)
.include "${.CURDIR}/../../Makefile.inc"
.endif

.endif
