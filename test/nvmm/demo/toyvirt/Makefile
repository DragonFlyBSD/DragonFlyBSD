all:
	cc -Wall -Wextra -g -std=c99 \
		-o toyvirt \
		main.c elf.c toydev.c \
		-lnvmm -lpthread

clean:
	rm -f toyvirt
