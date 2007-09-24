# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/Makefile,v 1.3 2007/09/24 03:04:28 y0netan1 Exp $

SUBDIR=  libiberty
SUBDIR+= gengenrtl genmodes gengtype genpreds gencheck genconstants
SUBDIR+= genconditions genflags gencodes genattr genattrtab genemit
SUBDIR+= genconfig genextract genopinit genoutput genpeep genrecog
SUBDIR+= gcov-iov genchecksum

#.if !defined(NO_FORTRAN)
#SUBDIR+= fini
#.endif

.include "Makefile.tools"
clean cleandir:
	rm -rf ${TOOLGENDIR}

.include <bsd.subdir.mk>
