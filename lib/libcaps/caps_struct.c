/*
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
 * $DragonFly: src/lib/libcaps/caps_struct.c,v 1.1 2004/03/07 23:36:44 dillon Exp $
 */

#include "defs.h"

static
int32_t
parsehex32(const u_int8_t *ptr, int len)
{
    int neg = 0;
    int32_t v = 0;
    u_int8_t c;

    if (len && ptr[0] == '-') {
	neg = 1;
	--len;
	++ptr;
    }
    while (len) {
	v = v << 4;
	c = *ptr;
	if (c >= '0' && c <= '9')
	    v |= c - '0';
	if (c >= 'a' && c <= 'f')
	    v |= c - ('a' - 10);
	--len;
	++ptr;
    }
    if (neg)
	v = -v;
    return(v);
}

static
int64_t
parsehex64(const u_int8_t *ptr, int len)
{
    int neg = 0;
    int64_t v;

    if (len && ptr[0] == '-') {
	neg = 1;
	--len;
	++ptr;
    }

    if (len > 4) {
	v = parsehex32(ptr + len - 4, 4) |
		((int64_t)parsehex32(ptr, len - 4) << 32);
    } else {
	v = (int64_t)parsehex32(ptr, len);
    }
    if (neg)
	v = -v;
    return(v);
}

static caps_label_t
caps_find_label(caps_struct_t cs, caps_fid_t fid, const struct caps_label **pcache)
{
    caps_label_t label;

    if ((label = *pcache) != NULL) {
	if (label->fid == fid)
	    return(label);
	++label;
	if (label->fid == fid && label->offset >= 0) {
	    *pcache = label;
	    return(label);
	}
	--label;
	if (label != cs->labels) {
	    --label;
	    if (label->fid == fid && label->offset >= 0) {
		*pcache = label;
		return(label);
	    }
	}
    }
    for (label = cs->labels; label->offset >= 0; ++label) {
	if (label->fid == fid) {
	    *pcache = label;
	    return(label);
	}
    }
    return(NULL);
}

/*
 * Generic structural encoder.  The number of bytes that would be stored in
 * buf if it were infinitely-sized is returned.
 */
int
caps_encode(void *buf, int bytes, void *data, caps_struct_t cs)
{
    struct caps_msgbuf msgbuf;

    caps_init_msgbuf(&msgbuf, buf, bytes);
    caps_msg_encode_structure(&msgbuf, data, cs);
    return(msgbuf.index);
}

/*
 * Encode a structure into a message using the supplied label data.  The
 * message's index is updated to reflect the actual number of bytes that
 * would be consumed, even if the message buffer would overflow (but we don't
 * overflow the buffer, obviously).
 */
void
caps_msg_encode_structure(caps_msgbuf_t msg, void *data, caps_struct_t cs)
{
    caps_label_t label;
    void *ptr;

    caps_msgbuf_printf(msg, "S%s{", cs->name);
    for (label = cs->labels; label->offset >= 0; ++label) {
	if (label != cs->labels)
	    caps_msgbuf_putc(msg, ',');
	caps_msgbuf_printf(msg, "F%x", label->fid);
	ptr = (char *)data + label->offset;
	if (label->nary > 1)
	    caps_msg_encode_array(msg, ptr, label);
	else if (label->csinfo)
	    caps_msg_encode_structure(msg, ptr, label->csinfo);
	else
	    caps_msg_encode_data(msg, ptr, label->type, label->size);
    }
    caps_msgbuf_putc(msg, '}');
}

void
caps_msg_encode_array(caps_msgbuf_t msg, void *data, caps_label_t label)
{
    int i;
    void *ptr;

    caps_msgbuf_printf(msg, "A%x{", label->nary);
    for (i = 0; i < label->nary; ++i) {
	ptr = (char *)data + i *
		((label->type & CAPS_OPF_PTR) ? sizeof(void *) : label->size);
	if (label->csinfo)
	    caps_msg_encode_structure(msg, ptr, label->csinfo);
	else
	    caps_msg_encode_data(msg, ptr, label->type, label->size);
    }
    caps_msgbuf_putc(msg, '}');
}

void
caps_msg_encode_data(caps_msgbuf_t msg, void *data, int type, int size)
{
    int i;
    u_int8_t c;

    switch(type) {
    case CAPS_OP_INT_T:
	switch(size) {
	case 1:
	    if (*(int8_t *)data < 0)
		caps_msgbuf_printf(msg, "D-%x", -*(int8_t *)data);
	    else
		caps_msgbuf_printf(msg, "D%x", *(u_int8_t *)data);
	    break;
	case 2:
	    if (*(int16_t *)data < 0)
		caps_msgbuf_printf(msg, "D%x", -*(int16_t *)data);
	    else
		caps_msgbuf_printf(msg, "D%x", *(u_int16_t *)data);
	    break;
	case 4:
	    if (*(int32_t *)data < 0)
		caps_msgbuf_printf(msg, "D%x", -*(int32_t *)data);
	    else
		caps_msgbuf_printf(msg, "D%x", *(u_int32_t *)data);
	    break;
	case 8:
	    if (*(int64_t *)data < 0)
		caps_msgbuf_printf(msg, "D%llx", -*(int64_t *)data);
	    else
		caps_msgbuf_printf(msg, "D%llx", *(u_int64_t *)data);
	    break;
	default:
	    caps_msgbuf_putc(msg, 'D');
	    caps_msgbuf_putc(msg, '?');
	    break;
	}
	break;
    case CAPS_OP_UINT_T:
	switch(size) {
	case 1:
	    caps_msgbuf_printf(msg, "D%x", *(u_int8_t *)data);
	    break;
	case 2:
	    caps_msgbuf_printf(msg, "D%x", *(u_int16_t *)data);
	    break;
	case 4:
	    caps_msgbuf_printf(msg, "D%x", *(u_int32_t *)data);
	    break;
	case 8:
	    caps_msgbuf_printf(msg, "D%llx", *(u_int64_t *)data);
	    break;
	default:
	    caps_msgbuf_putc(msg, 'D');
	    caps_msgbuf_putc(msg, '?');
	    break;
	}
	break;
    case CAPS_OP_STRPTR_T:
	data = *(void **)data;
	if (data == NULL) {
	    caps_msgbuf_printf(msg, "D");	 /* represents NULL */
	    break;
	}
	if (size == 0)
	    size = 0x7FFFFFFF;
	/* fall through, size is the 'limit' */
    case CAPS_OP_STRBUF_T:
	caps_msgbuf_putc(msg, 'D');
	caps_msgbuf_putc(msg, '"');	/* string designator */
	for (i = 0; i < size && (c = ((u_int8_t *)data)[i]) != 0; ++i) {
	    if ((c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') ||
		c == '_' || c == '.' || c == '/' || c == '+' || c == '-'
	    ) {
		caps_msgbuf_putc(msg, c);
	    } else {
		caps_msgbuf_printf(msg, "%%%02x", (int)c);
	    }
	}
	caps_msgbuf_putc(msg, '"');
	break;
    case CAPS_OP_OPAQUE_T:
	caps_msgbuf_putc(msg, 'D');
	caps_msgbuf_putc(msg, '"');
	for (i = 0; i < size; ++i) {
	    c = ((u_int8_t *)data)[i];
	    if ((c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') ||
		c == '_' || c == '.' || c == '/' || c == '+' || c == '-'
	    ) {
		caps_msgbuf_putc(msg, c);
	    } else {
		caps_msgbuf_printf(msg, "%%%02x", (int)c);
	    }
	}
	caps_msgbuf_putc(msg, '"');
	break;
    default:
	caps_msgbuf_putc(msg, 'D');
	caps_msgbuf_putc(msg, '?');
	break;
    }
}

/*
 * Generic structural decoder.  The number of bytes that were decoded from
 * the buffer are returned, the structure is populated, and the error code
 * is set unconditionally as a side effect.
 */
int
caps_decode(const void *buf, int bytes, void *data, caps_struct_t cs, int *error)
{
    struct caps_msgbuf msgbuf;
    u_int8_t c;
    u_int8_t *ptr;
    int len;

    caps_init_msgbuf(&msgbuf, (void *)buf, bytes);
    while ((c = caps_msgbuf_getclass(&msgbuf, &ptr, &len)) != 0) {
	if (c == 'S' && len == strlen(cs->name) &&
	    strncmp(ptr, cs->name, len) == 0
	) {
	    caps_msg_decode_structure(&msgbuf, data, cs);
	    *error = msgbuf.error;
	    return(msgbuf.index);
	}
	/*
	 * Skip substructures.
	 */
	if (c == '{') {
	    caps_msgbuf_error(&msgbuf, 0, 1);
	    caps_msg_decode_structure(&msgbuf, NULL, NULL);
	}
    }
    if (msgbuf.error == 0)
	*error = ENOENT;
    *error = msgbuf.error;
    return(msgbuf.index);
}

/*
 * Decode a message buffer into a structure, return the number of bytes
 * chomped and set *error to 0 on success, or an error code on failure.
 * The 'Sname' has already been snarfed.  We are responsible for snarfing
 * the '{'.
 *
 * Note that the structural specification, cs, may be NULL, indicating and
 * unknown structure which causes us to skip the structure.
 */
void
caps_msg_decode_structure(caps_msgbuf_t msg, void *data, caps_struct_t cs)
{
    caps_label_t label;
    caps_label_t cache;
    u_int8_t *ptr;
    int len;
    char c;

    cache = NULL;

    /*
     * A structure must contain an open brace
     */
    if (caps_msgbuf_getc(msg) != '{') {
	caps_msgbuf_error(msg, EINVAL, 1);
	return;
    }

    /*
     * Parse data elements within the structure
     */
    do {
	/*
	 * Parse class info for the next element.  Note that the label
	 * specification may be NULL.
	 */
	label = NULL;
	while ((c = caps_msgbuf_getclass(msg, &ptr, &len)) != 0) {
	    switch(c) {
	    case 'F':
		label = caps_find_label(cs, parsehex32(ptr, len), &cache);
		continue;
	    case 'A':
		caps_msg_decode_array(msg, 
					(char *)data + label->offset,
					parsehex32(ptr, len), 
					label);
		continue;
	    case 'S':
		if (label && label->csinfo &&
		    strlen(label->csinfo->name) == len &&
		    strncmp(label->csinfo->name, ptr, len) == 0
		) {
		    caps_msg_decode_structure(msg, 
					(char *)data + label->offset, 
					label->csinfo);
		} else {
		    caps_msg_decode_structure(msg, NULL, NULL);
		}
		continue;
	    case 'D':
		if (label) {
		    caps_msg_decode_data(ptr, len,
					(char *)data + label->offset,
					label->type,
					label->size);
		}
		continue;
	    case '{':
		/*
		 * This case occurs when we previously hit an unknown class
		 * which has a sub-structure associated with it.  Parseskip
		 * the sub-structure.
		 */
		caps_msgbuf_error(msg, 0, 1);
		caps_msg_decode_structure(msg, NULL, NULL);
		continue;
	    case '}':
	    case ',':
		break;
	    default:
		/* unknown classes are ignored */
		continue;
	    }
	    break;
	}
    } while (c == ',');

    /*
     * A structure must end with a close brace
     */
    if (c != '}')
	caps_msgbuf_error(msg, EINVAL, 1);
}

void
caps_msg_decode_array(caps_msgbuf_t msg, void *data, int nary, caps_label_t label)
{
    int i;
    char c;
    int len;
    u_int8_t *ptr;

    c = 0;

    /*
     * An array must contain an open brace
     */
    if (caps_msgbuf_getc(msg) != '{') {
	caps_msgbuf_error(msg, EINVAL, 1);
	return;
    }
    for (i = 0; i < nary && (label == NULL || i < label->nary); ++i) {
	while ((c = caps_msgbuf_getclass(msg, &ptr, &len)) != 0) {
	    switch(c) {
	    case 'F':
		/* a field id for an array element is not expected */
		continue;
	    case 'A':
		/* nested arrays are not yet supported */
		continue;
	    case 'S':
		if (label && label->csinfo && 
		    strlen(label->csinfo->name) == len &&
		    strncmp(label->csinfo->name, ptr, len) == 0
		) {
		    caps_msg_decode_structure(msg, data, label->csinfo);
		} else {
		    caps_msg_decode_structure(msg, NULL, NULL);
		}
		continue;
	    case 'D':
		if (label) {
		    caps_msg_decode_data(ptr, len, data, 
					label->type, label->size);
		}
		continue;
	    case '{':
		/*
		 * This case occurs when we previously hit an unknown class
		 * which has a sub-structure associated with it.  Parseskip
		 * the sub-structure.
		 */
		caps_msgbuf_error(msg, 0, 1);
		caps_msg_decode_structure(msg, NULL, NULL);
		continue;
	    case '}':
	    case ',':
		break;
	    default:
		/* unknown classes are ignored */
		continue;
	    }
	    break;
	}

	if (label) { 
	    data = (char *)data +
		((label->type & CAPS_OPF_PTR) ? sizeof(void *) : label->size);
	}

	/*
	 * I really expected a comma here
	 */
	if (c != ',') {
	    caps_msgbuf_error(msg, EINVAL, 0);
	    break;
	}
    }

    /*
     * Our array was too small, exhaust any remaining elements
     */
    for (; i < nary; ++i) {
	while ((c = caps_msgbuf_getclass(msg, &ptr, &len)) != 0) {
	    switch(c) {
	    case 'S':
		caps_msg_decode_structure(msg, NULL, NULL);
		continue;
	    case 'D':
		/* data is embedded, no additional decoding needed to skip */
		continue;
	    case '{':
		caps_msgbuf_error(msg, 0, 1);
		caps_msg_decode_structure(msg, NULL, NULL);
		continue;
	    case '}':
	    case ',':
		break;
	    default:
		/* unknown classes are ignored */
		continue;
	    }
	    break;
	}
	if (c != ',') {
	    caps_msgbuf_error(msg, EINVAL, 0);
	    break;
	}
    }

    /*
     * Finish up.  Note degenerate case (c not loaded) if nary is 0
     */
    if (nary == 0)
	c = caps_msgbuf_getc(msg);
    if (c != '}')
	caps_msgbuf_error(msg, EINVAL, 1);
}

void
caps_msg_decode_data(char *ptr, int len, void *data, int type, int size)
{
    int i;
    int j;

    switch(type) {
    case CAPS_OP_INT_T:
    case CAPS_OP_UINT_T:
	switch(size) {
	case 1:
	    *(int8_t *)data = parsehex32(ptr, len);
	    break;
	case 2:
	    *(int16_t *)data = parsehex32(ptr, len);
	    break;
	case 4:
	    *(int32_t *)data = parsehex32(ptr, len);
	    break;
	case 8:
	    *(int64_t *)data = parsehex64(ptr, len);
	    break;
	default:
	    /* unknown data type */
	    break;
	}
	break;
    case CAPS_OP_STRPTR_T:
	/*
	 * Assume NULL if not a quoted string (the actual encoding for NULL
	 * is a completely empty 'D' specification).
	 */
	if (len < 2 || ptr[0] != '"' || ptr[len-1] != '"') {
	    *(void **)data = NULL;
	    break;
	}
	for (i = j = 0; i < len; ++j) {
	    if (ptr[i] == '%') {
		i += 3;
	    } else {
		++i;
	    }
	}
	if (size == 0 || size > j)
	    size = j + 1;
	*(void **)data = malloc(size);
	data = *(void **)data;
	assert(data != NULL);
	/* fall through */
    case CAPS_OP_STRBUF_T:
    case CAPS_OP_OPAQUE_T:
	/*
	 * Skip quotes
	 */
	if (len < 2 || ptr[0] != '"' || ptr[len-1] != '"') {
	    break;
	}
	++ptr;
	len -= 2;

	/*
	 * Parse the contents of the string
	 */
	for (i = j = 0; i < len && j < size; ++j) {
	    if (ptr[i] == '%') {
		if (i + 2 < len) {
		    ((char *)data)[j] = parsehex32(ptr + 1, 2);
		    i += 3;
		} else {
		    /* XXX error */
		    i = len;
		}
	    } else {
		((char *)data)[j] = ptr[i];
		++i;
	    }
	}
	if (type == CAPS_OP_OPAQUE_T) {
	    if (j < size)
		bzero((char *)data + j, size - j);
	} else {
	    if (j < size)
		((char *)data)[j] = 0;
	    else if (size)
		((char *)data)[size - 1] = 0;	/* XXX error */
	}
	break;
    default:
	break;
    }
}

/*
 * Free string pointers dynamically allocated by caps_msg_decode_structure().
 */
void
caps_struct_free_pointers(void *data, caps_struct_t cs)
{
    caps_label_t label;
    void *ptr;

    for (label = cs->labels; label->offset >= 0; ++label) {
	ptr = (char *)data + label->offset;
	if (label->nary > 1) {
	    caps_array_free_pointers(ptr, label);
	} else if (label->csinfo) {
	    caps_struct_free_pointers(ptr, label->csinfo);
	} else if (label->type & CAPS_OPF_PTR) {
	    if (*(void **)ptr) {
		free(*(void **)ptr);
		*(void **)ptr = NULL;
	    }
	}
    }
}

void
caps_array_free_pointers(void *data, caps_label_t label)
{
    int i;

    for (i = 0; i < label->nary; ++i) {
	if (label->csinfo) {
	    caps_struct_free_pointers(data, label->csinfo);
	} else if (label->type & CAPS_OPF_PTR) {
	    if (*(void **)data) {
		free(*(void **)data);
		*(void **)data = NULL;
	    }
	}
	data = (char *)data + 
	    ((label->type & CAPS_OPF_PTR) ? sizeof(void *) : label->size);
    }
}
