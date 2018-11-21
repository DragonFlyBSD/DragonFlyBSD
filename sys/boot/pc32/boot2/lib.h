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
 */

/*
 * $FreeBSD: src/sys/boot/i386/boot2/lib.h,v 1.2 1999/08/28 00:40:02 peter Exp $
 * $DragonFly: src/sys/boot/pc32/boot2/lib.h,v 1.4 2004/06/27 08:00:46 dillon Exp $
 */

int sio_init(int) __attribute__((regparm (3))); /* returns non-zero if init failed */
int sio_flush(void);
void sio_putc(int) __attribute__((regparm (3)));
int sio_getc(void);
int sio_ischar(void);
