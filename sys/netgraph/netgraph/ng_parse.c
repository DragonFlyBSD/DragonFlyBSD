
/*
 * ng_parse.c
 *
 * Copyright (c) 1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $Whistle: ng_parse.c,v 1.3 1999/11/29 01:43:48 archie Exp $
 * $FreeBSD: src/sys/netgraph/ng_parse.c,v 1.3.2.8 2002/07/02 23:44:02 archie Exp $
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/ctype.h>

#include <netinet/in.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>

/* Compute alignment for primitive integral types */
struct int16_temp {
	char	x;
	int16_t	y;
};

struct int32_temp {
	char	x;
	int32_t	y;
};

struct int64_temp {
	char	x;
	int64_t	y;
};

#define INT8_ALIGNMENT		1
#define INT16_ALIGNMENT		((uintptr_t)&((struct int16_temp *)0)->y)
#define INT32_ALIGNMENT		((uintptr_t)&((struct int32_temp *)0)->y)
#define INT64_ALIGNMENT		((uintptr_t)&((struct int64_temp *)0)->y)

/* Output format for integral types */
#define INT_UNSIGNED		0
#define INT_SIGNED		1
#define INT_HEX			2

/* Type of composite object: struct, array, or fixedarray */
enum comptype {
	CT_STRUCT,
	CT_ARRAY,
	CT_FIXEDARRAY,
};

/* Composite types helper functions */
static int	ng_parse_composite(const struct ng_parse_type *type,
			const char *s, int *off, const u_char *start,
			u_char *const buf, int *buflen, enum comptype ctype);
static int	ng_unparse_composite(const struct ng_parse_type *type,
			const u_char *data, int *off, char *cbuf, int cbuflen,
			enum comptype ctype);
static int	ng_get_composite_elem_default(const struct ng_parse_type *type,
			int index, const u_char *start, u_char *buf,
			int *buflen, enum comptype ctype);
static int	ng_get_composite_len(const struct ng_parse_type *type,
			const u_char *start, const u_char *buf,
			enum comptype ctype);
static const	struct ng_parse_type *ng_get_composite_etype(const struct
			ng_parse_type *type, int index, enum comptype ctype);
static int	ng_parse_get_elem_pad(const struct ng_parse_type *type,
			int index, enum comptype ctype, int posn);

/* Parsing helper functions */
static int	ng_parse_skip_value(const char *s, int off, int *lenp);

/* Poor man's virtual method calls */
#define METHOD(t,m)	(ng_get_ ## m ## _method(t))
#define INVOKE(t,m)	(*METHOD(t,m))

static ng_parse_t	*ng_get_parse_method(const struct ng_parse_type *t);
static ng_unparse_t	*ng_get_unparse_method(const struct ng_parse_type *t);
static ng_getDefault_t	*ng_get_getDefault_method(const
				struct ng_parse_type *t);
static ng_getAlign_t	*ng_get_getAlign_method(const struct ng_parse_type *t);

#define ALIGNMENT(t)	(METHOD(t, getAlign) == NULL ? \
				0 : INVOKE(t, getAlign)(t))

/* For converting binary to string */
#define NG_PARSE_APPEND(fmt, args...)				\
		do {						\
			int len;				\
								\
			len = ksnprintf((cbuf), (cbuflen),	\
				fmt , ## args);			\
			if (len >= (cbuflen))			\
				return (ERANGE);		\
			(cbuf) += len;				\
			(cbuflen) -= len;			\
		} while (0)

/************************************************************************
			PUBLIC FUNCTIONS
 ************************************************************************/

/*
 * Convert an ASCII string to binary according to the supplied type descriptor
 */
int
ng_parse(const struct ng_parse_type *type,
	const char *string, int *off, u_char *buf, int *buflen)
{
	return INVOKE(type, parse)(type, string, off, buf, buf, buflen);
}

/*
 * Convert binary to an ASCII string according to the supplied type descriptor
 */
int
ng_unparse(const struct ng_parse_type *type,
	const u_char *data, char *cbuf, int cbuflen)
{
	int off = 0;

	return INVOKE(type, unparse)(type, data, &off, cbuf, cbuflen);
}

/*
 * Fill in the default value according to the supplied type descriptor
 */
int
ng_parse_getDefault(const struct ng_parse_type *type, u_char *buf, int *buflen)
{
	ng_getDefault_t *const func = METHOD(type, getDefault);

	if (func == NULL)
		return (EOPNOTSUPP);
	return (*func)(type, buf, buf, buflen);
}


/************************************************************************
			STRUCTURE TYPE
 ************************************************************************/

static int
ng_struct_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	return ng_parse_composite(type, s, off, start, buf, buflen, CT_STRUCT);
}

static int
ng_struct_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	return ng_unparse_composite(type, data, off, cbuf, cbuflen, CT_STRUCT);
}

static int
ng_struct_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{
	int off = 0;

	return ng_parse_composite(type,
	    "{}", &off, start, buf, buflen, CT_STRUCT);
}

static int
ng_struct_getAlign(const struct ng_parse_type *type)
{
	const struct ng_parse_struct_field *field;
	int align = 0;

	for (field = type->info; field->name != NULL; field++) {
		int falign = ALIGNMENT(field->type);

		if (falign > align)
			align = falign;
	}
	return align;
}

const struct ng_parse_type ng_parse_struct_type = {
	NULL,
	NULL,
	NULL,
	ng_struct_parse,
	ng_struct_unparse,
	ng_struct_getDefault,
	ng_struct_getAlign
};

/************************************************************************
			FIXED LENGTH ARRAY TYPE
 ************************************************************************/

static int
ng_fixedarray_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	return ng_parse_composite(type,
	    s, off, start, buf, buflen, CT_FIXEDARRAY);
}

static int
ng_fixedarray_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	return ng_unparse_composite(type,
		data, off, cbuf, cbuflen, CT_FIXEDARRAY);
}

static int
ng_fixedarray_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{
	int off = 0;

	return ng_parse_composite(type,
	    "[]", &off, start, buf, buflen, CT_FIXEDARRAY);
}

static int
ng_fixedarray_getAlign(const struct ng_parse_type *type)
{
	const struct ng_parse_fixedarray_info *fi = type->info;

	return ALIGNMENT(fi->elementType);
}

const struct ng_parse_type ng_parse_fixedarray_type = {
	NULL,
	NULL,
	NULL,
	ng_fixedarray_parse,
	ng_fixedarray_unparse,
	ng_fixedarray_getDefault,
	ng_fixedarray_getAlign
};

/************************************************************************
			VARIABLE LENGTH ARRAY TYPE
 ************************************************************************/

static int
ng_array_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	return ng_parse_composite(type, s, off, start, buf, buflen, CT_ARRAY);
}

static int
ng_array_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	return ng_unparse_composite(type, data, off, cbuf, cbuflen, CT_ARRAY);
}

static int
ng_array_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{
	int off = 0;

	return ng_parse_composite(type,
	    "[]", &off, start, buf, buflen, CT_ARRAY);
}

static int
ng_array_getAlign(const struct ng_parse_type *type)
{
	const struct ng_parse_array_info *ai = type->info;

	return ALIGNMENT(ai->elementType);
}

const struct ng_parse_type ng_parse_array_type = {
	NULL,
	NULL,
	NULL,
	ng_array_parse,
	ng_array_unparse,
	ng_array_getDefault,
	ng_array_getAlign
};

/************************************************************************
				INT8 TYPE
 ************************************************************************/

static int
ng_int8_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	long val;
	int8_t val8;
	char *eptr;

	val = strtol(s + *off, &eptr, 0);
	if (val < (int8_t)0x80 || val > (u_int8_t)0xff || eptr == s + *off)
		return (EINVAL);
	*off = eptr - s;
	val8 = (int8_t)val;
	bcopy(&val8, buf, sizeof(int8_t));
	*buflen = sizeof(int8_t);
	return (0);
}

static int
ng_int8_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	const char *fmt;
	int fval;
	int8_t val;

	bcopy(data + *off, &val, sizeof(int8_t));
	switch ((int)(intptr_t)type->info) {
	case INT_SIGNED:
		fmt = "%d";
		fval = val;
		break;
	case INT_UNSIGNED:
		fmt = "%u";
		fval = (u_int8_t)val;
		break;
	case INT_HEX:
		fmt = "0x%x";
		fval = (u_int8_t)val;
		break;
	default:
		panic("%s: unknown type", __func__);
	}
	NG_PARSE_APPEND(fmt, fval);
	*off += sizeof(int8_t);
	return (0);
}

static int
ng_int8_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{
	int8_t val;

	if (*buflen < sizeof(int8_t))
		return (ERANGE);
	val = 0;
	bcopy(&val, buf, sizeof(int8_t));
	*buflen = sizeof(int8_t);
	return (0);
}

static int
ng_int8_getAlign(const struct ng_parse_type *type)
{
	return INT8_ALIGNMENT;
}

const struct ng_parse_type ng_parse_int8_type = {
	NULL,
	(void *)INT_SIGNED,
	NULL,
	ng_int8_parse,
	ng_int8_unparse,
	ng_int8_getDefault,
	ng_int8_getAlign
};

const struct ng_parse_type ng_parse_uint8_type = {
	&ng_parse_int8_type,
	(void *)INT_UNSIGNED
};

const struct ng_parse_type ng_parse_hint8_type = {
	&ng_parse_int8_type,
	(void *)INT_HEX
};

/************************************************************************
				INT16 TYPE
 ************************************************************************/

static int
ng_int16_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	long val;
	int16_t val16;
	char *eptr;

	val = strtol(s + *off, &eptr, 0);
	if (val < (int16_t)0x8000
	    || val > (u_int16_t)0xffff || eptr == s + *off)
		return (EINVAL);
	*off = eptr - s;
	val16 = (int16_t)val;
	bcopy(&val16, buf, sizeof(int16_t));
	*buflen = sizeof(int16_t);
	return (0);
}

static int
ng_int16_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	const char *fmt;
	int fval;
	int16_t val;

	bcopy(data + *off, &val, sizeof(int16_t));
	switch ((int)(intptr_t)type->info) {
	case INT_SIGNED:
		fmt = "%d";
		fval = val;
		break;
	case INT_UNSIGNED:
		fmt = "%u";
		fval = (u_int16_t)val;
		break;
	case INT_HEX:
		fmt = "0x%x";
		fval = (u_int16_t)val;
		break;
	default:
		panic("%s: unknown type", __func__);
	}
	NG_PARSE_APPEND(fmt, fval);
	*off += sizeof(int16_t);
	return (0);
}

static int
ng_int16_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{
	int16_t val;

	if (*buflen < sizeof(int16_t))
		return (ERANGE);
	val = 0;
	bcopy(&val, buf, sizeof(int16_t));
	*buflen = sizeof(int16_t);
	return (0);
}

static int
ng_int16_getAlign(const struct ng_parse_type *type)
{
	return INT16_ALIGNMENT;
}

const struct ng_parse_type ng_parse_int16_type = {
	NULL,
	(void *)INT_SIGNED,
	NULL,
	ng_int16_parse,
	ng_int16_unparse,
	ng_int16_getDefault,
	ng_int16_getAlign
};

const struct ng_parse_type ng_parse_uint16_type = {
	&ng_parse_int16_type,
	(void *)INT_UNSIGNED
};

const struct ng_parse_type ng_parse_hint16_type = {
	&ng_parse_int16_type,
	(void *)INT_HEX
};

/************************************************************************
				INT32 TYPE
 ************************************************************************/

static int
ng_int32_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	long val;			/* assumes long is at least 32 bits */
	int32_t val32;
	char *eptr;

	val = strtol(s + *off, &eptr, 0);
	if (val < (int32_t)0x80000000
	    || val > (u_int32_t)0xffffffff || eptr == s + *off)
		return (EINVAL);
	*off = eptr - s;
	val32 = (int32_t)val;
	bcopy(&val32, buf, sizeof(int32_t));
	*buflen = sizeof(int32_t);
	return (0);
}

static int
ng_int32_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	const char *fmt;
	long fval;
	int32_t val;

	bcopy(data + *off, &val, sizeof(int32_t));
	switch ((int)(intptr_t)type->info) {
	case INT_SIGNED:
		fmt = "%ld";
		fval = val;
		break;
	case INT_UNSIGNED:
		fmt = "%lu";
		fval = (u_int32_t)val;
		break;
	case INT_HEX:
		fmt = "0x%lx";
		fval = (u_int32_t)val;
		break;
	default:
		panic("%s: unknown type", __func__);
	}
	NG_PARSE_APPEND(fmt, fval);
	*off += sizeof(int32_t);
	return (0);
}

static int
ng_int32_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{
	int32_t val;

	if (*buflen < sizeof(int32_t))
		return (ERANGE);
	val = 0;
	bcopy(&val, buf, sizeof(int32_t));
	*buflen = sizeof(int32_t);
	return (0);
}

static int
ng_int32_getAlign(const struct ng_parse_type *type)
{
	return INT32_ALIGNMENT;
}

const struct ng_parse_type ng_parse_int32_type = {
	NULL,
	(void *)INT_SIGNED,
	NULL,
	ng_int32_parse,
	ng_int32_unparse,
	ng_int32_getDefault,
	ng_int32_getAlign
};

const struct ng_parse_type ng_parse_uint32_type = {
	&ng_parse_int32_type,
	(void *)INT_UNSIGNED
};

const struct ng_parse_type ng_parse_hint32_type = {
	&ng_parse_int32_type,
	(void *)INT_HEX
};

/************************************************************************
				INT64 TYPE
 ************************************************************************/

static int
ng_int64_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	quad_t val;
	int64_t val64;
	char *eptr;

	val = strtoq(s + *off, &eptr, 0);
	if (eptr == s + *off)
		return (EINVAL);
	*off = eptr - s;
	val64 = (int64_t)val;
	bcopy(&val64, buf, sizeof(int64_t));
	*buflen = sizeof(int64_t);
	return (0);
}

static int
ng_int64_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	const char *fmt;
	long long fval;
	int64_t val;

	bcopy(data + *off, &val, sizeof(int64_t));
	switch ((int)(intptr_t)type->info) {
	case INT_SIGNED:
		fmt = "%lld";
		fval = val;
		break;
	case INT_UNSIGNED:
		fmt = "%llu";
		fval = (u_int64_t)val;
		break;
	case INT_HEX:
		fmt = "0x%llx";
		fval = (u_int64_t)val;
		break;
	default:
		panic("%s: unknown type", __func__);
	}
	NG_PARSE_APPEND(fmt, fval);
	*off += sizeof(int64_t);
	return (0);
}

static int
ng_int64_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{
	int64_t val;

	if (*buflen < sizeof(int64_t))
		return (ERANGE);
	val = 0;
	bcopy(&val, buf, sizeof(int64_t));
	*buflen = sizeof(int64_t);
	return (0);
}

static int
ng_int64_getAlign(const struct ng_parse_type *type)
{
	return INT64_ALIGNMENT;
}

const struct ng_parse_type ng_parse_int64_type = {
	NULL,
	(void *)INT_SIGNED,
	NULL,
	ng_int64_parse,
	ng_int64_unparse,
	ng_int64_getDefault,
	ng_int64_getAlign
};

const struct ng_parse_type ng_parse_uint64_type = {
	&ng_parse_int64_type,
	(void *)INT_UNSIGNED
};

const struct ng_parse_type ng_parse_hint64_type = {
	&ng_parse_int64_type,
	(void *)INT_HEX
};

/************************************************************************
				STRING TYPE
 ************************************************************************/

static int
ng_string_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	char *sval;
	int len;

	if ((sval = ng_get_string_token(s, off, &len)) == NULL)
		return (EINVAL);
	*off += len;
	len = strlen(sval) + 1;
	bcopy(sval, buf, len);
	kfree(sval, M_NETGRAPH);
	*buflen = len;
	return (0);
}

static int
ng_string_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	const char *const raw = (const char *)data + *off;
	char *const s = ng_encode_string(raw);

	if (s == NULL)
		return (ENOMEM);
	NG_PARSE_APPEND("%s", s);
	*off += strlen(raw) + 1;
	kfree(s, M_NETGRAPH);
	return (0);
}

static int
ng_string_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{

	if (*buflen < 1)
		return (ERANGE);
	buf[0] = (u_char)'\0';
	*buflen = 1;
	return (0);
}

const struct ng_parse_type ng_parse_string_type = {
	NULL,
	NULL,
	NULL,
	ng_string_parse,
	ng_string_unparse,
	ng_string_getDefault,
	NULL
};

/************************************************************************
			FIXED BUFFER STRING TYPE
 ************************************************************************/

static int
ng_fixedstring_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	const struct ng_parse_fixedstring_info *const fi = type->info;
	char *sval;
	int len;

	if ((sval = ng_get_string_token(s, off, &len)) == NULL)
		return (EINVAL);
	if (strlen(sval) + 1 > fi->bufSize)
		return (E2BIG);
	*off += len;
	len = strlen(sval) + 1;
	bcopy(sval, buf, len);
	kfree(sval, M_NETGRAPH);
	bzero(buf + len, fi->bufSize - len);
	*buflen = fi->bufSize;
	return (0);
}

static int
ng_fixedstring_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	const struct ng_parse_fixedstring_info *const fi = type->info;
	int error, temp = *off;

	if ((error = ng_string_unparse(type, data, &temp, cbuf, cbuflen)) != 0)
		return (error);
	*off += fi->bufSize;
	return (0);
}

static int
ng_fixedstring_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{
	const struct ng_parse_fixedstring_info *const fi = type->info;

	if (*buflen < fi->bufSize)
		return (ERANGE);
	bzero(buf, fi->bufSize);
	*buflen = fi->bufSize;
	return (0);
}

const struct ng_parse_type ng_parse_fixedstring_type = {
	NULL,
	NULL,
	NULL,
	ng_fixedstring_parse,
	ng_fixedstring_unparse,
	ng_fixedstring_getDefault,
	NULL
};

const struct ng_parse_fixedstring_info ng_parse_nodebuf_info = {
	NG_NODESIZ
};
const struct ng_parse_type ng_parse_nodebuf_type = {
	&ng_parse_fixedstring_type,
	&ng_parse_nodebuf_info
};

const struct ng_parse_fixedstring_info ng_parse_hookbuf_info = {
	NG_HOOKSIZ
};
const struct ng_parse_type ng_parse_hookbuf_type = {
	&ng_parse_fixedstring_type,
	&ng_parse_hookbuf_info
};

const struct ng_parse_fixedstring_info ng_parse_pathbuf_info = {
	NG_PATHSIZ
};
const struct ng_parse_type ng_parse_pathbuf_type = {
	&ng_parse_fixedstring_type,
	&ng_parse_pathbuf_info
};

const struct ng_parse_fixedstring_info ng_parse_typebuf_info = {
	NG_TYPESIZ
};
const struct ng_parse_type ng_parse_typebuf_type = {
	&ng_parse_fixedstring_type,
	&ng_parse_typebuf_info
};

const struct ng_parse_fixedstring_info ng_parse_cmdbuf_info = {
	NG_CMDSTRSIZ
};
const struct ng_parse_type ng_parse_cmdbuf_type = {
	&ng_parse_fixedstring_type,
	&ng_parse_cmdbuf_info
};

/************************************************************************
			IP ADDRESS TYPE
 ************************************************************************/

static int
ng_ipaddr_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	int i, error;

	for (i = 0; i < 4; i++) {
		if ((error = ng_int8_parse(&ng_parse_int8_type,
		    s, off, start, buf + i, buflen)) != 0)
			return (error);
		if (i < 3 && s[*off] != '.')
			return (EINVAL);
		(*off)++;
	}
	*buflen = 4;
	return (0);
}

static int
ng_ipaddr_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	struct in_addr ip;

	bcopy(data + *off, &ip, sizeof(ip));
	NG_PARSE_APPEND("%d.%d.%d.%d", ((u_char *)&ip)[0],
	    ((u_char *)&ip)[1], ((u_char *)&ip)[2], ((u_char *)&ip)[3]);
	*off += sizeof(ip);
	return (0);
}

static int
ng_ipaddr_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{
	struct in_addr ip = { 0 };

	if (*buflen < sizeof(ip))
		return (ERANGE);
	bcopy(&ip, buf, sizeof(ip));
	*buflen = sizeof(ip);
	return (0);
}

const struct ng_parse_type ng_parse_ipaddr_type = {
	NULL,
	NULL,
	NULL,
	ng_ipaddr_parse,
	ng_ipaddr_unparse,
	ng_ipaddr_getDefault,
	ng_int32_getAlign
};

/************************************************************************
			BYTE ARRAY TYPE
 ************************************************************************/

/* Get the length of a byte array */
static int
ng_parse_bytearray_subtype_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	ng_parse_array_getLength_t *const getLength = type->private;

	return (*getLength)(type, start, buf);
}

/* Byte array element type is hex int8 */
static const struct ng_parse_array_info ng_parse_bytearray_subtype_info = {
	&ng_parse_hint8_type,
	&ng_parse_bytearray_subtype_getLength,
	NULL
};
static const struct ng_parse_type ng_parse_bytearray_subtype = {
	&ng_parse_array_type,
	&ng_parse_bytearray_subtype_info
};

static int
ng_bytearray_parse(const struct ng_parse_type *type,
	const char *s, int *off, const u_char *const start,
	u_char *const buf, int *buflen)
{
	char *str;
	int toklen;

	/* We accept either an array of bytes or a string constant */
	if ((str = ng_get_string_token(s, off, &toklen)) != NULL) {
		ng_parse_array_getLength_t *const getLength = type->info;
		int arraylen, slen;

		arraylen = (*getLength)(type, start, buf);
		if (arraylen > *buflen) {
			kfree(str, M_NETGRAPH);
			return (ERANGE);
		}
		slen = strlen(str) + 1;
		if (slen > arraylen) {
			kfree(str, M_NETGRAPH);
			return (E2BIG);
		}
		bcopy(str, buf, slen);
		bzero(buf + slen, arraylen - slen);
		kfree(str, M_NETGRAPH);
		*off += toklen;
		*buflen = arraylen;
		return (0);
	} else {
		struct ng_parse_type subtype;

		subtype = ng_parse_bytearray_subtype;
		*(const void **)(void *)&subtype.private = type->info;
		return ng_array_parse(&subtype, s, off, start, buf, buflen);
	}
}

static int
ng_bytearray_unparse(const struct ng_parse_type *type,
	const u_char *data, int *off, char *cbuf, int cbuflen)
{
	struct ng_parse_type subtype;

	subtype = ng_parse_bytearray_subtype;
	*(const void **)(void *)&subtype.private = type->info;
	return ng_array_unparse(&subtype, data, off, cbuf, cbuflen);
}

static int
ng_bytearray_getDefault(const struct ng_parse_type *type,
	const u_char *const start, u_char *buf, int *buflen)
{
	struct ng_parse_type subtype;

	subtype = ng_parse_bytearray_subtype;
	*(const void **)(void *)&subtype.private = type->info;
	return ng_array_getDefault(&subtype, start, buf, buflen);
}

const struct ng_parse_type ng_parse_bytearray_type = {
	NULL,
	NULL,
	NULL,
	ng_bytearray_parse,
	ng_bytearray_unparse,
	ng_bytearray_getDefault,
	NULL
};

/************************************************************************
			STRUCT NG_MESG TYPE
 ************************************************************************/

/* Get msg->header.arglen when "buf" is pointing to msg->data */
static int
ng_parse_ng_mesg_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	const struct ng_mesg *msg;

	msg = (const struct ng_mesg *)(buf - sizeof(*msg));
	return msg->header.arglen;
}

/* Type for the variable length data portion of a struct ng_mesg */
static const struct ng_parse_type ng_msg_data_type = {
	&ng_parse_bytearray_type,
	&ng_parse_ng_mesg_getLength
};

/* Type for the entire struct ng_mesg header with data section */
static const struct ng_parse_struct_field ng_parse_ng_mesg_type_fields[]
	= NG_GENERIC_NG_MESG_INFO(&ng_msg_data_type);
const struct ng_parse_type ng_parse_ng_mesg_type = {
	&ng_parse_struct_type,
	&ng_parse_ng_mesg_type_fields,
};

/************************************************************************
			COMPOSITE HELPER ROUTINES
 ************************************************************************/

/*
 * Convert a structure or array from ASCII to binary
 */
static int
ng_parse_composite(const struct ng_parse_type *type, const char *s,
	int *off, const u_char *const start, u_char *const buf, int *buflen,
	const enum comptype ctype)
{
	const int num = ng_get_composite_len(type, start, buf, ctype);
	int nextIndex = 0;		/* next implicit array index */
	u_int index;			/* field or element index */
	int *foff;			/* field value offsets in string */
	int align, len, blen, error = 0;

	/* Initialize */
	foff = kmalloc(num * sizeof(*foff), M_NETGRAPH, M_NOWAIT | M_ZERO);
	if (foff == NULL) {
		error = ENOMEM;
		goto done;
	}

	/* Get opening brace/bracket */
	if (ng_parse_get_token(s, off, &len)
	    != (ctype == CT_STRUCT ? T_LBRACE : T_LBRACKET)) {
		error = EINVAL;
		goto done;
	}
	*off += len;

	/* Get individual element value positions in the string */
	for (;;) {
		enum ng_parse_token tok;

		/* Check for closing brace/bracket */
		tok = ng_parse_get_token(s, off, &len);
		if (tok == (ctype == CT_STRUCT ? T_RBRACE : T_RBRACKET)) {
			*off += len;
			break;
		}

		/* For arrays, the 'name' (ie, index) is optional, so
		   distinguish name from values by seeing if the next
		   token is an equals sign */
		if (ctype != CT_STRUCT) {
			int len2, off2;
			char *eptr;

			/* If an opening brace/bracket, index is implied */
			if (tok == T_LBRACE || tok == T_LBRACKET) {
				index = nextIndex++;
				goto gotIndex;
			}

			/* Might be an index, might be a value, either way... */
			if (tok != T_WORD) {
				error = EINVAL;
				goto done;
			}

			/* If no equals sign follows, index is implied */
			off2 = *off + len;
			if (ng_parse_get_token(s, &off2, &len2) != T_EQUALS) {
				index = nextIndex++;
				goto gotIndex;
			}

			/* Index was specified explicitly; parse it */
			index = (u_int)strtoul(s + *off, &eptr, 0);
			if (index < 0 || eptr - (s + *off) != len) {
				error = EINVAL;
				goto done;
			}
			nextIndex = index + 1;
			*off += len + len2;
gotIndex:
			; /* avoid gcc3.x warning */
		} else {			/* a structure field */
			const struct ng_parse_struct_field *const
			    fields = type->info;

			/* Find the field by name (required) in field list */
			if (tok != T_WORD) {
				error = EINVAL;
				goto done;
			}
			for (index = 0; index < num; index++) {
				const struct ng_parse_struct_field *const
				    field = &fields[index];

				if (strncmp(&s[*off], field->name, len) == 0
				    && field->name[len] == '\0')
					break;
			}
			if (index == num) {
				error = ENOENT;
				goto done;
			}
			*off += len;

			/* Get equals sign */
			if (ng_parse_get_token(s, off, &len) != T_EQUALS) {
				error = EINVAL;
				goto done;
			}
			*off += len;
		}

		/* Check array index */
		if (index >= num) {
			error = E2BIG;
			goto done;
		}

		/* Save value's position and skip over it for now */
		if (foff[index] != 0) {
			error = EALREADY;		/* duplicate */
			goto done;
		}
		while (isspace(s[*off]))
			(*off)++;
		foff[index] = *off;
		if ((error = ng_parse_skip_value(s, *off, &len)) != 0)
			goto done;
		*off += len;
	}

	/* Now build binary structure from supplied values and defaults */
	for (blen = index = 0; index < num; index++) {
		const struct ng_parse_type *const
		    etype = ng_get_composite_etype(type, index, ctype);
		int k, pad, vlen;

		/* Zero-pad any alignment bytes */
		pad = ng_parse_get_elem_pad(type, index, ctype, blen);
		for (k = 0; k < pad; k++) {
			if (blen >= *buflen) {
				error = ERANGE;
				goto done;
			}
			buf[blen++] = 0;
		}

		/* Get value */
		vlen = *buflen - blen;
		if (foff[index] == 0) {		/* use default value */
			error = ng_get_composite_elem_default(type, index,
			    start, buf + blen, &vlen, ctype);
		} else {			/* parse given value */
			*off = foff[index];
			error = INVOKE(etype, parse)(etype,
			    s, off, start, buf + blen, &vlen);
		}
		if (error != 0)
			goto done;
		blen += vlen;
	}

	/* Make total composite structure size a multiple of its alignment */
	if ((align = ALIGNMENT(type)) != 0) {
		while (blen % align != 0) {
			if (blen >= *buflen) {
				error = ERANGE;
				goto done;
			}
			buf[blen++] = 0;
		}
	}

	/* Done */
	*buflen = blen;
done:
	if (foff != NULL)
		kfree(foff, M_NETGRAPH);
	return (error);
}

/*
 * Convert an array or structure from binary to ASCII
 */
static int
ng_unparse_composite(const struct ng_parse_type *type, const u_char *data,
	int *off, char *cbuf, int cbuflen, const enum comptype ctype)
{
	const struct ng_mesg *const hdr
	    = (const struct ng_mesg *)(data - sizeof(*hdr));
	const int num = ng_get_composite_len(type, data, data + *off, ctype);
	const int workSize = 20 * 1024;		/* XXX hard coded constant */
	int nextIndex = 0, didOne = 0;
	int error, index;
	u_char *workBuf;

	/* Get workspace for checking default values */
	workBuf = kmalloc(workSize, M_NETGRAPH, M_NOWAIT);
	if (workBuf == NULL)
		return (ENOMEM);

	/* Opening brace/bracket */
	NG_PARSE_APPEND("%c", (ctype == CT_STRUCT) ? '{' : '[');

	/* Do each item */
	for (index = 0; index < num; index++) {
		const struct ng_parse_type *const
		    etype = ng_get_composite_etype(type, index, ctype);

		/* Skip any alignment pad bytes */
		*off += ng_parse_get_elem_pad(type, index, ctype, *off);

		/*
		 * See if element is equal to its default value; skip if so.
		 * Copy struct ng_mesg header for types that peek into it.
		 */
		if (sizeof(*hdr) + *off < workSize) {
			int tempsize = workSize - sizeof(*hdr) - *off;

			bcopy(hdr, workBuf, sizeof(*hdr) + *off);
			if (ng_get_composite_elem_default(type, index, workBuf
			      + sizeof(*hdr), workBuf + sizeof(*hdr) + *off,
			      &tempsize, ctype) == 0
			    && bcmp(workBuf + sizeof(*hdr) + *off,
			      data + *off, tempsize) == 0) {
				*off += tempsize;
				continue;
			}
		}

		/* Print name= */
		NG_PARSE_APPEND(" ");
		if (ctype != CT_STRUCT) {
			if (index != nextIndex) {
				nextIndex = index;
				NG_PARSE_APPEND("%d=", index);
			}
			nextIndex++;
		} else {
			const struct ng_parse_struct_field *const
			    fields = type->info;

			NG_PARSE_APPEND("%s=", fields[index].name);
		}

		/* Print value */
		if ((error = INVOKE(etype, unparse)
		    (etype, data, off, cbuf, cbuflen)) != 0) {
			kfree(workBuf, M_NETGRAPH);
			return (error);
		}
		cbuflen -= strlen(cbuf);
		cbuf += strlen(cbuf);
		didOne = 1;
	}
	kfree(workBuf, M_NETGRAPH);

	/* Closing brace/bracket */
	NG_PARSE_APPEND("%s%c",
	    didOne ? " " : "", (ctype == CT_STRUCT) ? '}' : ']');
	return (0);
}

/*
 * Generate the default value for an element of an array or structure
 * Returns EOPNOTSUPP if default value is unspecified.
 */
static int
ng_get_composite_elem_default(const struct ng_parse_type *type,
	int index, const u_char *const start, u_char *buf, int *buflen,
	const enum comptype ctype)
{
	const struct ng_parse_type *etype;
	ng_getDefault_t *func;

	switch (ctype) {
	case CT_STRUCT:
		break;
	case CT_ARRAY:
	    {
		const struct ng_parse_array_info *const ai = type->info;

		if (ai->getDefault != NULL) {
			return (*ai->getDefault)(type,
			    index, start, buf, buflen);
		}
		break;
	    }
	case CT_FIXEDARRAY:
	    {
		const struct ng_parse_fixedarray_info *const fi = type->info;

		if (*fi->getDefault != NULL) {
			return (*fi->getDefault)(type,
			    index, start, buf, buflen);
		}
		break;
	    }
	default:
	    panic("%s", __func__);
	}

	/* Default to element type default */
	etype = ng_get_composite_etype(type, index, ctype);
	func = METHOD(etype, getDefault);
	if (func == NULL)
		return (EOPNOTSUPP);
	return (*func)(etype, start, buf, buflen);
}

/*
 * Get the number of elements in a struct, variable or fixed array.
 */
static int
ng_get_composite_len(const struct ng_parse_type *type,
	const u_char *const start, const u_char *buf,
	const enum comptype ctype)
{
	switch (ctype) {
	case CT_STRUCT:
	    {
		const struct ng_parse_struct_field *const fields = type->info;
		int numFields = 0;

		for (numFields = 0; ; numFields++) {
			const struct ng_parse_struct_field *const
				fi = &fields[numFields];

			if (fi->name == NULL)
				break;
		}
		return (numFields);
	    }
	case CT_ARRAY:
	    {
		const struct ng_parse_array_info *const ai = type->info;

		return (*ai->getLength)(type, start, buf);
	    }
	case CT_FIXEDARRAY:
	    {
		const struct ng_parse_fixedarray_info *const fi = type->info;

		return fi->length;
	    }
	default:
	    panic("%s", __func__);
	}
	return (0);
}

/*
 * Return the type of the index'th element of a composite structure
 */
static const struct ng_parse_type *
ng_get_composite_etype(const struct ng_parse_type *type,
	int index, const enum comptype ctype)
{
	const struct ng_parse_type *etype = NULL;

	switch (ctype) {
	case CT_STRUCT:
	    {
		const struct ng_parse_struct_field *const fields = type->info;

		etype = fields[index].type;
		break;
	    }
	case CT_ARRAY:
	    {
		const struct ng_parse_array_info *const ai = type->info;

		etype = ai->elementType;
		break;
	    }
	case CT_FIXEDARRAY:
	    {
		const struct ng_parse_fixedarray_info *const fi = type->info;

		etype = fi->elementType;
		break;
	    }
	default:
	    panic("%s", __func__);
	}
	return (etype);
}

/*
 * Get the number of bytes to skip to align for the next
 * element in a composite structure.
 */
static int
ng_parse_get_elem_pad(const struct ng_parse_type *type,
	int index, enum comptype ctype, int posn)
{
	const struct ng_parse_type *const
	    etype = ng_get_composite_etype(type, index, ctype);
	int align;

	/* Get element's alignment, and possibly override */
	align = ALIGNMENT(etype);
	if (ctype == CT_STRUCT) {
		const struct ng_parse_struct_field *const fields = type->info;

		if (fields[index].alignment != 0)
			align = fields[index].alignment;
	}

	/* Return number of bytes to skip to align */
	return (align ? (align - (posn % align)) % align : 0);
}

/************************************************************************
			PARSING HELPER ROUTINES
 ************************************************************************/

/*
 * Skip over a value
 */
static int
ng_parse_skip_value(const char *s, int off0, int *lenp)
{
	int len, nbracket, nbrace;
	int off = off0;

	len = nbracket = nbrace = 0;
	do {
		switch (ng_parse_get_token(s, &off, &len)) {
		case T_LBRACKET:
			nbracket++;
			break;
		case T_LBRACE:
			nbrace++;
			break;
		case T_RBRACKET:
			if (nbracket-- == 0)
				return (EINVAL);
			break;
		case T_RBRACE:
			if (nbrace-- == 0)
				return (EINVAL);
			break;
		case T_EOF:
			return (EINVAL);
		default:
			break;
		}
		off += len;
	} while (nbracket > 0 || nbrace > 0);
	*lenp = off - off0;
	return (0);
}

/*
 * Find the next token in the string, starting at offset *startp.
 * Returns the token type, with *startp pointing to the first char
 * and *lenp the length.
 */
enum ng_parse_token
ng_parse_get_token(const char *s, int *startp, int *lenp)
{
	char *t;
	int i;

	while (isspace(s[*startp]))
		(*startp)++;
	switch (s[*startp]) {
	case '\0':
		*lenp = 0;
		return T_EOF;
	case '{':
		*lenp = 1;
		return T_LBRACE;
	case '}':
		*lenp = 1;
		return T_RBRACE;
	case '[':
		*lenp = 1;
		return T_LBRACKET;
	case ']':
		*lenp = 1;
		return T_RBRACKET;
	case '=':
		*lenp = 1;
		return T_EQUALS;
	case '"':
		if ((t = ng_get_string_token(s, startp, lenp)) == NULL)
			return T_ERROR;
		kfree(t, M_NETGRAPH);
		return T_STRING;
	default:
		for (i = *startp + 1; s[i] != '\0' && !isspace(s[i])
		    && s[i] != '{' && s[i] != '}' && s[i] != '['
		    && s[i] != ']' && s[i] != '=' && s[i] != '"'; i++)
			;
		*lenp = i - *startp;
		return T_WORD;
	}
}

/*
 * Get a string token, which must be enclosed in double quotes.
 * The normal C backslash escapes are recognized.
 */
char *
ng_get_string_token(const char *s, int *startp, int *lenp)
{
	char *cbuf, *p;
	int start, off;

	while (isspace(s[*startp]))
		(*startp)++;
	start = *startp;
	if (s[*startp] != '"')
		return (NULL);
	cbuf = kmalloc(strlen(s + start), M_NETGRAPH, M_NOWAIT);
	if (cbuf == NULL)
		return (NULL);
	strcpy(cbuf, s + start + 1);
	for (off = 1, p = cbuf; *p != '\0'; off++, p++) {
		if (*p == '"') {
			*p = '\0';
			*lenp = off + 1;
			return (cbuf);
		} else if (p[0] == '\\' && p[1] != '\0') {
			int x, k;
			char *v;

			strcpy(p, p + 1);
			v = p;
			switch (*p) {
			case 't':
				*v = '\t';
				off++;
				continue;
			case 'n':
				*v = '\n';
				off++;
				continue;
			case 'r':
				*v = '\r';
				off++;
				continue;
			case 'v':
				*v =  '\v';
				off++;
				continue;
			case 'f':
				*v =  '\f';
				off++;
				continue;
			case '"':
				*v =  '"';
				off++;
				continue;
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
				for (x = k = 0;
				    k < 3 && *v >= '0' && *v <= '7'; v++) {
					x = (x << 3) + (*v - '0');
					off++;
				}
				*--v = (char)x;
				break;
			case 'x':
				for (v++, x = k = 0;
				    k < 2 && isxdigit(*v); v++) {
					x = (x << 4) + (isdigit(*v) ?
					      (*v - '0') :
					      (tolower(*v) - 'a' + 10));
					off++;
				}
				*--v = (char)x;
				break;
			default:
				continue;
			}
			strcpy(p, v);
		}
	}
	return (NULL);		/* no closing quote */
}

/*
 * Encode a string so it can be safely put in double quotes.
 * Caller must free the result.
 */
char *
ng_encode_string(const char *raw)
{
	char *cbuf;
	int off = 0;

	cbuf = kmalloc(strlen(raw) * 4 + 3, M_NETGRAPH, M_NOWAIT);
	if (cbuf == NULL)
		return (NULL);
	cbuf[off++] = '"';
	for ( ; *raw != '\0'; raw++) {
		switch (*raw) {
		case '\t':
			cbuf[off++] = '\\';
			cbuf[off++] = 't';
			break;
		case '\f':
			cbuf[off++] = '\\';
			cbuf[off++] = 'f';
			break;
		case '\n':
			cbuf[off++] = '\\';
			cbuf[off++] = 'n';
			break;
		case '\r':
			cbuf[off++] = '\\';
			cbuf[off++] = 'r';
			break;
		case '\v':
			cbuf[off++] = '\\';
			cbuf[off++] = 'v';
			break;
		case '"':
		case '\\':
			cbuf[off++] = '\\';
			cbuf[off++] = *raw;
			break;
		default:
			if (*raw < 0x20 || *raw > 0x7e) {
				off += ksprintf(cbuf + off,
				    "\\x%02x", (u_char)*raw);
				break;
			}
			cbuf[off++] = *raw;
			break;
		}
	}
	cbuf[off++] = '"';
	cbuf[off] = '\0';
	return (cbuf);
}

/************************************************************************
			VIRTUAL METHOD LOOKUP
 ************************************************************************/

static ng_parse_t *
ng_get_parse_method(const struct ng_parse_type *t)
{
	while (t != NULL && t->parse == NULL)
		t = t->supertype;
	return (t ? t->parse : NULL);
}

static ng_unparse_t *
ng_get_unparse_method(const struct ng_parse_type *t)
{
	while (t != NULL && t->unparse == NULL)
		t = t->supertype;
	return (t ? t->unparse : NULL);
}

static ng_getDefault_t *
ng_get_getDefault_method(const struct ng_parse_type *t)
{
	while (t != NULL && t->getDefault == NULL)
		t = t->supertype;
	return (t ? t->getDefault : NULL);
}

static ng_getAlign_t *
ng_get_getAlign_method(const struct ng_parse_type *t)
{
	while (t != NULL && t->getAlign == NULL)
		t = t->supertype;
	return (t ? t->getAlign : NULL);
}
