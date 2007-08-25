# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/Makefile,v 1.2 2007/08/25 15:29:28 corecode Exp $

SUBDIR=  libiberty
SUBDIR+= gengenrtl genmodes gengtype genpreds gencheck genconstants
SUBDIR+= genconditions genflags gencodes genattr genattrtab genemit
SUBDIR+= genconfig genextract genopinit genoutput genpeep genrecog
SUBDIR+= gcov-iov genchecksum

#.if !defined(NO_FORTRAN)
#SUBDIR+= fini
#.endif

.ORDER: mktooldir ${SUBDIR}

.include "Makefile.tools"
obj depend: mktooldir
mktooldir:
	mkdir -p ${TOOLGENDIR}

clean cleandir:
	rm -rf ${TOOLGENDIR}

.include <bsd.subdir.mk>
