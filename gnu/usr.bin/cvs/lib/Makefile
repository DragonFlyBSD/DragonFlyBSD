# $FreeBSD: src/gnu/usr.bin/cvs/lib/Makefile,v 1.19.2.4 2003/01/21 23:06:52 peter Exp $

.include "${.CURDIR}/../Makefile.inc"

.PATH: ${CVSDIR}/src
.PATH: ${CVSDIR}/lib

LIB=		cvs
INTERNALLIB=	YES

CFLAGS+= 	-I. -I${CVSDIR}/src -I${CVSDIR}/lib -I${.CURDIR}
CFLAGS+= 	-DHAVE_CONFIG_H
CLEANFILES+=	config.h getopt.h glob.h

CVS_UMASK_DFLT?=	002
CVS_ADMIN_GROUP?=	cvsadmin
CVS_TMPDIR_DFLT?=	/tmp
CVS_OVERRIDE_CONF?=	/etc/cvs.conf
CVS_OVERRIDE_DIR?=	/etc/cvs/

SRCS=	config.h \
	getopt.h \
	glob.h \
	sighandle.c \
	allocsa.c \
	cycle-check.c \
	basename.c \
	stripslash.c \
	getnline.c  \
	getdelim.c \
	strnlen1.c \
	xalloc-die.c \
	xgethostname.c \
	xreadlink.c \
	save-cwd.c  \
	mktime.c \
	strftime.c  \
	canon-host.c \
	canonicalize.c  \
	closeout.c \
	dirname.c \
	exitfail.c \
	getdate.c \
	getndelim2.c \
	getopt.c \
	getopt1.c \
	getpass.c \
	gettime.c \
	glob.c \
	mbchar.c \
	md5.c \
	pagealign_alloc.c \
	quotearg.c \
	regex.c \
	rpmatch.c \
	strcasecmp.c \
	dup-safer.c \
	fd-safer.c \
	vasnprintf.c \
	printf-args.c \
	printf-parse.c \
	asnprintf.c \
	xmalloc.c \
	xgetcwd.c \
	yesno.c

# use mempcpy() from libc
#SRCS+=	mempcpy.c

config.h: config.h.proto
	sed -e "s,@VERSION@,${CVSVERSION}-DragonFly,g" \
	    -e "s,@UMASK_DFLT@,${CVS_UMASK_DFLT},g" \
	    -e "s,@TMPDIR_DFLT@,${CVS_TMPDIR_DFLT},g" \
	    -e "s,@CVS_ADMIN_GROUP@,${CVS_ADMIN_GROUP},g" \
	    -e "s,@CVS_OVERRIDE_DIR@,${CVS_OVERRIDE_DIR},g" \
	    -e "s,@CVS_OVERRIDE_CONF@,${CVS_OVERRIDE_CONF},g" \
	    ${.ALLSRC} > ${.TARGET}

getopt.h: getopt_.h
	cp ${.ALLSRC} ${.TARGET}

glob.h: glob_.h
	cp ${.ALLSRC} ${.TARGET}

.include <bsd.lib.mk>
