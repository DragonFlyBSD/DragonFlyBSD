# $DragonFly: src/gnu/usr.bin/cvs/Makefile.inc,v 1.5 2005/03/13 11:43:07 corecode Exp $
#

.if !defined(CVSDIR)

CVSDIR=		$(.CURDIR)/../../../../contrib/cvs-1.12.11

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
