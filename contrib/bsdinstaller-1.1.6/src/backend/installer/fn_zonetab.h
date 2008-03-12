/*
 * Copyright (c)2004 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of the DragonFly Project nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * fn_zonetab.h
 * $Id: fn_zonetab.h,v 1.1 2004/07/15 11:01:21 dodell Exp $
 */

#ifndef _FN_ZONETAB_H_
#define _FN_ZONETAB_H_

#define ZONETAB_FILE "/usr/share/zoneinfo/zone.tab"

#define ZT_CC			0
#define ZT_COORDS		1
#define ZT_REGION		2
#define ZT_LOCALE		3
#define ZT_LOCALE_ENDL	4
#define ZT_COMMENTS		5
#define ZT_NOTOK		6

struct zonetab {
	char *zt_cc;
	char *zt_coords;
	char *zt_region;
	char *zt_locale;
	char *zt_comments;
	struct zonetab *next;
};	

struct zt_parse {
	char *buf;
	char tok[1255];
	int state;
};

char	*zt_readfile(char *, int, FILE *);
void	 zt_get_token(struct zt_parse *);
void	 zt_list_free(struct zonetab *);
void	 zt_parse(struct zonetab *);

#endif
