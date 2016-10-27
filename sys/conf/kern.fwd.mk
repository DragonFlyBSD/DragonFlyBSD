# Create forwarding headers for ${SYSDIR}/cpu/${MACHINE_ARCH}/*.h in
# ${_MACHINE_FWD}/include/machine and share the directory among module build
# directories.
# Define _MACHINE_FWD before inclusion of this file.
.if !defined(_MACHINE_FWD)
.error you must define _MACHINE_FWD in which to generate forwarding headers.
.endif

# _FWDHDRS fills in missing <machine/BLAH.h> headers from <cpu/BLAH.h>,
# allowing us to omit forwarding-only header files in each platform
# architecture's machine/ directory.
#
_cpu_hdrs!=	echo ${SYSDIR}/cpu/${MACHINE_ARCH}/include/*.h
_FWDHDRS=
.for _h in ${_cpu_hdrs}
_fwd:=	${_MACHINE_FWD}/include/machine/${_h:T}
_FWDHDRS:=	${_FWDHDRS} ${_fwd}
${_fwd}: ${_h}
.endfor

# _LHDRS mimics _LHDRS from /usr/src/include/Makefile, directly linking
# sys/BLAH.h as <BLAH.h> for certain header files.  These are used to
# mimic a standard user include topology.  Only the virtual kernel
# build uses these.  e.g. in order for #include <fcntl.h> to work.
#
_lhdrs= aio.h errno.h fcntl.h linker_set.h poll.h syslog.h \
	termios.h ucontext.h
_LHDRS=
.for _h in ${_lhdrs}
_fwd:=	${_MACHINE_FWD}/include/${_h}
_LHDRS:=	${_LHDRS} ${_fwd}
${_fwd}: ${SYSDIR}/sys/${_h}
.endfor

.ORDER: ${_MACHINE_FWD}/include/machine ${_FWDHDRS} ${_LHDRS}

${_MACHINE_FWD} ${_MACHINE_FWD}/include/machine:
	@mkdir -p ${.TARGET}

FORWARD_HEADERS_COOKIE=	${_MACHINE_FWD}/.done
${FORWARD_HEADERS_COOKIE}: ${_MACHINE_FWD}/include/machine ${_FWDHDRS} ${_LHDRS}
	@touch ${.TARGET}

${_FWDHDRS}:
	@(echo "creating machine/ forwarding header ${.TARGET}" 1>&2; \
	echo "/*" ; \
	echo " * CONFIG-GENERATED FILE, DO NOT EDIT" ; \
	echo " */" ; \
	echo ; \
	echo "#ifndef _MACHINE_${.TARGET:T:S/./_/g:tu}_" ; \
	echo "#define _MACHINE_${.TARGET:T:S/./_/g:tu}_" ; \
	echo "#include <cpu/${.TARGET:T}>" ; \
	echo "#endif" ; \
	echo) > ${.TARGET}.${.MAKE.PID}
	@mv -f ${.TARGET}.${.MAKE.PID} ${.TARGET}

${_LHDRS}:
	@(echo "creating sys/ forwarding header ${.TARGET}" 1>&2; \
	echo "/*" ; \
	echo " * CONFIG-GENERATED FILE, DO NOT EDIT" ; \
	echo " */" ; \
	echo ; \
	echo "#ifndef _${.TARGET:T:S/./_/g:tu}_" ; \
	echo "#define _${.TARGET:T:S/./_/g:tu}_" ; \
	echo "#include <sys/${.TARGET:T}>" ; \
	echo "#endif" ; \
	echo) > ${.TARGET}.${.MAKE.PID}
	@mv -f ${.TARGET}.${.MAKE.PID} ${.TARGET}
