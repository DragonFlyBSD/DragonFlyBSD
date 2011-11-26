.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils

PROG=	c++filt
SRCS=   cxxfilt.c
MAN=	c++filt220.1
#MLINKS+=c++filt220.1 c++filt.1
WARNS=	1

c++filt220.1: ${.CURDIR}/c++filt.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	c++filt220.1

.include <bsd.prog.mk>
