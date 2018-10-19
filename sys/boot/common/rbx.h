/*-
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
 * $FreeBSD$
 */
#ifndef _RBX_H_
#define	_RBX_H_

/* mistakenly uses /usr/include/sys/reboot.h instead of /usr/src/sys/sys/reboot.h
#include <sys/reboot.h>
*/
#include "../../sys/reboot.h"

#define NOPT		15
#define OPT_SET(opt)	(opt)
#define OPT_CHECK(opt)	(opts & opt)
#define _LOG2(_x) 	(31 - __builtin_clz(_x))

static const char optstr[NOPT] = "asrdcvhCgmpqnDM";
	/* Also 'P':ProbeKeyboard, 'S':SetSerialSpeed */
	
/* these _LOG2 values are be calculated at compile time */
static const unsigned char flags[NOPT] = {
	_LOG2(RB_ASKNAME),
	_LOG2(RB_SINGLE),
	_LOG2(RB_DFLTROOT),
	_LOG2(RB_KDB),
	_LOG2(RB_CONFIG),
	_LOG2(RB_VERBOSE),
	_LOG2(RB_SERIAL),
	_LOG2(RB_CDROM),
	_LOG2(RB_GDB),
	_LOG2(RB_MUTE),
	_LOG2(RB_PAUSE),
	_LOG2(RB_QUIET),
	_LOG2(RB_NOINTR),
	_LOG2(RB_DUAL),
	_LOG2(RB_MULTIPLE),
	// _LOG2(RB_VIDEO),
};
/* pass: -a -s -r -d -c -v -h -C -g -m -p -q -n -D -M */
#define RB_MASK						  \
(							  \
	OPT_SET(RB_ASKNAME)	| OPT_SET(RB_SINGLE)	| \
	OPT_SET(RB_DFLTROOT)	| OPT_SET(RB_KDB )	| \
	OPT_SET(RB_CONFIG)	| OPT_SET(RB_VERBOSE)	| \
	OPT_SET(RB_SERIAL)	| OPT_SET(RB_CDROM)	| \
	OPT_SET(RB_GDB )	| OPT_SET(RB_MUTE)	| \
	OPT_SET(RB_PAUSE)	| OPT_SET(RB_QUIET)	| \
	OPT_SET(RB_NOINTR)	| OPT_SET(RB_DUAL)	| \
	OPT_SET(RB_MULTIPLE)				  \
)

extern uint32_t opts;

#endif	/* !_RBX_H_ */
