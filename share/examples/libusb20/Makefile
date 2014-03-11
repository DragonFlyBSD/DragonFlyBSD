# $FreeBSD: head/share/examples/libusb20/Makefile 257779 2013-11-07 07:22:51Z hselasky $
TARGETS=	bulk control
CFLAGS+=	-Wall

all: $(TARGETS)

bulk: bulk.o util.o
	$(CC) $(CFLAGS) -o bulk bulk.o util.o -lusb

control: control.o util.o
	$(CC) $(CFLAGS) -o control control.o util.o -lusb

clean:
	rm -f $(TARGETS) *.o *~
