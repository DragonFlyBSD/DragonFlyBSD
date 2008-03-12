# $Id: Makefile,v 1.10 2005/04/05 20:53:46 cpressey Exp $
# DFUI CGI frontend Makefile.

CFLAGS=-g -Wall
CC=gcc
AR=ar
RANLIB=ranlib
LIBS=-L./ -lcgic -L/usr/local/lib -L../../lib/libdfui -ldfui -laura
INCS=-I/usr/local/include -I../../lib/libdfui
WWW=/usr/local/www

.ifdef DEBUG
CFLAGS+=-DDEBUG
.endif

WARNS=	-Werror -W -Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith	-Wno-uninitialized -Wreturn-type -Wcast-qual \
	-Wswitch -Wshadow -Wcast-align -Wchar-subscripts \
	-Winline -Wnested-externs -Wredundant-decls

all: dfuife.cgi libcgic.a cgictest.cgi capture example.cgi

# XXX link to dependencies here
in_cvs: all

libcgic.a: cgic.o cgic.h
	rm -f libcgic.a
	$(AR) rc libcgic.a cgic.o
	$(RANLIB) libcgic.a

cgictest.cgi: cgictest.o libcgic.a
	$(CC) $(CFLAGS) cgictest.c -o cgictest.cgi $(LIBS)

capture: capture.o libcgic.a
	$(CC) $(CFLAGS) capture.o -o capture $(LIBS)

example.cgi: example.o
	$(CC) $(CFLAGS) example.o -o example.cgi $(LIBS)

example.o: example.c
	$(CC) $(CFLAGS) $(WARNS) $(INCS) -c  example.c -o example.o

dfuife_cgi.o: dfuife_cgi.c
	$(CC) $(CFLAGS) $(WARNS) $(INCS) -c dfuife_cgi.c -o dfuife_cgi.o

dfuife.cgi: dfuife_cgi.o libcgic.a
	$(CC) $(CFLAGS) dfuife_cgi.o -o dfuife.cgi $(LIBS)

clean:
	rm -f *.o *.a *.cgi capture
