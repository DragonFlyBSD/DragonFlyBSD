/*-
 * Copyright (c) 2001
 *     Jeff Wheelhouse (jdw@wwwi.com)
 *
 * This code was originally developed by Jeff Wheelhouse (jdw@wwwi.com).
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistribution of source code must retail the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY JEFF WHEELHOUSE ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN 
 * NO EVENT SHALL JEFF WHEELHOUSE BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: clog.h,v 1.2 2001/10/02 04:43:52 jdw Exp $
 * $DragonFly: src/usr.sbin/clog/clog.h,v 1.1 2004/10/30 20:26:46 dillon Exp $
 */


#ifndef _CLOG_H_
#define _CLOG_H_

/*
 *  This magic constant is used to identify a valid circular log file.
 *  syslogd will ignore any circular log file that doesn't have this constant.
 */

const char MAGIC_CONST[4] = "CLOG";


struct clog_footer {
	uint32_t cf_magic;
	uint32_t cf_wrap;
	uint32_t cf_next;
	uint32_t cf_max;
	uint32_t cf_lock;
};


#endif  /* _CLOG_H_ */
	

