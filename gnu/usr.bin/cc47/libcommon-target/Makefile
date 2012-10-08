.include "../Makefile.inc"

LIB=		common-target
INTERNALLIB=	YES

# Almost verbatim from Makefile
common_out_object_file=${GCC_CPU}-common.o
OBJS-libcommon-target = \
	$(common_out_object_file) \
	prefix.o \
	params.o \
	opts.o \
	opts-common.o \
	options.o \
	vec.o \
	hooks.o \
	common-targhooks.o

SRCS=	${OBJS-libcommon-target:.o=.c}

CFLAGS+= -DPREFIX=\"${TOOLS_PREFIX}/usr\"

.PATH: $(srcdir)/common
.PATH: $(srcdir)/common/config/${GCC_CPU}
.include <bsd.lib.mk>
