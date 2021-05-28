all:
	cc -Wall -Wextra -g -std=c99 \
		-mno-red-zone -ffreestanding \
		-DKERNEL -D__x86_64__ \
		-c locore.S trap.S main.c console.c
	ld -o smallkern \
		-X -z max-page-size=0x100000 -Ttext 0x100000 \
		-T smallkern.ldscript *.o

clean:
	rm -f smallkern *.o
