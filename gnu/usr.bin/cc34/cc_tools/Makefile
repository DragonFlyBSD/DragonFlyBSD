# $DragonFly: src/gnu/usr.bin/cc34/cc_tools/Makefile,v 1.3 2004/06/20 20:59:21 joerg Exp $

SUBDIR=	 gengenrtl genmodes gengtype genpreds gencheck genconstants
SUBDIR+= genconditions genflags gencodes genattr genattrtab genemit
SUBDIR+= genconfig genextract genopinit genoutput genpeep genrecog
SUBDIR+= gcov-iov

SUBDIR+= fini

depend: cc_prep
cc_prep: .PHONY
	cd ${STOPDIR}/cc_prep; make depend

.ORDER: cc_prep ${SUBDIR}

.include <bsd.subdir.mk>
