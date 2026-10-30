#	from: @(#)bsd.lib.mk	5.26 (Berkeley) 5/2/91
# $FreeBSD: src/share/mk/bsd.lib.mk,v 1.91.2.15 2002/08/07 16:31:50 ru Exp $
#

.include <bsd.init.mk>

# Set up the variables controlling shared libraries.  After this section,
# SHLIB_NAME will be defined only if we are to create a shared library.
# SHLIB_LINK will be defined only if we are to create a link to it.
# INSTALL_PIC_ARCHIVE will be defined only if we are to create a PIC archive.
.if defined(NOPIC)
.undef SHLIB_NAME
.undef INSTALL_PIC_ARCHIVE
.else
.if !defined(SHLIB_NAME) && defined(LIB) && defined(SHLIB_MAJOR)
SHLIB_NAME=	lib${LIB}.so.${SHLIB_MAJOR}
.endif
.if defined(SHLIB_NAME) && ${SHLIB_NAME:M*.so.*}
SHLIB_LINK?=	${SHLIB_NAME:R}
.endif
SONAME?=	${SHLIB_NAME}
.endif

.if defined(DEBUG_FLAGS)
CFLAGS+=	${DEBUG_FLAGS}
.else
STRIP?=		-s
.endif

.include <bsd.libnames.mk>

.if !defined(PRIVATELIB)
TARGET_LIBDIR?=		${LIBDIR}
TARGET_DEBUGLIBDIR?=	${DEBUGLIBDIR}
TARGET_PROFLIBDIR?=	${PROFLIBDIR}
TARGET_SHLIBDIR?=	${SHLIBDIR}
.else
TARGET_LIBDIR?=		${LIBDIR}/priv
TARGET_DEBUGLIBDIR?=	${DEBUGLIBDIR:S!/debug$!/priv/debug!}
TARGET_PROFLIBDIR?=	${PROFLIBDIR:S!/profile$!/priv/profile!}
. if !empty(PRIVATELIB) && ${PRIVATELIB} == "shpub"
TARGET_SHLIBDIR?=	${SHLIBDIR}
. else
TARGET_SHLIBDIR?=	${SHLIBDIR}/priv
. endif
.endif

PRIVATELIB_CFLAGS?=	-I${_SHLIBDIRPREFIX}/usr/include/priv
PRIVATELIB_LDFLAGS?=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

# Add .po (profiling object) and .So (PIC shared object)
.SUFFIXES: .po .So

.if !defined(PICFLAG)
PICFLAG=	-fpic
.endif

PO_FLAG=	-pg
PO_CFLAGS=	${CFLAGS:N-ffunction-sections}
PO_CXXFLAGS=	${CXXFLAGS:N-ffunction-sections}

# Flags that distinguish the kind of object being built.
_CFLAGS_MODE.po=	${PO_FLAG} ${STATIC_CFLAGS} ${PO_CFLAGS}
_CFLAGS_MODE.So=	${PICFLAG} -DPIC ${SHARED_CFLAGS} ${CFLAGS}
_CXXFLAGS_MODE.po=	${PO_FLAG} ${STATIC_CXXFLAGS} ${PO_CXXFLAGS}
_CXXFLAGS_MODE.So=	${PICFLAG} -DPIC ${SHARED_CXXFLAGS} ${CXXFLAGS}
_FFLAGS_MODE.po=	${PO_FLAG} ${FFLAGS}
_FFLAGS_MODE.So=	${PICFLAG} -DPIC ${FFLAGS}
_OBJCFLAGS_MODE.po=	${PO_FLAG} ${OBJCFLAGS}
_OBJCFLAGS_MODE.So=	${PICFLAG} -DPIC ${OBJCFLAGS}
_ASMFLAGS_MODE.po=	-DPROF ${CFLAGS}
_ASMFLAGS_MODE.So=	${PICFLAG} -DPIC ${CFLAGS}
_AFLAGS_MODE.po=	-DPROF ${AFLAGS}
_AFLAGS_MODE.So=	${PICFLAG} -DPIC ${AFLAGS}

.c.po .c.So:
	${COMPILE.c} ${.IMPSRC} -o ${.TARGET}

.cc.po .C.po .cpp.po .cxx.po:
	${COMPILE.cc} ${.IMPSRC} -o ${.TARGET}
.cc.So .C.So .cpp.So .cxx.So:
	${COMPILE.cc} ${.IMPSRC} -o ${.TARGET}

.f.po .f.So:
	${COMPILE.f} ${.IMPSRC} -o ${.TARGET}

.m.po .m.So:
	${COMPILE.m} ${.IMPSRC} -o ${.TARGET}

.S.po .S.So:
	${COMPILE.S} ${.IMPSRC} -o ${.TARGET}

.s.po .s.So:
	${COMPILE.s} -o ${.TARGET} ${.IMPSRC}

# Compile commands for targets with an explicit SRC.<target-root>.o mapping.
# Use the first prerequisite for explicit rules where .IMPSRC is empty.
_LIB_COMPILE.c: .USE
	${COMPILE.c} ${.IMPSRC:U${.ALLSRC:[1]}} -o ${.TARGET}
_LIB_COMPILE.cc _LIB_COMPILE.C _LIB_COMPILE.cpp _LIB_COMPILE.cxx: .USE
	${COMPILE.cc} ${.IMPSRC:U${.ALLSRC:[1]}} -o ${.TARGET}
_LIB_COMPILE.f: .USE
	${COMPILE.f} ${.IMPSRC:U${.ALLSRC:[1]}} -o ${.TARGET}
_LIB_COMPILE.m: .USE
	${COMPILE.m} ${.IMPSRC:U${.ALLSRC:[1]}} -o ${.TARGET}
_LIB_COMPILE.S: .USE
	${COMPILE.S} ${.IMPSRC:U${.ALLSRC:[1]}} -o ${.TARGET}
_LIB_COMPILE.s: .USE
	${COMPILE.s} -o ${.TARGET} ${.IMPSRC:U${.ALLSRC:[1]}}

all: objwarn

.include <bsd.symver.mk>

# Allow libraries to specify their own version map or have it
# automatically generated (see bsd.symver.mk above).
.if !defined(NO_SYMVER) && !empty(VERSION_MAP)
${SHLIB_NAME}:	${VERSION_MAP}
LDFLAGS+=	-Wl,--version-script=${VERSION_MAP}
.endif

.if defined(LIB) && !empty(LIB) || defined(SHLIB_NAME)
. if !empty(SRCS)
OBJS+=		${SRCS:N*.h:R:S/$/.o/g}
. endif
.endif

.if defined(LIB) && !empty(LIB)
_LIBS=		lib${LIB}.a

. if ! target(lib${LIB}.a)
lib${LIB}.a: ${OBJS} ${STATICOBJS}
	@${ECHO} building static ${LIB} library
	rm -f ${.TARGET}
	${AR} ${ARFLAGS} ${.TARGET} `lorder ${OBJS} ${STATICOBJS} | tsort -q` ${ARADD}
	${RANLIB} ${.TARGET}
. endif
.endif

.if !defined(INTERNALLIB) || defined(INTERNALLIBPROF)
. if !defined(NOPROFILE) && defined(LIB) && !empty(LIB)
_LIBS+=		lib${LIB}_p.a
POBJS+=		${OBJS:.o=.po} ${STATICOBJS:.o=.po}

.  if ! target(lib${LIB}_p.a)
lib${LIB}_p.a: ${POBJS}
	@${ECHO} building profiled ${LIB} library
	rm -f ${.TARGET}
	${AR} ${ARFLAGS} ${.TARGET} `lorder ${POBJS} | tsort -q` ${ARADD}
	${RANLIB} ${.TARGET}
.  endif
. endif
.endif

.if (!defined(INTERNALLIB) && defined(SHLIB_NAME)) || \
    (defined(INSTALL_PIC_ARCHIVE) && defined(LIB) && !empty(LIB))
SOBJS+=		${OBJS:.o=.So}
.endif

# SRC.<target-root>.o provides a source for targets that are not derived from
# SRCS.  OBJS, POBJS and SOBJS share the same target root, so an .o-only OBJS
# entry automatically gets .po and .So mappings as well.
_LIB_SRC_EXTS=	c cc C cpp cxx m f s S

.for _o in ${OBJS} ${POBJS} ${SOBJS}
. if !empty(SRC.${_o:R}.o)
_ext:=	${SRC.${_o:R}.o:[1]:E}
.  if empty(_LIB_SRC_EXTS:M${_ext})
.error Unsupported source suffix "${_ext}" for ${_o}
.  endif
${_o}: ${SRC.${_o:R}.o} _LIB_COMPILE.${_ext}
. endif
.endfor

.if !defined(INTERNALLIB) && defined(SHLIB_NAME)
_LIBS+=		${SHLIB_NAME}

${SHLIB_NAME}: ${SOBJS}
	@${ECHO} building shared library ${SHLIB_NAME}
	rm -f ${.TARGET} ${SHLIB_LINK}
.if defined(SHLIB_LINK)
	${LN} -fs ${.TARGET} ${SHLIB_LINK}
.endif
	${CC_LINK} ${LDFLAGS} -shared -Wl,-x \
	    -o ${.TARGET} -Wl,-soname,${SONAME} \
	    `lorder ${SOBJS} | tsort -q` ${LDADD}
.endif

.if defined(INSTALL_PIC_ARCHIVE) && defined(LIB) && !empty(LIB)
_LIBS+=		lib${LIB}_pic.a

lib${LIB}_pic.a: ${SOBJS}
	@${ECHO} building special pic ${LIB} library
	rm -f ${.TARGET}
	${AR} ${ARFLAGS} ${.TARGET} ${SOBJS} ${ARADD}
	${RANLIB} ${.TARGET}
.endif

all: ${_LIBS}

.if !defined(NOMAN)
all: _manpages
.endif

_EXTRADEPEND:
.if !defined(NOIMPLICITDEPEND)
	@TMP=_depend$$$$; \
	sed -e 's/^\([^\.]*\).o[ ]*:/\1.o \1.po \1.So:/' < ${DEPENDFILE} \
	    > $$TMP; \
	mv $$TMP ${DEPENDFILE}
.endif
.if !defined(NOEXTRADEPEND) && defined(SHLIB_NAME)
.if defined(DPADD) && !empty(DPADD)
	echo ${SHLIB_NAME}: ${DPADD} >> ${DEPENDFILE}
.endif
.endif

.if !target(install)

.if defined(PRECIOUSLIB) && !defined(NOFSCHG)
SHLINSTALLFLAGS+= -fschg
.endif

_INSTALLFLAGS:=	${INSTALLFLAGS}
.for ie in ${INSTALLFLAGS_EDIT}
_INSTALLFLAGS:=	${_INSTALLFLAGS${ie}}
.endfor
_SHLINSTALLFLAGS:=	${SHLINSTALLFLAGS}
.for ie in ${INSTALLFLAGS_EDIT}
_SHLINSTALLFLAGS:=	${_SHLINSTALLFLAGS${ie}}
.endfor

.if !defined(INTERNALLIB)
realinstall: _libinstall
.ORDER: beforeinstall _libinstall
_libinstall:
.if defined(LIB) && !empty(LIB) && !defined(NOINSTALLLIB)
	${INSTALL} -C -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${_INSTALLFLAGS} lib${LIB}.a ${DESTDIR}${TARGET_LIBDIR}
.endif
.if !defined(NOPROFILE) && defined(LIB) && !empty(LIB)
	${INSTALL} -C -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${_INSTALLFLAGS} lib${LIB}_p.a ${DESTDIR}${TARGET_PROFLIBDIR}/lib${LIB}.a
.endif
.if defined(SHLIB_NAME)
	${INSTALL} ${STRIP} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${_INSTALLFLAGS} ${_SHLINSTALLFLAGS} \
	    ${SHLIB_NAME} ${DESTDIR}${TARGET_SHLIBDIR}
.if defined(SHLIB_LINK)
.if ${TARGET_SHLIBDIR} == ${TARGET_LIBDIR}
	${LN} -fs ${SHLIB_NAME} ${DESTDIR}${TARGET_LIBDIR}/${SHLIB_LINK}
.else
	${LN} -fs ${_SHLIBDIRPREFIX}${TARGET_SHLIBDIR}/${SHLIB_NAME} \
	    ${DESTDIR}${TARGET_LIBDIR}/${SHLIB_LINK}
.endif
.endif
.endif
.if defined(INSTALL_PIC_ARCHIVE) && defined(LIB) && !empty(LIB)
	${INSTALL} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${_INSTALLFLAGS} lib${LIB}_pic.a ${DESTDIR}${TARGET_LIBDIR}
.endif
.endif # !defined(INTERNALLIB)

.include <bsd.nls.mk>
.include <bsd.files.mk>
.include <bsd.incs.mk>
.include <bsd.links.mk>

.if !defined(NOMAN)
realinstall: _maninstall
.ORDER: beforeinstall _maninstall
.endif

.endif

.if !defined(NOMAN)
.include <bsd.man.mk>
.endif

.include <bsd.dep.mk>

.if !exists(${.OBJDIR}/${DEPENDFILE})
.if defined(LIB) && !empty(LIB)
${OBJS} ${STATICOBJS} ${POBJS}: ${SRCS:M*.h}
.endif
.if defined(SHLIB_NAME) || \
    (defined(INSTALL_PIC_ARCHIVE) && defined(LIB) && !empty(LIB))
${SOBJS}: ${SRCS:M*.h}
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
.if defined(SHLIB_NAME) || \
    (defined(INSTALL_PIC_ARCHIVE) && defined(LIB) && !empty(LIB))
	rm -f ${SOBJS} ${SOBJS:.So=.so} ${SOBJS:S/$/.tmp/}
.endif
.if !defined(INTERNALLIB) || defined(INTERNALLIBPROF)
.if !defined(NOPROFILE) && defined(LIB) && !empty(LIB)
	rm -f ${POBJS} ${POBJS:S/$/.tmp/}
.endif
.endif # !defined(INTERNALLIB) || defined(INTERNALLIBPROF)
.if !defined(INTERNALLIB)
.if defined(SHLIB_NAME)
.if defined(SHLIB_LINK)
	rm -f ${SHLIB_LINK}
.endif
.if defined(LIB) && !empty(LIB)
	rm -f lib${LIB}.so.* lib${LIB}.so
.endif
.endif
.endif # !defined(INTERNALLIB)
.if defined(_LIBS) && !empty(_LIBS)
	rm -f ${_LIBS}
.endif
.if defined(CLEANDIRS) && !empty(CLEANDIRS)
	rm -rf ${CLEANDIRS}
.endif
.endif

.include <bsd.obj.mk>
.include <bsd.sys.mk>
