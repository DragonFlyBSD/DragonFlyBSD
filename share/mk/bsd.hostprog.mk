# $DragonFly: src/share/mk/bsd.hostprog.mk,v 1.2 2004/06/15 11:56:03 joerg Exp $

.include <bsd.init.mk>

.SUFFIXES: .out .nx .no .c .cc .cpp .cxx .C .m .y .l .s .S

.if defined(PROG_CXX)
PROG=	${PROG_CXX}
.endif

.if !defined(SRCS)
.if defined(PROG_CXX)
SRCS=	${PROG}.cc
.else
SRCS=	${PROG}.c
.endif
.endif

all: objwarn ${PROG}.nx

.if defined(PROG)

# If there are Objective C sources, link with Objective C libraries.
.if ${SRCS:M*.m} != ""
OBJCLIBS?= -lobjc
LDADD+=	${OBJCLIBS}
.endif

OBJS+=  ${SRCS:N*.h:N*.patch:R:S/$/.no/g}
_PATCHES= ${SRCS:M*.patch}
.for _PATCH in ${SRCS:T:N*.h.patch:M*.patch}
.for _OBJ in ${_PATCH:R:R:S/$/.no/}
OBJS:=	${OBJS:N${_OBJ}} ${_OBJ}
.endfor
.endfor
.undef _PATCHES

${PROG}.nx: ${OBJS}
.if defined(PROG_CXX)
	${NXCXX} ${NXCXXFLAGS} ${NXLDFLAGS} -o ${.TARGET} ${OBJS} ${LDADD}
.else
	${NXCC} ${NXCFLAGS} ${NXLDFLAGS} -o ${.TARGET} ${OBJS} ${LDADD}
.endif
.endif

CLEANFILES+= ${PROG}.nx ${OBJS}

all: ${PROG}.nx

_EXTRADEPEND:
	echo ${PROG}.nx: ${LIBC} ${DPADD} >> ${DEPENDFILE}
.if defined(PROG_CXX)
	echo ${PROG}.nx: ${LIBSTDCPLUSPLUS} >> ${DEPENDFILE}
.endif

.include <bsd.dep.mk>

.if defined(PROG) && !exists(${.OBJDIR}/${DEPENDFILE})
${OBJS}: ${SRCS:M*.h}
.endif

.include <bsd.obj.mk>

.include <bsd.sys.mk>
