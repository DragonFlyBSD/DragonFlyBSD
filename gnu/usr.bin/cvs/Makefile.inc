# $DragonFly: src/gnu/usr.bin/cvs/Makefile.inc,v 1.7 2005/12/07 01:16:30 corecode Exp $
#

.if !defined(CVSDIR)

CVSDIR=		$(.CURDIR)/../../../../contrib/cvs-1.12
CVSVERSION=	1.12.13

CFLAGS+= -I${.OBJDIR}/../lib -I${CVSDIR}/lib

WARNS?=	 1

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
