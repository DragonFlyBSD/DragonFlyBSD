# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/Makefile,v 1.4 2008/05/14 15:02:48 hasso Exp $

SUBDIR=  libiberty
SUBDIR+= gengenrtl genmodes gengtype genpreds gencheck genconstants
SUBDIR+= genconditions genflags gencodes genattr genattrtab genemit
SUBDIR+= genconfig genextract genopinit genoutput genpeep genrecog
SUBDIR+= gcov-iov genchecksum

.include "Makefile.tools"
clean cleandir:
	rm -rf ${TOOLGENDIR}

.include <bsd.subdir.mk>
