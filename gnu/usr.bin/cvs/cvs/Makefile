# $FreeBSD: src/gnu/usr.bin/cvs/cvs/Makefile,v 1.32.2.6 2003/01/21 23:06:51 peter Exp $
# $DragonFly: src/gnu/usr.bin/cvs/cvs/Makefile,v 1.6 2004/05/20 02:27:56 dillon Exp $

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${CVSDIR}/src
.PATH:	${CVSDIR}/lib
.PATH:	${CVSDIR}/man
.PATH:	${CVSDIR}/doc
.PATH:	${CVSDIR}

PROG=	cvs
MAN=	${.OBJDIR}/cvs.1 cvs.5

SRCS=	add.c admin.c annotate.c buffer.c \
	checkin.c checkout.c classify.c client.c \
	commit.c create_adm.c cvsrc.c edit.c entries.c error.c \
	exithandle.c expand_path.c fileattr.c filesubr.c find_names.c \
	hardlink.c hash.c history.c ignore.c import.c lock.c log.c \
	log-buffer.c login.c logmsg.c mkmodules.c \
	modules.c myndbm.c no_diff.c parseinfo.c patch.c prepend_args.c \
	rcs.c rcscmds.c \
	recurse.c release.c remove.c repos.c root.c rsh-client.c run.c \
	scramble.c server.c socket-client.c status.c subr.c tag.c update.c \
	vers_ts.c version.c watch.c wrapper.c zlib.c \
	${.OBJDIR}/main.c ${.OBJDIR}/diff.c

${.OBJDIR}/main.c: main.c.patch
	patch -o ${.TARGET} -i ${.ALLSRC} ${CONTRIBDIR}/src/main.c

${.OBJDIR}/diff.c: diff.c.patch
	patch -o ${.TARGET} -i ${.ALLSRC} ${CONTRIBDIR}/src/diff.c

${.OBJDIR}/cvs.1: cvs.1.patch
	patch -o ${.TARGET} -i ${.ALLSRC} ${CONTRIBDIR}/doc/cvs.1

CLEANFILES+= ${.OBJDIR}/main.c ${.OBJDIR}/diff.c ${.OBJDIR}/cvs.1

CFLAGS+= -I${.CURDIR} -I../lib -DHAVE_CONFIG_H \
	 -I${CVSDIR}/src -I${CVSDIR}/lib -I${CVSDIR}/diff -I.

DPADD+=	${LIBCVS} ${LIBDIFF} ${LIBGNUREGEX} ${LIBMD} ${LIBCRYPT} ${LIBZ}
LDADD+=	${LIBCVS} ${LIBDIFF} -lgnuregex -lmd -lcrypt -lz

#
# Regression test support
#
CLEANDIRS+=cvs-sanity
.ifmake regress
USERID!=id -u
regress:
	mkdir -p ${.OBJDIR}/cvs-sanity/tmp ${.OBJDIR}/cvs-sanity/work
.if ${USERID} == "0"
	chown -R nobody ${.OBJDIR}/cvs-sanity
	(TESTDIR=`sh -c 'cd ${.OBJDIR}/cvs-sanity/tmp && /bin/pwd'`;\
	        export TESTDIR;\
		cd ${.OBJDIR}/cvs-sanity/work;\
		su -m nobody -c "sh ${CVSDIR}/src/sanity.sh ${.OBJDIR}/cvs")
.else
	(TESTDIR=`sh -c 'cd ${.OBJDIR}/cvs-sanity/tmp && /bin/pwd'`;\
		export TESTDIR;\
		cd ${.OBJDIR}/cvs-sanity/work;\
		sh ${CVSDIR}/src/sanity.sh ${.OBJDIR}/cvs)
.endif
.endif

.include <bsd.prog.mk>
