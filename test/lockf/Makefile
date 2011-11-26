# $DragonFly: src/test/lockf/Makefile,v 1.1 2004/05/11 08:03:57 joerg Exp $

TARGETS=/tmp/lockf

all:	$(TARGETS)

/tmp/lockf: lockf.c
	$(CC) $(CFLAGS) lockf.c -o ${.TARGET}

clean:
	rm -f $(TARGETS)
