TARGETS= hackbench.o interbench.o interbench

CFLAGS= -W -Wall -g -O2 -s -pipe -Werror
LDFLAGS= -lpthread -lrt -lm

all:	$(TARGETS)

hackbench.o: hackbench.c
	$(CC) $(CFLAGS) hackbench.c -c

interbench.o: interbench.c
	$(CC) $(CFLAGS) interbench.c -c

interbench: hackbench.o interbench.o
	$(CC) $(CFLAGS) $(LDFLAGS) hackbench.o interbench.o -o interbench

clean:
	rm -f $(TARGETS) interbench.read interbench.write interbench.loops_per_ms *~
