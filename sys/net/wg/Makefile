KMOD=	if_wg

SRCS=	if_wg.c wg_cookie.c wg_noise.c
SRCS+=	opt_inet6.h

# Help quickly build and test this module alone.
.if !defined(KERNBUILDDIR)
KCFLAGS+=-g -DINVARIANTS

opt_inet6.h:
	echo "#define INET6 1" > ${.TARGET}
.endif

# Uncomment the following lines to enable selftests.
#KCFLAGS+=-DWG_SELFTESTS
#KCFLAGS+=-DWG_ALLOWEDIPS_RANDOMIZED_TEST # NOTE: can take >1 hour !

# Header files are installed by 'include/Makefile'.
# XXX: should install only 'if_wg.h' but no other header files.

.include <bsd.kmod.mk>
