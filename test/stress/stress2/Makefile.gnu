#MAKE=gmake

# Gnu Makefile by "Brad Knotwell" <bknotwell@yahoo.com>

LIBOBJS=$(subst .c,.o,$(wildcard lib/*.c))
TESTDIRS=run swap mkdir creat thr1 syscall rw sysctl tcp udp
EXES=$(foreach dir,$(TESTDIRS),testcases/$(dir)/$(dir).test)
OBJS=$(subst .test,.o,$(EXES))
SRCS=$(subst .o,.c,$(OBJS))
LIBS=./lib/libstress.a
CFLAGS=-g -Wall -I./include

all: $(EXES)

lib/libstress.a: lib/libstress.a($(LIBOBJS))
	ranlib lib/libstress.a

lib/libstress.a(*.o): $(LIBOBJS)

$(OBJS): %.o: %.c

$(EXES): %.test: %.o lib/libstress.a

%.test: %.o
	$(CC) $(CFLAGS) $(LIBS) $< -o $@

clean:
	rm -fr $(LIBOBJS) lib/libstress.a $(EXES) $(OBJS)
