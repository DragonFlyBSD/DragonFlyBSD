# $FreeBSD: src/gnu/usr.bin/cvs/cvs/Makefile,v 1.32.2.6 2003/01/21 23:06:51 peter Exp $

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${CVSDIR}/src
.PATH:	${CVSDIR}/lib
.PATH:	${CVSDIR}/man
.PATH:	${CVSDIR}/doc
.PATH:	${CVSDIR}

PROG=	cvs
MAN=	cvs.1 cvs.5 cvsclient.7

SRCS=	add.c admin.c annotate.c buffer.c \
	checkin.c checkout.c classify.c client.c \
	commit.c create_adm.c cvsrc.c diff.c edit.c entries.c error.c \
	exithandle.c expand_path.c fileattr.c filesubr.c find_names.c \
	hardlink.c hash.c history.c ignore.c import.c lock.c log.c \
	log-buffer.c login.c logmsg.c ls.c main.c mkmodules.c \
	modules.c ms-buffer.c myndbm.c no_diff.c parseinfo.c patch.c \
	rcs.c rcscmds.c \
	recurse.c release.c remove.c repos.c root.c rsh-client.c run.c \
	scramble.c server.c stack.c status.c subr.c tag.c \
	update.c version.c vers_ts.c watch.c wrapper.c zlib.c

CFLAGS+= -I${.CURDIR} -I../lib -DHAVE_CONFIG_H \
	 -I${CVSDIR}/src -I${CVSDIR}/lib -I${CVSDIR}/diff -I.

DPADD+=	${LIBDIFF} ${LIBCVS} ${LIBCRYPT} ${LIBZ}
LDADD+=	${LIBDIFF} ${LIBCVS} -lcrypt -lz

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
