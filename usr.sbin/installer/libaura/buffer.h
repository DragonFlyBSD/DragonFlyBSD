/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Chris Pressey <cpressey@catseye.mine.nu>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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
 */

/*
 * buffer.h
 * $Id: buffer.h,v 1.2 2005/02/06 06:57:30 cpressey Exp $
 */

#ifndef __AURA_BUFFER_H_
#define	__AURA_BUFFER_H_

#include <stdlib.h>

struct aura_buffer {
	char	*buf;
	size_t	 len;
	size_t	 size;
	size_t	 pos;
};

struct aura_buffer	*aura_buffer_new(size_t);
void			 aura_buffer_free(struct aura_buffer *);
char			*aura_buffer_buf(struct aura_buffer *);
size_t			 aura_buffer_len(struct aura_buffer *);
size_t			 aura_buffer_size(struct aura_buffer *);

void			 aura_buffer_ensure_size(struct aura_buffer *, size_t);
void			 aura_buffer_set(struct aura_buffer *, const char *, size_t);
void			 aura_buffer_append(struct aura_buffer *, const char *, size_t);

void			 aura_buffer_cpy(struct aura_buffer *, const char *);
void			 aura_buffer_cat(struct aura_buffer *, const char *);
int			 aura_buffer_cat_file(struct aura_buffer *, const char *, ...)
			     __printflike(2, 3);
int			 aura_buffer_cat_pipe(struct aura_buffer *, const char *, ...)
			     __printflike(2, 3);

int			 aura_buffer_seek(struct aura_buffer *, size_t);
size_t			 aura_buffer_tell(struct aura_buffer *);
int			 aura_buffer_eof(struct aura_buffer *);
char			 aura_buffer_peek_char(struct aura_buffer *);
char			 aura_buffer_scan_char(struct aura_buffer *);
int			 aura_buffer_compare(struct aura_buffer *, const char *);
int			 aura_buffer_expect(struct aura_buffer *, const char *);

void			 aura_buffer_push(struct aura_buffer *, const void *, size_t);
int			 aura_buffer_pop(struct aura_buffer *, void *, size_t);

#endif /* !__AURA_BUFFER_H_ */
