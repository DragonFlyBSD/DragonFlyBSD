# Make all libraries.
# $Id: Makefile,v 1.10 2005/04/05 20:53:46 cpressey Exp $

SUBDIR=	libaura libdfui libinstaller lua
TARGETS=all in_cvs clean

.for _target in ${TARGETS}
${_target}:
	@for _subdir in ${SUBDIR}; do					\
		echo "===> $${_subdir}" &&				\
		cd ${.CURDIR}$//$${_subdir} &&				\
		${MAKE} ${_target};					\
	done
.endfor
