TARGETS= dschedstats

CFLAGS= -O -g

all:	$(TARGETS)

dschedstats: dsched_stats.c
	$(CC) $(CFLAGS) dsched_stats.c -o ${.TARGET}

clean:
	rm -f $(TARGETS)
