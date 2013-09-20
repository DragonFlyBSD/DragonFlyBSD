DESTDIR=	/usr/local/bin
TARGETS=	$(DESTDIR)/vmm_test

#CFLAGS= 	-O -g -I/usr/src/sys

all:	$(TARGETS)

$(DESTDIR)/vmm_test: vmm_test.c
	$(CC) $(CFLAGS) vmm_test.c -o ${.TARGET}

clean:
	rm -f $(TARGETS)
