# $DragonFly: src/gnu/usr.bin/cc34/cc_tools/Makefile,v 1.2 2004/06/20 00:19:51 joerg Exp $

SUBDIR=	 gengenrtl genmodes gengtype genpreds gencheck genconstants
SUBDIR+= genconditions genflags gencodes genattr genattrtab genemit
SUBDIR+= genconfig genextract genopinit genoutput genpeep genrecog
SUBDIR+= gcov-iov

SUBDIR+= fini

depend: cc_prep
cc_prep: .PHONY
	cd ${STOPDIR}/cc_prep; make depend

.include <bsd.subdir.mk>
