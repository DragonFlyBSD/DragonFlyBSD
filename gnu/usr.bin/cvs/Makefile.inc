# $DragonFly: src/gnu/usr.bin/cvs/Makefile.inc,v 1.3 2004/08/03 18:11:54 dillon Exp $
#

.if !defined(CVSDIR)

CVSDIR=		$(.CURDIR)/../../../../contrib/cvs-1.12.9
CONTRIBDIR=	${CVSDIR}

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
