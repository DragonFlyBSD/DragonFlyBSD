#	$NetBSD: Makefile,v 1.56 2012/05/30 21:54:23 sjg Exp $
#	@(#)Makefile	5.2 (Berkeley) 12/28/90
#	$Id: Makefile.in,v 1.174 2012/10/10 18:46:24 sjg Exp $
#
# Derived from the vendor's Makefile and make-bootstrap.sh.
#

srcdir=	${.CURDIR}/../../contrib/bmake
.PATH:	${srcdir}

PROG=	make
MAN=	make.1
SRCS=	\
	arch.c		\
	buf.c		\
	compat.c	\
	cond.c		\
	dir.c		\
	for.c		\
	hash.c		\
	job.c		\
	lst.c		\
	main.c		\
	make.c		\
	make_malloc.c	\
	meta.c		\
	metachar.c	\
	parse.c		\
	str.c		\
	suff.c		\
	targ.c		\
	trace.c		\
	util.c		\
	var.c
SRCS+=	stresep.c

# Include ${_MAKE_VERSION}
.include "${srcdir}/VERSION"

.if defined(BOOTSTRAPPING)
DEFAULT_SYS_PATH=	${.CURDIR}/../../share/mk
.else
DEFAULT_SYS_PATH=	/usr/share/mk
.endif

# move vital dports information to bmake for performance reasons
DFLYVERSION!=	awk '/^\#define[[:blank:]]__DragonFly_version/ {print $$3}' \
		< ${.CURDIR}/../../sys/sys/param.h
OSREL!=		echo ${DFLYVERSION} | \
		awk '{a=int($$1/100000); b=int(($$1-(a*100000))/100); \
			print a "." b}'

CFLAGS+=	-I${.CURDIR} -I${srcdir}
CFLAGS+=	-DHAVE_CONFIG_H \
		-DNO_PWD_OVERRIDE \
		-DUSE_META \
		-DMAKE_NATIVE
CFLAGS+=	${COPTS.${.ALLSRC:M*.c:T}}

COPTS.main.c=	-DMAKE_VERSION=\"${_MAKE_VERSION}\" \
		-D_PATH_DEFSYSPATH=\"${DEFAULT_SYS_PATH}\" \
		-DDFVER=\"${DFLYVERSION}\" \
		-DOSREL=\"${OSREL}\"

.include <bsd.prog.mk>
