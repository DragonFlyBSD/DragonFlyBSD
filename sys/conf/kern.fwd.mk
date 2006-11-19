# $DragonFly: src/sys/conf/kern.fwd.mk,v 1.1 2006/11/19 07:49:34 sephe Exp $

# Create forwarding headers for ${SYSDIR}/cpu/${MACHINE_ARCH}/*.h in
# ${_MACHINE_FWD}/include/machine and share the directory among module build
# directories.
# Define _MACHINE_FWD before inclusion of this file.
.if !defined(_MACHINE_FWD)
.error you must define _MACHINE_FWD in which to generate forwarding headers.
.endif

_cpu_hdrs!=	echo ${SYSDIR}/cpu/${MACHINE_ARCH}/include/*.h
_FWDHDRS=
.for _h in ${_cpu_hdrs}
_fwd:=	${_MACHINE_FWD}/include/machine/${_h:T}
_FWDHDRS:=	${_FWDHDRS} ${_fwd}
${_fwd}: ${_h}
.endfor
${_MACHINE_FWD} ${_MACHINE_FWD}/include/machine:
	@mkdir -p ${.TARGET}

forwarding-headers: ${_MACHINE_FWD}/include/machine ${_FWDHDRS}
	@touch ${_MACHINE_FWD}/.done

${_FWDHDRS}:
	@(echo "creating forwarding header ${.TARGET}" 1>&2; \
	echo "/*" ; \
	echo " * CONFIG-GENERATED FILE, DO NOT EDIT" ; \
	echo " */" ; \
	echo ; \
	echo "#ifndef _MACHINE_${.TARGET:T:S/./_/g:U:R}_H_" ; \
	echo "#define _MACHINE_${.TARGET:T:S/./_/g:U:R}_H_" ; \
	echo "#include <cpu/${.TARGET:T}>" ; \
	echo "#endif" ; \
	echo) > ${.TARGET}
