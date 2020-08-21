#	$NetBSD: Makefile,v 1.56 2012/05/30 21:54:23 sjg Exp $
#	@(#)Makefile	5.2 (Berkeley) 12/28/90
#	$Id: Makefile.in,v 1.174 2012/10/10 18:46:24 sjg Exp $

PROG=	make
SRCS=	arch.c buf.c compat.c cond.c dir.c for.c hash.c job.c main.c \
	make.c parse.c str.c suff.c targ.c trace.c var.c util.c 
SRCS+=  strlist.c
SRCS+=  make_malloc.c
SRCS+=	lstAppend.c lstAtEnd.c lstAtFront.c lstClose.c lstConcat.c \
	lstDatum.c lstDeQueue.c lstDestroy.c lstDupl.c lstEnQueue.c \
	lstFind.c lstFindFrom.c lstFirst.c lstForEach.c lstForEachFrom.c \
	lstInit.c lstInsert.c lstIsAtEnd.c lstIsEmpty.c lstLast.c \
	lstMember.c lstNext.c lstOpen.c lstRemove.c lstReplace.c lstSucc.c
SRCS += lstPrev.c

SRCS+=	stresep.c
SRCS+=	meta.c metachar.c

# you can use this Makefile if you have an earlier version of bmake.
srcdir= ${.CURDIR}/../../contrib/bmake

# Base version on src date
MAKE_VERSION= 20200710
.if defined(BOOTSTRAPPING)
DEFAULT_SYS_PATH = ${.CURDIR}/../../share/mk
.else
DEFAULT_SYS_PATH = /usr/share/mk
.endif

# move vital dports information to bmake for performance reasons
DFLYVERSION!=	awk '/^\#define[[:blank:]]__DragonFly_version/ {print $$3}' \
		< ${.CURDIR}/../../sys/sys/param.h
OSREL!=		echo ${DFLYVERSION} | \
		awk '{a=int($$1/100000); b=int(($$1-(a*100000))/100); \
			print a "." b}'

CPPFLAGS+=	-DUSE_EMALLOC
CPPFLAGS+=	-DUSE_META
CPPFLAGS+=	-DMAKE_NATIVE
CFLAGS+=	${CPPFLAGS}
CFLAGS+=	-D_PATH_DEFSYSPATH=\"${DEFAULT_SYS_PATH}\"
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${srcdir}
CFLAGS+=	-DHAVE_CONFIG_H
CFLAGS+=	-DCCVER=\"${CCVER}\"
CFLAGS+=	-DDFVER=\"${DFLYVERSION}\" -DOSREL=\"${OSREL}\"
CFLAGS+=	-DNO_PWD_OVERRIDE
CFLAGS+=	${XDEFS}
CFLAGS+=	${CFLAGS_${.TARGET:T}} 
CFLAGS+=	${COPTS.${.ALLSRC:M*.c:T}}
COPTS.main.c=	"-DMAKE_VERSION=\"${MAKE_VERSION}\""
COPTS.var.c=	-Wno-cast-qual
COPTS.job.c=	-Wno-format-nonliteral
COPTS.parse.c=	-Wno-format-nonliteral
COPTS.var.c=	-Wno-format-nonliteral

# XXX pending bmake update
.if !defined(WARNS_AUDIT)
WARNS=	1
.endif

.PATH:	${srcdir}
.PATH:	${srcdir}/lst.lib

MAN=	make.1
make.1:	bmake.1
	@sed -e '/^.Nm/s/bmake/${PROG}/' -e 's/Xr make 1/Nm bmake/' \
	    -e 's/ $$//' -e 's/^make$$/.Nm/' -e 's/ NetBSD$$/\'$$'\n.Nx/' \
	    -e '/^NetBSD .\..$$/s/NetBSD/.Nx/' \
	    < ${.ALLSRC} > ${.TARGET}

CLEANFILES+=	make.1

.include <bsd.prog.mk>
