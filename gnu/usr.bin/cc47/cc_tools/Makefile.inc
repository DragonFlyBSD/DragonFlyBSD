# Portions of our build system need this file early, make sure it isn't
# included twice (because e.g. bsd.init.mk would also include it)
#
.if !target(__<cctools.Makefile.inc>__)
__<cctools.Makefile.inc>__:

# Hint HOST_CCVER handling.
__USE_HOST_CCVER=

TOP_PREFIX=	../

GCC_NO_LIBS=	# yes

.include "../Makefile.inc"

.endif
