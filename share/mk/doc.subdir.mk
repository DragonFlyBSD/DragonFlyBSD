# Taken from:
#	Id: bsd.subdir.mk,v 1.27 1999/03/21 06:43:40 bde
#
# $DragonFly: doc/share/mk/doc.subdir.mk,v 1.1.1.1 2004/04/02 09:36:36 hmp Exp $
# $DragonFly: doc/share/mk/doc.subdir.mk,v 1.1.1.1 2004/04/02 09:36:36 hmp Exp $
#
# This include file <doc.subdir.mk> contains the default targets
# for building subdirectories in the FreeBSD Documentation Project.
#
# For all of the directories listed in the variable SUBDIR, the
# specified directory will be visited and the target made. There is
# also a default target which allows the command "make subdir" where
# subdir is any directory listed in the variable SUBDIR.
#

# ------------------------------------------------------------------------
#
# Document-specific variables:
#
#	SUBDIR			A list of subdirectories that should be
#				built as well.  Each of the targets will
#				execute the same target in the
#				subdirectories.
#
#	COMPAT_SYMLINK		Create a symlink named in this variable
#				to this directory, when installed.
#
#	ROOT_SYMLINKS		Create symlinks to the named directories
#				in the document root, if the current
#				language is the primary language (the
#				PRI_LANG variable).
#

# ------------------------------------------------------------------------
#
# Provided targets:
#
#	install:
#	package:
#			Go down subdirectories and call these targets
#			along the way, and then call the real target
#			here.
#
#	clean:
#			Remove files created by the build process (using
#			defaults specified by environment)
#
#	cleandir:
#			Remove the object directory, if any.
#
#	cleanall:
#			Remove all possible generated files (all predictable
#			combinations of ${FORMAT} values)
#

.if !target(__initialized__)
__initialized__:
.if exists(${.CURDIR}/../Makefile.inc)
.include "${.CURDIR}/../Makefile.inc"
.endif
.endif

.if !target(install)
install: afterinstall symlinks 
afterinstall: realinstall
realinstall: beforeinstall _SUBDIRUSE
.endif

package: realpackage symlinks
realpackage: _SUBDIRUSE

.if !defined(IGNORE_COMPAT_SYMLINK) && defined(COMPAT_SYMLINK)
SYMLINKS+= ${DOCDIR} ${.CURDIR:T:ja_JP.eucJP=ja} \
	   ${COMPAT_SYMLINK:ja=ja_JP.eucJP}
.endif

.if defined(PRI_LANG) && defined(ROOT_SYMLINKS) && !empty(ROOT_SYMLINKS)
.if ${PRI_LANG} == ${LANGCODE}
.for _tmp in ${ROOT_SYMLINKS}
SYMLINKS+= ${DOCDIR} ${LANGCODE:ja_JP.eucJP=ja}/${.CURDIR:T}/${_tmp} ${_tmp}
.endfor
.endif
.endif

.if !target(symlinks)
symlinks:
.if defined(SYMLINKS) && !empty(SYMLINKS)
	@set $$(${ECHO_CMD} ${SYMLINKS}); \
	while : ; do \
		case $$# in \
			0) break;; \
			[12]) ${ECHO_CMD} "warn: empty SYMLINKS: $$1 $$2"; break;; \
		esac; \
		d=$$1; shift; \
		l=$$1; shift; \
		t=$$1; shift; \
		if [ ! -e $${d}/$${l} ]; then \
			${ECHO} "$${d}/$${l} doesn't exist, not linking"; \
		else \
			${ECHO} $${d}/$${t} -\> $${d}/$${l}; \
			(cd $${d} && ${RM} -rf $${t}); \
			(cd $${d} && ${LN} -s $${l} $${t}); \
		fi; \
	done
.endif
.endif

.for __target in beforeinstall afterinstall realinstall realpackage
.if !target(${__target})
${__target}:
.endif
.endfor

_SUBDIRUSE: .USE
.for entry in ${SUBDIR}
	@${ECHODIR} "===> ${DIRPRFX}${entry}"
	@cd ${.CURDIR}/${entry} && \
	${MAKE} ${.TARGET:S/realpackage/package/:S/realinstall/install/} \
		DIRPRFX=${DIRPRFX}${entry}/
.endfor

.if !defined(NOINCLUDEMK)

.include <bsd.obj.mk>

.else

.MAIN: all

${SUBDIR}::
	@cd ${.CURDIR}/${.TARGET} && ${MAKE} all

.for __target in all cleandir lint objlink install
.if !target(${__target})
${__target}: _SUBDIRUSE
.endif
.endfor

.if !target(obj)
obj:	_SUBDIRUSE
	@if ! [ -d ${CANONICALOBJDIR}/ ]; then \
		${MKDIR} -p ${CANONICALOBJDIR}; \
		if ! [ -d ${CANONICALOBJDIR}/ ]; then \
			${ECHO_CMD} "Unable to create ${CANONICALOBJDIR}."; \
			exit 1; \
		fi; \
		${ECHO} "${CANONICALOBJDIR} created ${.CURDIR}"; \
	fi
.endif

.if !target(objlink)
objlink: _SUBDIRUSE
	@if [ -d ${CANONICALOBJDIR}/ ]; then \
		${RM} -f ${.CURDIR}/obj; \
		${LN} -s ${CANONICALOBJDIR} ${.CURDIR}/obj; \
	else \
		${ECHO_CMD} "No ${CANONICALOBJDIR} to link to - do a make obj."; \
	fi
.endif

.if !target(whereobj)
whereobj:
	@${ECHO_CMD} ${.OBJDIR}
.endif

cleanobj:
	@if [ -d ${CANONICALOBJDIR}/ ]; then \
		${RM} -rf ${CANONICALOBJDIR}; \
	else \
		cd ${.CURDIR} && ${MAKE} clean cleandepend; \
	fi
	@if [ -h ${.CURDIR}/obj ]; then ${RM} -f ${.CURDIR}/obj; fi

.if !target(clean)
clean: _SUBDIRUSE
.if defined(CLEANFILES) && !empty(CLEANFILES)
	${RM} -f ${CLEANFILES}
.endif
.if defined(CLEANDIRS) && !empty(CLEANDIRS)
	${RM} -rf ${CLEANDIRS}
.endif
.if defined(IMAGES_LIB) && !empty(LOCAL_IMAGES_LIB_DIR)
	${RM} -rf ${LOCAL_IMAGES_LIB_DIR}
.endif
.endif

cleandir: cleanobj _SUBDIRUSE

.endif # end of NOINCLUDEMK section

#
# Create /usr/obj image subdirs when ${IMAGES} contains subdir/image.xxx
#

_imagesubdir=
.for _imagedir in ${IMAGES:H}
.if ${_imagesubdir:M${_imagedir}} == ""
_imagesubdir+= ${_imagedir}
.endif
.endfor

.if ${_imagesubdir} != ""
_IMAGESUBDIR: .USE
.for dir in ${_imagesubdir}
	@if ! [ -d ${CANONICALOBJDIR}/${dir}/ ]; then \
		${MKDIR} -p ${CANONICALOBJDIR}/${dir}; \
		if ! [ -d ${CANONICALOBJDIR}/${dir}/ ]; then \
			${ECHO_CMD} "Unable to create ${CANONICALOBJDIR}/${dir}/."; \
			exit 1; \
		fi; \
		${ECHO} "${CANONICALOBJDIR}/${dir}/ created for ${.CURDIR}"; \
	fi
.endfor

obj: _IMAGESUBDIR
.endif

cleanall:
	${MAKE} FORMATS="${ALL_FORMATS}" clean
