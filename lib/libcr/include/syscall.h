/*
 * Copyright (c) 2004 Eirik Nygaard.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/lib/libcr/include/Attic/syscall.h,v 1.1 2004/08/12 19:59:30 eirikn Exp $
 */

/* XXX: eirik */
#define _MMAP_DECLARED
#define _LSEEK_DECLARED
#define _TRUNCATE_DECLARED
#define _FTRUNCATE_DECLARED

#define __syscall_args nosys_args

#include <sys/types.h>
#include <sys/msgport.h>
#include <sys/syscall.h>
#include <sys/sysproto.h>

#include <stdlib.h>
#include <errno.h>

#define INITMSGSYSCALL(name, flags) do {				\
	bzero(&__CONCAT(name, msg), sizeof(__CONCAT(name, msg)));	\
	(__CONCAT(name, msg)).usrmsg.umsg.ms_cmd.cm_op =		\
	        (__CONCAT(SYS_,name));					\
	(__CONCAT(name, msg)).usrmsg.umsg.ms_flags = (flags);		\
} while (0)

#define DOMSGSYSCALL(name) do {						\
	error = sendsys(NULL, &__CONCAT(name,msg).usrmsg,		\
	        sizeof(__CONCAT(name,msg)));				\
} while (0)

#define FINISHMSGSYSCALL(name, error) do {				\
	if (__CONCAT(name,msg).usrmsg.umsg.ms_error == EASYNC) {	\
		for (;;) {						\
			int rmsg;					\
			rmsg = waitsys(NULL, (void *)error, 0);		\
			if (error == rmsg)				\
				break;					\
			else if (rmsg != 0) {				\
				errno = rmsg;				\
				return(-1);				\
			}						\
		}							\
	}								\
	else if (__CONCAT(name,msg).usrmsg.umsg.ms_error) {		\
		errno = __CONCAT(name,msg).usrmsg.umsg.ms_error;	\
		return(-1);						\
	}								\
	else								\
		return(__CONCAT(name,msg).usrmsg.umsg.u.ms_result);	\
} while(0)

static __inline int
sendsys(void *port, void *msg, int msgsize)
{
	int error;

	__asm __volatile("int $0x81" : "=a"(error), "=c"(msg), "=d"(msgsize) :
	                 "0"(port), "1"(msg), "2"(msgsize) : "memory");
	return(error);
}

static __inline int
waitsys(void *port, void *msg, int msgsize)
{
	int error;

	__asm __volatile("int $0x82" : "=a"(error), "=c"(msg),
			"=d"(msgsize) : "0"(port), "1"(msg), "2"(msgsize) :
			"memory");
	return(error);
}
