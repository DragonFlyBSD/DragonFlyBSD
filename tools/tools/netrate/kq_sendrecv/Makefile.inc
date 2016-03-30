COPTS	+= -fno-common -I${.CURDIR}/../include
LDADD	+= -lpthread
DPADD	+= ${DESTDIR}${LIBDIR}/libpthread.a

WARNS	= 6
MAN	=

BINDIR	= /usr/local/bin
