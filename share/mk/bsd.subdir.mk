# The include file <bsd.subdir.mk> contains the default targets
# for building subdirectories. 
#
# For all of the directories listed in the variable SUBDIRS, the
# specified directory will be visited and the target made. There is
# also a default target which allows the command "make subdir" where
# subdir is any directory listed in the variable SUBDIRS.
#
#
# +++ variables +++
#
# SUBDIR	A list of subdirectories that should be built as well.
#		Each of the targets will execute the same target in the
#		subdirectories.
#
# SUBDIR_ORDERED A list of subdirectories which also must be included in
#		in SUBDIR which have ordering requirements.  If this
#		Make variable does not exist then all subdirectories are
#		assumed to be strictly ordered.
#
# +++ targets +++
#
#	afterinstall, all, all-man, beforeinstall, checkdpadd,
#	clean, cleandepend, cleandir, depend, install, lint, maninstall,
#	manlint, obj, objlink, realinstall, regress, tags
#

.include <bsd.init.mk>

# If SUBDIR_ORDERED not specified we default strongly ordering all
# subdirectories.
#
SUBDIR_ORDERED?= ${SUBDIR}

__targets= \
	checkdpadd clean cleandepend cleandir cleanobj \
	obj objlink tags depend all all-man \
	maninstall realinstall	\
	lint manlint regress \
	buildfiles buildincludes installfiles installincludes
__targets+=	mandiff # XXX temporary target

.for __target in ${__targets}

.if defined(SUBDIR) && !empty(SUBDIR) && !defined(NO_SUBDIR)

_SUBDIR_${__target}: ${SUBDIR:S/^/_SUBDIR_${__target}_/}

# Now create the command set for each subdirectory and target
#

.for entry in ${SUBDIR}
_SUBDIR_${__target}_${entry}:
		@(if test -d ${.CURDIR}/${entry}.${MACHINE_ARCH}; then \
			${ECHODIR} "===> ${DIRPRFX}${entry}.${MACHINE_ARCH}"; \
			edir=${entry}.${MACHINE_ARCH}; \
			cd ${.CURDIR}/$${edir}; \
		else \
			${ECHODIR} "===> ${DIRPRFX}${entry}"; \
			edir=${entry}; \
			cd ${.CURDIR}/$${edir}; \
		fi; \
		${MAKE} ${__target:realinstall=install} \
		    DIRPRFX=${DIRPRFX}$$edir/;)

.endfor

# order subdirectories for each target, set up dependency
#
.ORDER: ${SUBDIR_ORDERED:S/^/_SUBDIR_${__target}_/}

.else

_SUBDIR_${__target}: .USE

.endif

.endfor

${SUBDIR}: .PHONY
	@if test -d ${.TARGET}.${MACHINE_ARCH}; then \
		cd ${.CURDIR}/${.TARGET}.${MACHINE_ARCH}; \
	else \
		cd ${.CURDIR}/${.TARGET}; \
	fi; \
	${MAKE} all


.for __target in ${__targets}
${__target}: _SUBDIR_${__target}
.endfor

.for __target in files includes
.for __stage in build install
${__stage}${__target}:
.if make(${__stage}${__target})
${__stage}${__target}: _SUBDIR_${__stage}${__target}
.endif
.endfor
${__target}:
	cd ${.CURDIR}; ${MAKE} build${__target}; ${MAKE} install${__target}
.endfor

.if !target(install)
.if !target(beforeinstall)
beforeinstall:
.endif
.if !target(afterinstall)
afterinstall:
.endif
install: beforeinstall realinstall afterinstall
.ORDER: beforeinstall realinstall afterinstall
.endif

.ORDER: ${__targets:S/^/_SUBDIR_/}
