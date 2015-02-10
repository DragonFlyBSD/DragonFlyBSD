RELUP=	/..
.include "../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.src"

LIB=		stdc++
SHLIB_MAJOR=	9

.PATH:	${SRCDIR}/src/c++98
.PATH:	${SRCDIR}/src/c++11

CFLAGS+=	-I${.OBJDIR}
CXXFLAGS+=	-fno-implicit-templates
CXXFLAGS+=	-fdiagnostics-show-location=once
CXXFLAGS+=	-ffunction-sections -fdata-sections
LDFLAGS+=	-Wl,-z -Wl,relro -Wl,--gc-sections
VERSION_MAP=	${.CURDIR}/libstdc++-symbols.ver

FLAGS_GROUPS=		gnu11
gnu11_FLAGS=		-std=gnu++11
gnu11_FLAGS_FILES=	${cxx11_sources:S|++|xx|}

CONVARCS=	../components/libconv_supc/libsupcxxconvenience.a
CONVARCS+=	../components/libconv_1998/libcxx98convenience.a
CONVARCS+=	../components/libconv_2011/libcxx11convenience.a
LDADD=		-Wl,--whole-archive ${CONVARCS:.a=_pic.a} -lm
LDADD+=		-Wl,--no-whole-archive
ARADD=		${CONVARCS} ${LIBM}

# The AR script is a nice trick, but it *cannot* handle "++" on any line
# To work around this incredible limitation, copy files with "++" in
# their names to a new file name (transform ++ => xx)

SRCS=		${libstdc___la_SOURCES:S|++|xx|}
PROBLEMS=	${libstdc___la_SOURCES:M*++*}

libstdc++.a: ${SRCS:.cc=.o} script.ar
	@${ECHO} building static ${LIB} library via script
	rm -f ${.TARGET}
	${AR} -M < script.ar
	mv libstdcxx.a ${.TARGET}

${PROBLEMS:S|++|xx|}: ${PROBLEMS}
	cp ${SRCDIR}/src/c++11/${.TARGET:S|xx|++|} ${.TARGET}

gstdint.h:
	@echo "#include <stdint.h>" > ${.TARGET}

script.ar:
	@echo "CREATE libstdcxx.a" 	>  ${.TARGET}
.for arc in ${CONVARCS}
	@echo "ADDLIB ${arc}"		>> ${.TARGET}
.endfor
.for obj in ${SRCS:.cc=.o}
	@echo "ADDMOD ${obj}"		>> ${.TARGET}
.endfor
	@echo "SAVE"			>> ${.TARGET}
	@echo "END"			>> ${.TARGET}

beforedepend:	gstdint.h script.ar ${PROBLEMS:S|++|xx|}
CLEANFILES=	gstdint.h script.ar ${PROBLEMS:S|++|xx|}

.include <bsd.lib.mk>
