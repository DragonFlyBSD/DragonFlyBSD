/*
 * DEFS.H
 *
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/lib/libcaps/defs.h,v 1.5 2004/07/04 22:44:26 eirikn Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/stdint.h>
#include <sys/upcall.h>
#include <sys/malloc.h>
#include "thread.h"
#include <sys/thread.h>
#include <sys/msgport.h>
#include <sys/errno.h>
#include "globaldata.h"
#include "sysport.h"
#include <machine/cpufunc.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <sys/caps.h>

#include <sys/time.h>
#include <sys/event.h>
#if 0
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include <fcntl.h>
#include <stdio.h>	/* temporary debugging */
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>	/* temporary debugging */
#include <signal.h>
#include <assert.h>

#include "caps_struct.h"

#ifdef CAPS_DEBUG
#define DBPRINTF(x)	printf x
#else
#define DBPRINTF(x)
#endif

extern struct thread main_td;
