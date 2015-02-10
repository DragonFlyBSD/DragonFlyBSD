CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
.include "../Makefile.inc"

LIB=		itm
SHLIB_MAJOR=	1

CFLAGS+=	-DHAVE_CONFIG_H -DGATHER_STATISTICS=0
CFLAGS+=	-I${GCCDIR}/libitm
CFLAGS+=	-I${GCCDIR}/libitm/config/x86
CFLAGS+=	-I${GCCDIR}/libitm/config/posix
CFLAGS+=	-I${GCCDIR}/libitm/config/generic
CXXFLAGS+=	-std=gnu++0x -funwind-tables -fno-exceptions
CXXFLAGS+=	-fno-rtti -fabi-version=4
VERSION_MAP=	${GCCDIR}/libitm/libitm.map

.PATH:		${GCCDIR}/libitm
.PATH:		${GCCDIR}/libitm/config/x86
.PATH:		${GCCDIR}/libitm/config/posix
.PATH:		${GCCDIR}/libitm/config/generic

# From libitm Makefile
am__append_1=
am__append_2=	x86_sse.cc x86_avx.cc
am__append_3=

libitm_la_SOURCES = aatree.cc alloc.cc alloc_c.cc alloc_cpp.cc \
	barrier.cc beginend.cc clone.cc eh_cpp.cc local.cc query.cc \
	retry.cc rwlock.cc useraction.cc util.cc sjlj.S tls.cc \
	method-serial.cc method-gl.cc method-ml.cc $(am__append_1) \
	$(am__append_2) $(am__append_3)

#CC_FILES=	${libitm_la_SOURCES:M*.cc}
#ASSY=		${libitm_la_SOURCES:M*.S}
#SO_FILES=	${CC_FILES:.cc=.So}
#ST_FILES=	${CC_FILES:.cc=.o}
#OBJS=           ${SO_FILES} ${ST_FILES} ${ASSY:.S=.So} ${ASSY:.S=.o}
SRCS=	${libitm_la_SOURCES}

libitm.spec:
	echo "*link_itm: -litm %{static: }" > libitm.spec

# hack to force c++ compiler to compile *.c files to create library
#.for ofile in ${SO_FILES}
#${ofile}: ${ofile:.So=.cc}
#	${CXX} ${PICFLAG} -DPIC ${SHARED_CXXFLAGS} ${CXXFLAGS} \
#		-c ${.ALLSRC} -o ${.TARGET}
#.endfor
#.for ofile in ${ST_FILES}
#${ofile}: ${ofile:.o=.cc}
#	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.ALLSRC} -o ${.TARGET}
#.endfor

CLEANFILES+= 	libitm.spec

beforedepend:	libitm.spec

.include <bsd.lib.mk>
