# $DragonFly: src/share/mk/bsd.hostlib.mk,v 1.2 2007/08/25 15:22:30 corecode Exp $

.include <bsd.init.mk>

# prefer .s to a .c, add .po, remove stuff not used in the BSD libraries
# .So used for PIC object files
.SUFFIXES:
.SUFFIXES: .out .no .c .cc .cpp .cxx .C .y .l

.c.no:
	${NXCC} ${_${.IMPSRC}_FLAGS} ${NXCFLAGS} -c ${.IMPSRC} -o ${.TARGET}
	@${NXLD} -o ${.TARGET}.tmp -x -r ${.TARGET}
	@mv ${.TARGET}.tmp ${.TARGET}

.cc.no .C.no .cpp.no .cxx.no:
	${NXCXX} ${_${.IMPSRC}_FLAGS} ${NXCXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
	@${NXLD} -o ${.TARGET}.tmp -x -r ${.TARGET}
	@mv ${.TARGET}.tmp ${.TARGET}

all: objwarn

.if defined(LIB) && !empty(LIB)
. if !empty(SRCS)
OBJS+=  ${SRCS:N*.h:N*.patch:R:S/$/.no/g}
.  for _PATCH in ${SRCS:T:N*.no_obj.patch:N*.h.patch:M*.patch}
.   for _OBJ in ${_PATCH:R:R:S/$/.no/}
OBJS:=	${OBJS:N${_OBJ}} ${_OBJ}
.   endfor
.  endfor
. endif
.endif

.if defined(LIB) && !empty(LIB)
_LIBS=		lib${LIB}.na

lib${LIB}.na: ${OBJS} ${STATICOBJS}
	@${ECHO} building native static ${LIB} library
	rm -f ${.TARGET}
	${NXAR} cq ${.TARGET} `lorder ${OBJS} ${STATICOBJS} | tsort -q` ${ARADD}
	${NXRANLIB} ${.TARGET}
.endif

all: ${_LIBS}

afterdepend: all

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

