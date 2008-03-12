# BSD Makefile for dfuife_qt.
# $Id: Makefile,v 1.6 2005/04/05 20:53:46 cpressey Exp $

MAKEFILE_QT_EXISTS!=[ -r Makefile.qt ] || echo "NO"

all:
.if ${MAKEFILE_QT_EXISTS} == "NO"
	QTDIR=/usr/X11R6 qmake \
		-spec /usr/local/share/qt/mkspecs/freebsd-g++ \
		dfuife_qt.pro -o Makefile.qt
.endif
	QTDIR=/usr/X11R6 make -f Makefile.qt

# XXX eek! ... build and symlink dependencies here first.
in_cvs: all

clean:
.if ${MAKEFILE_QT_EXISTS} != "NO"
	make -f Makefile.qt clean
	rm -f Makefile.qt
.endif
	rm -f dfuife_qt *.o
