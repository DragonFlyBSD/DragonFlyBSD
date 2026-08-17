RELUP=	/..
.include "../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
PREDIR=	${SRCDIR}/config/abi/pre
.include "Makefile.src"

LIB=		stdc++
SHLIB_MAJOR=	9

.PATH:	${SRCDIR}/src/c++98
.PATH:	${SRCDIR}/src/c++11

CXXFLAGS+=	-std=gnu++98 -fno-implicit-templates
VERSION_MAP=	${.OBJDIR}/libstdc++.map

FLAGS_GROUPS=		gnu11
gnu11_FLAGS=		-std=gnu++11 -I${.OBJDIR}
gnu11_FLAGS_FILES=	${cxx11_sources}

CONVARCS=	../components/libconv_supc/libsupc++convenience.a \
		../components/libconv_1998/libc++98convenience.a \
		../components/libconv_2011/libc++11convenience.a \
		../components/libconv_2017/libc++17convenience.a \
		../components/libconv_2020/libc++20convenience.a
LDADD=		-Wl,--whole-archive ${CONVARCS:.a=_pic.a} -Wl,--no-whole-archive
LDADD+=		-lm
DPADD=		${CONVARCS} ${LIBM}

SRCS=		${libstdc___la_SOURCES}

# Override bsd.lib.mk scripts
libstdc++.a: ${SRCS:.cc=.o} script.ar
	@${ECHO} building static ${LIB} library via script
	rm -f ${.TARGET}
	${AR} -M < script.ar
	${RANLIB} ${.TARGET}

libstdc++_p.a: ${SRCS:.cc=.po} script_p.ar
	@${ECHO} building profiled ${LIB} library via script
	rm -f ${.TARGET}
	${AR} -M < script_p.ar
	${RANLIB} ${.TARGET}

# Required by compatibility-atomic-c++0x.cc in ${cxx11_sources}
gstdint.h:
	@echo "#include <stdint.h>" > ${.TARGET}

script.ar:
	@echo "CREATE libstdc++.a"	>  ${.TARGET}
.for arc in ${CONVARCS}
	@echo "ADDLIB ${arc}"		>> ${.TARGET}
.endfor
.for obj in ${SRCS:.cc=.o}
	@echo "ADDMOD ${obj}"		>> ${.TARGET}
.endfor
	@echo "SAVE"			>> ${.TARGET}
	@echo "END"			>> ${.TARGET}

script_p.ar:
	@echo "CREATE libstdc++_p.a"	>  ${.TARGET}
.for arc in ${CONVARCS:.a=_p.a}
	@echo "ADDLIB ${arc}"		>> ${.TARGET}
.endfor
.for obj in ${SRCS:.cc=.po}
	@echo "ADDMOD ${obj}"		>> ${.TARGET}
.endfor
	@echo "SAVE"			>> ${.TARGET}
	@echo "END"			>> ${.TARGET}

libstdc++.map: ${PREDIR}/gnu.ver ${PREDIR}/float128.ver
	cat ${.ALLSRC} | grep -E -v '^[	 ]*#(#| |$$)' | \
	${CC} -E -P -include ${.CURDIR}/../headers/config.h - > ${.TARGET}

GENFILES=	gstdint.h script.ar script_p.ar libstdc++.map
CLEANFILES=	${GENFILES}
beforedepend:	${GENFILES}

.include <bsd.lib.mk>
