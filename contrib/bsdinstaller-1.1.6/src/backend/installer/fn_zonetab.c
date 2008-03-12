/*
 * Copyright (c)2004 The DragonFly Project. All rights reserved.
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
 * fn_parsers.c
 * Installer Function : Parse /usr/share/zoneinfo/zone.tab.
 * $Id: fn_zonetab.c,v 1.3 2004/07/16 21:04:20 adonijah Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fn_zonetab.h"

char *
zt_readfile(char *buf, int len, FILE *f) {
	int i;
	char *cs;
	
	cs = buf;
	while (--len > 0 && (i = getc(f)) != EOF) 
		*(cs++) = i;
	*cs = '\0';
	return((i == EOF && buf == cs) ? NULL : buf);
}

void
zt_get_token(struct zt_parse *zp) {
	int i = 0;

	while (*(zp->buf) != '\0' && *(zp->buf) != '\n' && *(zp->buf) != '\t') {
		switch (*(zp->buf)) {
		case '#':
			while (*(zp->buf) != '\n')
				zp->buf++;
			zp->buf++;
			break;
		default:
			while (*(zp->buf) != '\n' && *(zp->buf) != '\t' && 
			    *(zp->buf) != '\0') {
				if (*(zp->buf) == '/' && zp->state == ZT_REGION) {
					zp->tok[i] = '\0';
					return;
				}
				zp->tok[i++] = *(zp->buf++);
			}
			zp->tok[i] = '\0';
			if (zp->state == ZT_LOCALE && *(zp->buf) == '\n')
				zp->state = ZT_LOCALE_ENDL;
			else if (*(zp->buf) == '\0')
				zp->state = ZT_NOTOK;
			break;
		}
	}
}

void
zt_parse(struct zonetab *head) {
	FILE *zone_tab;
	struct stat sb;
	struct zonetab *c;
	struct zt_parse zp;
	char *file;
	int i;

	zone_tab = fopen(ZONETAB_FILE, "r");
	stat(ZONETAB_FILE, &sb);
	file = malloc(sb.st_size + 1);
	file = zt_readfile(file, sb.st_size, zone_tab);
	zp.buf = file;
	
	zp.state = ZT_CC;
	i = 0;
	c = head;
	
	do {
		zt_get_token(&zp);
		switch (zp.state) {
		case ZT_CC:
			c->zt_cc = strdup(zp.tok);
			zp.state = ZT_COORDS;
			break;
		case ZT_COORDS:
			c->zt_coords = strdup(zp.tok);
			zp.state = ZT_REGION;
			break;
		case ZT_REGION:
			c->zt_region = strdup(zp.tok);
			zp.state = ZT_LOCALE;
			break;
		case ZT_LOCALE:
			c->zt_locale = strdup(zp.tok);
			zp.state = ZT_COMMENTS;
			break;
		case ZT_LOCALE_ENDL:
			c->zt_locale = strdup(zp.tok);
			c->zt_comments = NULL;
			c->next = malloc(sizeof(struct zonetab));
			c = c->next;
			zp.state = ZT_CC;
			break;
		case ZT_COMMENTS:
			c->zt_comments = strdup(zp.tok);
			c->next = malloc(sizeof(struct zonetab));
			c = c->next;
			zp.state = ZT_CC;
			break;
		case ZT_NOTOK:
			goto done;
		}
		zp.buf++;
	} while (1);

done:
	c->next = NULL;
	free(file);
}

void
fn_zt_list_free(struct zonetab *zt) {
	struct zonetab *p;
	for (p = zt; p->next != NULL; p = p->next) {
		free(p->zt_cc);
		free(p->zt_coords);
		free(p->zt_region);
		free(p->zt_locale);
		if (p->zt_comments != NULL)
			free(p->zt_comments);
	}
}

struct zonetab *
fn_zt_list(void) {
	struct zonetab **zt;
	
	*zt = malloc(sizeof(struct zonetab));
	zt_parse(*zt);
	return(*zt);
}
