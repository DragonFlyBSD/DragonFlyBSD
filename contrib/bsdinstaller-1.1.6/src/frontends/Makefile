# Make all frontends.
# $Id: Makefile,v 1.8 2005/04/05 20:53:46 cpressey Exp $

SUBDIR=	cgi ncurses test
TARGETS=all in_cvs clean

# Don't build Qt frontend on NetBSD yet.
OSTYPE!=sysctl -n kern.ostype
.if ${OSTYPE} != "NetBSD"
SUBDIR+= qt
.endif

.for _target in ${TARGETS}
${_target}:
	@for _subdir in ${SUBDIR}; do					\
		echo "===> $${_subdir}" &&				\
		cd ${.CURDIR}$//$${_subdir} &&				\
		${MAKE} ${_target};					\
	done
.endfor
