/*	$FreeBSD: src/sys/netinet6/esp_rijndael.h,v 1.1.2.1 2001/07/03 11:01:50 ume Exp $	*/
/*	$DragonFly: src/sys/netinet6/esp_rijndael.h,v 1.4 2006/05/20 02:42:12 dillon Exp $	*/
/*	$KAME: esp_rijndael.h,v 1.1 2000/09/20 18:15:22 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NETINET6_ESP_RIJNDAEL_H_
#define _NETINET6_ESP_RIJNDAEL_H_

#ifdef _KERNEL

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

struct secasvar;
struct esp_algorithm;

size_t esp_rijndael_schedlen (const struct esp_algorithm *);
int esp_rijndael_schedule (const struct esp_algorithm *,
	struct secasvar *);
int esp_rijndael_blockdecrypt (const struct esp_algorithm *,
	struct secasvar *, u_int8_t *, u_int8_t *);
int esp_rijndael_blockencrypt (const struct esp_algorithm *,
	struct secasvar *, u_int8_t *, u_int8_t *);

#endif

#endif
