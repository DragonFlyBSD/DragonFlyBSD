all: /tmp/toyvirt

/tmp/toyvirt: main.c elf.c toydev.c
	cc -g -std=c99 -Wall -Wextra -o ${.TARGET} ${.ALLSRC} -lnvmm

clean:
	rm -f /tmp/toyvirt
