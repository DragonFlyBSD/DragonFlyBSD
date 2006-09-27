# $DragonFly: src/gnu/usr.bin/cc41/cc_tools/Makefile,v 1.1 2006/09/27 12:10:34 corecode Exp $

SUBDIR=  libiberty
SUBDIR+= gengenrtl genmodes gengtype genpreds gencheck genconstants
SUBDIR+= genconditions genflags gencodes genattr genattrtab genemit
SUBDIR+= genconfig genextract genopinit genoutput genpeep genrecog
SUBDIR+= gcov-iov genchecksum

#.if !defined(NO_FORTRAN)
#SUBDIR+= fini
#.endif

.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
