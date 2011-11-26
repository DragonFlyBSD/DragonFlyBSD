/*
 * CAPS_STRUCT.H
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
 * $DragonFly: src/lib/libcaps/caps_struct.h,v 1.1 2004/03/07 23:36:44 dillon Exp $
 */
#include <sys/endian.h>

struct caps_label;
struct caps_struct;
struct caps_msgbuf;

typedef const struct caps_label *caps_label_t;
typedef const struct caps_struct *caps_struct_t;
typedef struct caps_msgbuf *caps_msgbuf_t;

typedef u_int32_t caps_fid_t;

struct caps_label {
    int	offset;
    int type;
    int size;			/* element size for array[1] */
    caps_fid_t fid;
    int nary;			/* can be 0 or 1 to indicate 1 element */
    caps_struct_t csinfo;	/* if embedded structure */
};

struct caps_struct {
    char *name;
    const caps_label_t labels;
};

struct caps_msgbuf {
    u_int8_t *base;
    int index;
    int bufsize;
    int error;
};

#define CAPS_OPF_PTR		0x0100

#define CAPS_OP_INT_T		1
#define CAPS_OP_UINT_T		2
#define CAPS_OP_STRBUF_T	3
#define CAPS_OP_STRPTR_T	(3|CAPS_OPF_PTR)
#define CAPS_OP_OPAQUE_T	4

/*
 * Note: signed/unsignedness may not reflect the actual type.  Instead it
 * reflects our casting policy if the client and server are using different
 * integer sizes for any given field.
 */
#define CAPS_IN_INT_T		CAPS_OP_INT_T, sizeof(int)
#define CAPS_IN_INT8_T		CAPS_OP_INT_T, sizeof(int8_t)
#define CAPS_IN_INT16_T		CAPS_OP_INT_T, sizeof(int16_t)
#define CAPS_IN_INT32_T		CAPS_OP_INT_T, sizeof(int32_t)
#define CAPS_IN_LONG_T		CAPS_OP_INT_T, sizeof(long)
#define CAPS_IN_INT64_T		CAPS_OP_INT_T, sizeof(int64_t)

#define CAPS_IN_UINT_T		CAPS_OP_INT_T, sizeof(u_int)
#define CAPS_IN_UINT8_T		CAPS_OP_INT_T, sizeof(u_int8_t)
#define CAPS_IN_UINT16_T	CAPS_OP_INT_T, sizeof(u_int16_t)
#define CAPS_IN_UINT32_T	CAPS_OP_INT_T, sizeof(u_int32_t)
#define CAPS_IN_ULONG_T		CAPS_OP_INT_T, sizeof(u_long)
#define CAPS_IN_UINT64_T	CAPS_OP_INT_T, sizeof(u_int64_t)

#define CAPS_IN_STRPTR_T	CAPS_OP_STRPTR_T, 0
#define CAPS_IN_STRBUF_T(n)	CAPS_OP_STRBUF_T, (n)
#define CAPS_IN_UID_T		CAPS_OP_INT_T, sizeof(uid_t)
#define CAPS_IN_GID_T		CAPS_OP_INT_T, sizeof(gid_t)
#define CAPS_IN_TIME_T		CAPS_OP_UINT_T, sizeof(time_t)
#define CAPS_IN_OFF_T		CAPS_OP_INT_T, sizeof(off_t)
#define CAPS_IN_SIZE_T		CAPS_OP_UINT_T, sizeof(size_t)
#define CAPS_IN_SSIZE_T		CAPS_OP_INT_T, sizeof(ssize_t)

static __inline
u_int8_t
caps_msgbuf_getc(caps_msgbuf_t msg)
{
    u_int8_t c = 0;

    if (msg->index < msg->bufsize)
	c = msg->base[msg->index];
    ++msg->index;
    return(c);		/* always bumped */
}

static __inline
void
caps_msgbuf_putc(caps_msgbuf_t msg, u_int8_t c)
{
    if (msg->index < msg->bufsize) {
	msg->base[msg->index] = c;
    }
    ++msg->index;	/* always bumped */
}

extern const struct caps_struct caps_passwd_struct;

int caps_encode(void *buf, int bytes, void *data, caps_struct_t cs);
int caps_decode(const void *buf, int bytes, void *data, caps_struct_t cs, int *error);
void caps_struct_free_pointers(void *data, caps_struct_t cs);
void caps_array_free_pointers(void *data, caps_label_t label);

void caps_init_msgbuf(caps_msgbuf_t msg, void *data, int size);
void caps_msgbuf_error(caps_msgbuf_t msg, int eno, int undo);
u_int8_t caps_msgbuf_getclass(caps_msgbuf_t msg, u_int8_t **pptr, int *plen);
void caps_msgbuf_printf(caps_msgbuf_t msg, const char *ctl, ...);

void caps_msg_encode_structure(caps_msgbuf_t msg, void *data, caps_struct_t cs);
void caps_msg_encode_array(caps_msgbuf_t msg, void *data, caps_label_t label);
void caps_msg_encode_data(caps_msgbuf_t msg, void *data, int type, int size);
void caps_msg_decode_structure(caps_msgbuf_t msg, void *data, caps_struct_t cs);
void caps_msg_decode_array(caps_msgbuf_t msg, void *data, int nary, caps_label_t label);
void caps_msg_decode_data(char *ptr, int len, void *data, int type, int size);

