#
# $FreeBSD: src/gnu/usr.bin/perl/pod/pod2man/Makefile,v 1.2.2.1 2000/10/05 09:54:51 markm Exp $
# $DragonFly: src/gnu/usr.bin/perl/pod/pod2man/Attic/Makefile,v 1.2 2003/06/17 04:25:47 dillon Exp $
#

PROG=	pod2man

${PROG}: ${PROG}.PL
	${MINIPERL} ${MINIPERLOPT} ${PROG}.PL

${PROG}.1: ${PROG}.PL
	${MINIPERL} ${MINIPERLOPT} ${.OBJDIR}/../pod2man/pod2man ${PROG}.PL \
		> ${PROG}.1

.include <bsd.prog.mk>

${PROG}.PL: ${PERL5SRC}/pod/${PROG}.PL
	cp -p ${.OODATE} ${.TARGET}
