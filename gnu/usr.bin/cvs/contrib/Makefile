# $FreeBSD: src/gnu/usr.bin/cvs/contrib/Makefile,v 1.14.2.5 2002/12/19 21:18:01 peter Exp $
# $DragonFly: src/gnu/usr.bin/cvs/contrib/Makefile,v 1.4 2004/05/20 02:27:54 dillon Exp $

.include "../Makefile.inc"

.PATH:	${CVSDIR}/contrib
.PATH:	${CVSDIR}/man

SCRIPTS= clmerge cln_hist commit_prep cvs2vendor cvs_acls \
	 log log_accum mfpipe rcs-to-cvs rcs2log rcslock sccs2rcs \
	 easy-import

FILES=	 README cvshelp.man descend.man intro.doc

EXAMPDIR=	${SHAREDIR}/examples/cvs
FILESDIR=	${EXAMPDIR}/contrib
SCRIPTSDIR=	${FILESDIR}
PERLPATH=	/usr/bin/perl
CLEANFILES+=	$(SCRIPTS)

.SUFFIXES: .sh .pl .in

.sh:
	cp $< $@

.pl:
	sed -e 's,xPERL_PATHx,$(PERLPATH),' $< > $@

.in:
	sed -e 's,@CSH@,/bin/csh,' -e 's,@PERL@,$(PERLPATH),' $< > $@

.include <bsd.prog.mk>
