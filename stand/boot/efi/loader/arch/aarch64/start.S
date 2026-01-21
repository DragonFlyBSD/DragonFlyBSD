	.text
	.align 4

	.globl _start
_start:
	stp x0, x1, [sp, #-16]!
	adrp x0, ImageBase
	add x0, x0, :lo12:ImageBase
	adrp x1, _DYNAMIC
	add x1, x1, :lo12:_DYNAMIC
	bl self_reloc
	ldp x0, x1, [sp], #16
	bl efi_main
	ret

	.data
	.section .reloc, "a"
	.long 0
	.long 10
	.word 0
