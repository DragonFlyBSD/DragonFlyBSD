all: /tmp/calc-vm

/tmp/calc-vm: calc-vm.c
	cc -g -std=c99 -Wall -Wextra -o ${.TARGET} ${.ALLSRC} -lnvmm

clean:
	rm -f /tmp/calc-vm
