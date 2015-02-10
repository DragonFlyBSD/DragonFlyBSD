# Portions of our build system need this file early, make sure it isn't
# included twice (because e.g. bsd.init.mk would also include it)

.if !target(__<cctools50.Makefile.inc>__)
__<cctools50.Makefile.inc>__:

TOP_PREFIX=	../
GCC_NO_LIBS=	# yes

.include "../Makefile.inc"
.endif
