/*
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 *
 * $FreeBSD: src/sys/boot/i386/btx/lib/btxcsu.s,v 1.3 1999/08/28 00:40:07 peter Exp $
 * $DragonFly: src/sys/boot/i386/btx/lib/Attic/btxcsu.S,v 1.6 2004/07/27 19:37:19 dillon Exp $
 */

/*
 * BTX C startup code (ELF).  Executed from ../btx/btx.S
 */

#include "../../bootasm.h"

		/*
		 * Globals.
		 */
		.global _start
		.extern _edata, _end

		/*
		 * Client entry point.
		 */
_start: 
		pushl %eax			# Save base address
		movl $_edata,%edi		# Clear BSS
		movl $_end,%ecx
		subl %edi,%ecx
		subl %eax,%eax
		rep
		stosb
		popl __base			# Set base address
		movl %esp,%eax			# Get base of arguments
		addl $USR_ARGSPACE-USR_ARGOFFSET,%eax
		movl %eax,__args
		call main			# Invoke client main()
		call exit			# Invoke client exit()

		/*
		 * Data
		 */
		.comm __base,4			# Client base address
		.comm __args,4			# Client arguments
