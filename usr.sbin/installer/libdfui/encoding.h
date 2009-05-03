/*
 * Copyright (c)2004 Cat's Eye Technologies.  All rights reserved.
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
 *   Neither the name of Cat's Eye Technologies nor the names of its
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
 * encoding.h
 * $Id: encoding.h,v 1.7 2005/02/07 06:40:00 cpressey Exp $
 */

#include <libaura/buffer.h>

#include "dfui.h"

/*
 * ENCODER
 */

void			 dfui_encode_string(struct aura_buffer *, const char *);
void			 dfui_encode_int(struct aura_buffer *, int);
void			 dfui_encode_bool(struct aura_buffer *, int );

void			 dfui_encode_info(struct aura_buffer *, struct dfui_info *);

void			 dfui_encode_datasets(struct aura_buffer *, struct dfui_dataset *);
void			 dfui_encode_dataset(struct aura_buffer *, struct dfui_dataset *);

void			 dfui_encode_celldatas(struct aura_buffer *, struct dfui_celldata *);
void			 dfui_encode_celldata(struct aura_buffer *, struct dfui_celldata *);

void			 dfui_encode_properties(struct aura_buffer *, struct dfui_property *);
void			 dfui_encode_property(struct aura_buffer *, struct dfui_property *);

void			 dfui_encode_fields(struct aura_buffer *, struct dfui_field *);
void			 dfui_encode_field(struct aura_buffer *, struct dfui_field *);

void			 dfui_encode_options(struct aura_buffer *, struct dfui_option *);
void			 dfui_encode_option(struct aura_buffer *, struct dfui_option *);

void			 dfui_encode_actions(struct aura_buffer *, struct dfui_action *);
void			 dfui_encode_action(struct aura_buffer *, struct dfui_action *);

void			 dfui_encode_form(struct aura_buffer *, struct dfui_form *);

void			 dfui_encode_response(struct aura_buffer *, struct dfui_response *);

void			 dfui_encode_progress(struct aura_buffer *, struct dfui_progress *);

/*
 * DECODER
 */

char			*dfui_decode_string(struct aura_buffer *);
int			 dfui_decode_int(struct aura_buffer *);
int			 dfui_decode_bool(struct aura_buffer *);

struct dfui_info	*dfui_decode_info(struct aura_buffer *);

struct dfui_celldata 	*dfui_decode_celldata(struct aura_buffer *);
struct dfui_celldata	*dfui_decode_celldatas(struct aura_buffer *);

struct dfui_property 	*dfui_decode_property(struct aura_buffer *);
struct dfui_property	*dfui_decode_properties(struct aura_buffer *);

struct dfui_dataset 	*dfui_decode_dataset(struct aura_buffer *);
struct dfui_dataset 	*dfui_decode_datasets(struct aura_buffer *);

struct dfui_field	*dfui_decode_field(struct aura_buffer *);
struct dfui_field	*dfui_decode_fields(struct aura_buffer *);

struct dfui_option	*dfui_decode_option(struct aura_buffer *);
struct dfui_option	*dfui_decode_options(struct aura_buffer *);

struct dfui_action	*dfui_decode_action(struct aura_buffer *);
struct dfui_action 	*dfui_decode_actions(struct aura_buffer *);

struct dfui_form 	*dfui_decode_form(struct aura_buffer *);

struct dfui_response	*dfui_decode_response(struct aura_buffer *);

struct dfui_progress	*dfui_decode_progress(struct aura_buffer *);
