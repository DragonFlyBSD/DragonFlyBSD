# $FreeBSD$

.if !target(__<bsd.symver.mk>__)
__<bsd.symver.mk>__:

.include <bsd.init.mk>

# Generate the version map given the version definitions
# and symbol maps.
.if !defined(NO_SYMVER) && !empty(VERSION_DEF) && !empty(SYMBOL_MAPS)
# Find the awk script that generates the version map.
VERSION_GEN?=	version_gen.awk
VERSION_MAP?=	Version.map

CLEANFILES+=	${VERSION_MAP}

.if exists(${.PARSEDIR}/${VERSION_GEN})
_vgen:=	${.PARSEDIR}/${VERSION_GEN}
.else
.error	${VERSION_GEN} not found in ${.PARSEDIR}
.endif

# Run the symbol maps through the C preprocessor before passing
# them to the symbol version generator.
${VERSION_MAP}: ${VERSION_DEF} ${_vgen} ${SYMBOL_MAPS}
	cat ${SYMBOL_MAPS} | ${CPP} - - \
	    | awk -v vfile=${VERSION_DEF} -f ${_vgen} > ${.TARGET}
.endif	# !empty(VERSION_DEF) && !empty(SYMBOL_MAPS)
.endif  # !target(__<bsd.symver.mk>__)
