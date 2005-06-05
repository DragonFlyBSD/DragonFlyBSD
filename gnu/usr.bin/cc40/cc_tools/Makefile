# $DragonFly: src/gnu/usr.bin/cc40/cc_tools/Attic/Makefile,v 1.1 2005/06/05 22:43:20 corecode Exp $

SUBDIR=  libiberty
SUBDIR+= gengenrtl genmodes gengtype genpreds gencheck genconstants
SUBDIR+= genconditions genflags gencodes genattr genattrtab genemit
SUBDIR+= genconfig genextract genopinit genoutput genpeep genrecog
SUBDIR+= gcov-iov

#.if !defined(NO_FORTRAN)
#SUBDIR+= fini
#.endif

.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
