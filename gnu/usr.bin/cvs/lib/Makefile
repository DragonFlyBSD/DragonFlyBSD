# $FreeBSD: src/gnu/usr.bin/cvs/lib/Makefile,v 1.19.2.4 2003/01/21 23:06:52 peter Exp $
# $DragonFly: src/gnu/usr.bin/cvs/lib/Makefile,v 1.12 2005/07/23 20:23:06 joerg Exp $

.include "${.CURDIR}/../Makefile.inc"

.PATH: ${CVSDIR}/src
.PATH: ${CVSDIR}/lib
.PATH: ${CVSDIR}/man

LIB=		cvs
INTERNALLIB=	YES

CFLAGS+= -I. -I${CVSDIR}/src -I${CVSDIR}/lib
CFLAGS+= -DHAVE_CONFIG_H -DIN_LIBINTL
YFLAGS=
CLEANFILES+=	config.h alloca.h getopt.h
CONTRIBDIR=     ${CVSDIR}/lib

VERSION!=       sed < ${CVSDIR}/configure \
		-e '/^[ 	]*VERSION=/!d' -e 's/.*=\(.*\)/\1/' -e q

CVS_UMASK_DFLT?=	002
CVS_ADMIN_GROUP?=	cvsadmin
CVS_TMPDIR_DFLT?=	/tmp

SRCS=	config.h \
	sighandle.c allocsa.c alloca.h basename.c dirname.c stripslash.c \
	exitfail.c getdate.c getopt.h getopt.c getopt1.c gettime.c \
	getnline.c md5.c save-cwd.c strftime.c xmalloc.c xalloc-die.c \
	xgetcwd.c xgethostname.c xreadlink.c yesno.c \
	pagealign_alloc.c getline.c getndelim2.c mktime.c \
	getpass.c rpmatch.c vasnprintf.c printf-args.c printf-parse.c \
	asnprintf.c getcwd.c closeout.c chdir-long.c quotearg.c \
	memrchr.c openat.c

config.h: config.h.proto
	sed -e "s,@VERSION@,${VERSION}-DragonFly,g" \
	    -e "s,@UMASK_DFLT@,${CVS_UMASK_DFLT},g" \
	    -e "s,@TMPDIR_DFLT@,${CVS_TMPDIR_DFLT},g" \
	    -e "s,@CVS_ADMIN_GROUP@,${CVS_ADMIN_GROUP},g" \
	    ${.ALLSRC} > ${.TARGET}

alloca.h: alloca_.h
	cp ${.ALLSRC} ${.TARGET}

getopt.h: getopt_.h
	cp ${.ALLSRC} ${.TARGET}

.include <bsd.lib.mk>
