/*
 * $FreeBSD: src/sys/boot/sparc64/boot1/_start.S,v 1.1 2002/03/28 02:41:52 obrien Exp $
 * $DragonFly: src/sys/boot/sparc64/boot1/_start.S,v 1.1 2003/11/10 06:08:40 dillon Exp $
 */

	.text
	.globl	_start
_start:
	call	ofw_init
	 nop
	sir
