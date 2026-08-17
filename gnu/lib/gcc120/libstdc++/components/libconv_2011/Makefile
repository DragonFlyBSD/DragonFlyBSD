RELUP=	/../..
.include "../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.src"

LIB=			c++11convenience
INTERNALLIB=		yes
INTERNALLIBPROF=	yes
# build the PIC archive for libstdc++.so
INSTALL_PIC_ARCHIVE=	yes

.PATH:	${SRCDIR}/src/c++11

SRCS=		${libc__11convenience_la_SOURCES}

SRCS:=		${SRCS:Ncxx11-ios_failure.cc}
OBJS+=		cxx11-ios_failure.o	# custom rules below
CLEANFILES+=	cxx11-ios_failure.*.s*

CXXFLAGS+=		-std=gnu++11 -fno-implicit-templates

FLAGS_GROUPS=		implicit char8
implicit_FLAGS=		-fimplicit-templates
implicit_FLAGS_FILES=	hashtable_c++0x.cc
char8_FLAGS=		-fchar8_t
char8_FLAGS_FILES=	codecvt.cc limits.cc

# Derived from libstdc++-v3/src/c++11/Makefile.am:
# Rewrite the type info for __ios_failure.
rewrite_ios_failure_typeinfo = sed -e '/^_*_ZTISt13__ios_failure:/,/_ZTVN10__cxxabiv120__si_class_type_infoE/s/_ZTVN10__cxxabiv120__si_class_type_infoE/_ZTVSt19__iosfail_type_info/'

cxx11-ios_failure.o: cxx11-ios_failure.cc
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} \
		-gno-as-loc-support -S -o ${.TARGET}.s.tmp ${.ALLSRC}
	${rewrite_ios_failure_typeinfo} < ${.TARGET}.s.tmp > ${.TARGET}.s
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} \
		-g0 -c ${.TARGET}.s -o ${.TARGET}

cxx11-ios_failure.po: cxx11-ios_failure.cc
	${CXX} ${PO_FLAG} ${STATIC_CXXFLAGS} ${PO_CXXFLAGS} \
		-gno-as-loc-support -S -o ${.TARGET}.s.tmp ${.ALLSRC}
	${rewrite_ios_failure_typeinfo} < ${.TARGET}.s.tmp > ${.TARGET}.s
	${CXX} ${PO_FLAG} ${STATIC_CXXFLAGS} ${PO_CXXFLAGS} \
		-g0 -c ${.TARGET}.s -o ${.TARGET}

cxx11-ios_failure.So: cxx11-ios_failure.cc
	${CXX} ${PICFLAG} -DPIC ${SHARED_CXXFLAGS} ${CXXFLAGS} \
		-gno-as-loc-support -S -o ${.TARGET}.s.tmp ${.ALLSRC}
	${rewrite_ios_failure_typeinfo} < ${.TARGET}.s.tmp > ${.TARGET}.s
	${CXX} ${PICFLAG} -DPIC ${SHARED_CXXFLAGS} ${CXXFLAGS} \
		-g0 -c ${.TARGET}.s -o ${.TARGET}

CLEANFILES+=	${GENFILES}
beforedepend:	${GENFILES}

.include <bsd.lib.mk>
