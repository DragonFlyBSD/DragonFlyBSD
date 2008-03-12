# Make everything.
# $Id: Makefile,v 1.4 2005/04/05 20:53:46 cpressey Exp $

SUBDIR=	lib backend frontends
TARGETS=all in_cvs clean

.for _target in ${TARGETS}
${_target}:
	@for _subdir in ${SUBDIR}; do					\
		echo "===> $${_subdir}" &&				\
		cd ${.CURDIR}$//$${_subdir} &&				\
		${MAKE} ${_target};					\
	done
.endfor
