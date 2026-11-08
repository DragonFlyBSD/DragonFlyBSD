.include <bsd.init.mk>

# Hint HOST_CCVER handling.
__USE_HOST_CCVER=
NO_WERROR=

all: objwarn

.if defined(LIB) && !empty(LIB)
. if !empty(SRCS)
OBJS+=		${SRCS:N*.h:R:S/$/.no/g}
. endif
.endif

.if defined(LIB) && !empty(LIB)
_LIBS=		lib${LIB}.na

lib${LIB}.na: ${OBJS} ${STATICOBJS}
	@${ECHO} building native static ${LIB} library
	rm -f ${.TARGET}
	${NXAR} ${ARFLAGS} ${.TARGET} `lorder ${OBJS} ${STATICOBJS} | tsort -q` ${ARADD}
	${NXRANLIB} ${.TARGET}
.endif

all: ${_LIBS}

afterdepend: all

_EXTRADEPEND:
	sed -i '' -Ee 's/^([^.]+)\.o:/\1.no:/' ${DEPENDFILE}

.include <bsd.dep.mk>

.if !exists(${.OBJDIR}/${DEPENDFILE})
.if defined(LIB) && !empty(LIB)
${OBJS} ${STATICOBJS}: ${SRCS:M*.h}
.endif
.endif

.if !target(clean)
clean:
.if defined(CLEANFILES) && !empty(CLEANFILES)
	rm -f ${CLEANFILES}
.endif
.if defined(LIB) && !empty(LIB)
	rm -f a.out ${OBJS} ${OBJS:S/$/.tmp/} ${STATICOBJS}
.endif
.if defined(_LIBS) && !empty(_LIBS)
	rm -f ${_LIBS}
.endif
.if defined(CLEANDIRS) && !empty(CLEANDIRS)
	rm -rf ${CLEANDIRS}
.endif
.endif

.include <bsd.obj.mk>
.include <bsd.sys.mk>
