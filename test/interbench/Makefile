CC=gcc
CFLAGS=-W -Wall -g -O2 -s -pipe
LDFLAGS=-lrt -lm

interbench: interbench.o hackbench.o
interbench.o: interbench.c
hackbench.o: hackbench.c

clean:
	rm -f *.o interbench interbench.read interbench.write interbench.loops_per_ms *~
