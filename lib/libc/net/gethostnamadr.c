/*-
 * Copyright (c) 1994, Garrett Wollman
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
 * THIS SOFTWARE IS PROVIDED BY THE CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/libc/net/gethostnamadr.c,v 1.15.2.2 2001/03/05 10:40:42 obrien Exp $
 * $DragonFly: src/lib/libc/net/gethostnamadr.c,v 1.6 2007/12/29 22:55:29 matthias Exp $
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <nsswitch.h>
#include <arpa/nameser.h>		/* XXX hack for _res */
#include <resolv.h>			/* XXX hack for _res */
#ifdef NS_CACHING
#include "nscache.h"
#endif

extern int _ht_gethostbyname(void *, void *, va_list);
extern int _dns_gethostbyname(void *, void *, va_list);
extern int _nis_gethostbyname(void *, void *, va_list);
extern int _ht_gethostbyaddr(void *, void *, va_list);
extern int _dns_gethostbyaddr(void *, void *, va_list);
extern int _nis_gethostbyaddr(void *, void *, va_list);

/* Host lookup order if nsswitch.conf is broken or nonexistant */
static const ns_src default_src[] = {
	{ NSSRC_FILES, NS_SUCCESS },
	{ NSSRC_DNS, NS_SUCCESS },
	{ 0 }
};
#ifdef NS_CACHING
static int host_id_func(char *, size_t *, va_list, void *);
static int host_marshal_func(char *, size_t *, void *, va_list, void *);
static int host_unmarshal_func(char *, size_t, void *, va_list, void *);
#endif

struct hostent *
gethostbyname(const char *name)
{
	struct hostent *hp;

	if ((_res.options & RES_INIT) == 0 && res_init() == -1) {
		h_errno = NETDB_INTERNAL;
		return (NULL);
	}
	if (_res.options & RES_USE_INET6) {		/* XXX */
		hp = gethostbyname2(name, AF_INET6);	/* XXX */
		if (hp)					/* XXX */
			return (hp);			/* XXX */
	}						/* XXX */
	return (gethostbyname2(name, AF_INET));
}

struct hostent *
gethostbyname2(const char *name, int type)
{
	struct hostent *hp = 0;
	int rval;

#ifdef NS_CACHING
	static const nss_cache_info cache_info =
		NS_COMMON_CACHE_INFO_INITIALIZER(
		hosts, (void *)nss_lt_name,
		host_id_func, host_marshal_func, host_unmarshal_func);
#endif
	static const ns_dtab dtab[] = {
		NS_FILES_CB(_ht_gethostbyname, NULL)
		{ NSSRC_DNS, _dns_gethostbyname, NULL },
		NS_NIS_CB(_nis_gethostbyname, NULL) /* force -DHESIOD */
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ 0 }
	};

	rval = nsdispatch((void *)&hp, dtab, NSDB_HOSTS, "gethostbyname",
			  default_src, name, type);

	if (rval != NS_SUCCESS)
		return NULL;
	else
		return hp;
}

struct hostent *
gethostbyaddr(const void *addr, socklen_t len, int type)
{
	struct hostent *hp = 0;
	int rval;

#ifdef NS_CACHING
	static const nss_cache_info cache_info =
		NS_COMMON_CACHE_INFO_INITIALIZER(
		hosts, (void *)nss_lt_id,
		host_id_func, host_marshal_func, host_unmarshal_func);
#endif
	static const ns_dtab dtab[] = {
		NS_FILES_CB(_ht_gethostbyaddr, NULL)
		{ NSSRC_DNS, _dns_gethostbyaddr, NULL },
		NS_NIS_CB(_nis_gethostbyaddr, NULL) /* force -DHESIOD */
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ 0 }
	};

	rval = nsdispatch((void *)&hp, dtab, NSDB_HOSTS, "gethostbyaddr",
			  default_src, addr, len, type);

	if (rval != NS_SUCCESS)
		return NULL;
	else
		return hp;
}

#ifdef NS_CACHING
static int
host_id_func(char *buffer, size_t *buffer_size, va_list ap, void *cache_mdata)
{
	res_state statp;
	u_long res_options;

	const int op_id = 1;
	char *str;
	int len, type;

	size_t desired_size, size;
	enum nss_lookup_type lookup_type;
	char *p;
	int res = NS_UNAVAIL;

	statp = __res_state();
	res_options = statp->options & (RES_RECURSE | RES_DEFNAMES |
	    RES_DNSRCH | RES_NOALIASES | RES_USE_INET6);

	lookup_type = (enum nss_lookup_type)cache_mdata;
	switch (lookup_type) {
	case nss_lt_name:
		str = va_arg(ap, char *);
		type = va_arg(ap, int);

		size = strlen(str);
		desired_size = sizeof(res_options) + sizeof(int) +
		    sizeof(enum nss_lookup_type) + sizeof(int) + size + 1;

		if (desired_size > *buffer_size) {
			res = NS_RETURN;
			goto fin;
		}

		p = buffer;

		memcpy(p, &res_options, sizeof(res_options));
		p += sizeof(res_options);

		memcpy(p, &op_id, sizeof(int));
		p += sizeof(int);

		memcpy(p, &lookup_type, sizeof(enum nss_lookup_type));
		p += sizeof(int);

		memcpy(p, &type, sizeof(int));
		p += sizeof(int);

		memcpy(p, str, size + 1);

		res = NS_SUCCESS;
		break;
	case nss_lt_id:
		str = va_arg(ap, char *);
		len = va_arg(ap, int);
		type = va_arg(ap, int);

		desired_size = sizeof(res_options) + sizeof(int) +
		    sizeof(enum nss_lookup_type) + sizeof(int) * 2 + len;

		if (desired_size > *buffer_size) {
			res = NS_RETURN;
			goto fin;
		}

		p = buffer;
		memcpy(p, &res_options, sizeof(res_options));
		p += sizeof(res_options);

		memcpy(p, &op_id, sizeof(int));
		p += sizeof(int);

		memcpy(p, &lookup_type, sizeof(enum nss_lookup_type));
		p += sizeof(int);

		memcpy(p, &type, sizeof(int));
		p += sizeof(int);

		memcpy(p, &len, sizeof(int));
		p += sizeof(int);

		memcpy(p, str, len);

		res = NS_SUCCESS;
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

fin:
	*buffer_size = desired_size;
	return (res);
}

static int
host_marshal_func(char *buffer, size_t *buffer_size, void *retval, va_list ap,
    void *cache_mdata)
{
	char *str;
	int len, type;
	struct hostent *ht;

	struct hostent new_ht;
	size_t desired_size, aliases_size, addr_size, size;
	char *p, **iter;

	switch ((enum nss_lookup_type)cache_mdata) {
	case nss_lt_name:
		str = va_arg(ap, char *);
		type = va_arg(ap, int);
		break;
	case nss_lt_id:
		str = va_arg(ap, char *);
		len = va_arg(ap, int);
		type = va_arg(ap, int);
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}
	ht = va_arg(ap, struct hostent *);

	desired_size = _ALIGNBYTES + sizeof(struct hostent) + sizeof(char *);
	if (ht->h_name != NULL)
		desired_size += strlen(ht->h_name) + 1;

	if (ht->h_aliases != NULL) {
		aliases_size = 0;
		for (iter = ht->h_aliases; *iter; ++iter) {
			desired_size += strlen(*iter) + 1;
			++aliases_size;
		}

		desired_size += _ALIGNBYTES +
		    (aliases_size + 1) * sizeof(char *);
	}

	if (ht->h_addr_list != NULL) {
		addr_size = 0;
		for (iter = ht->h_addr_list; *iter; ++iter)
			++addr_size;

		desired_size += addr_size * _ALIGN(ht->h_length);
		desired_size += _ALIGNBYTES + (addr_size + 1) * sizeof(char *);
	}

	if (desired_size > *buffer_size) {
		/* this assignment is here for future use */
		*buffer_size = desired_size;
		return (NS_RETURN);
	}

	memcpy(&new_ht, ht, sizeof(struct hostent));
	memset(buffer, 0, desired_size);

	*buffer_size = desired_size;
	p = buffer + sizeof(struct hostent) + sizeof(char *);
	memcpy(buffer + sizeof(struct hostent), &p, sizeof(char *));
	p = (char *)_ALIGN(p);

	if (new_ht.h_name != NULL) {
		size = strlen(new_ht.h_name);
		memcpy(p, new_ht.h_name, size);
		new_ht.h_name = p;
		p += size + 1;
	}

	if (new_ht.h_aliases != NULL) {
		p = (char *)_ALIGN(p);
		memcpy(p, new_ht.h_aliases, sizeof(char *) * aliases_size);
		new_ht.h_aliases = (char **)p;
		p += sizeof(char *) * (aliases_size + 1);

		for (iter = new_ht.h_aliases; *iter; ++iter) {
			size = strlen(*iter);
			memcpy(p, *iter, size);
			*iter = p;
			p += size + 1;
		}
	}

	if (new_ht.h_addr_list != NULL) {
		p = (char *)_ALIGN(p);
		memcpy(p, new_ht.h_addr_list, sizeof(char *) * addr_size);
		new_ht.h_addr_list = (char **)p;
		p += sizeof(char *) * (addr_size + 1);

		size = _ALIGN(new_ht.h_length);
		for (iter = new_ht.h_addr_list; *iter; ++iter) {
			memcpy(p, *iter, size);
			*iter = p;
			p += size + 1;
		}
	}
	memcpy(buffer, &new_ht, sizeof(struct hostent));
	return (NS_SUCCESS);
}

static int
host_unmarshal_func(char *buffer, size_t buffer_size, void *retval, va_list ap,
    void *cache_mdata)
{
	char *str;
	int len, type;
	struct hostent *ht;

	char *p;
	char **iter;
	char *orig_buf;
	size_t orig_buf_size;

	switch ((enum nss_lookup_type)cache_mdata) {
	case nss_lt_name:
		str = va_arg(ap, char *);
		type = va_arg(ap, int);
		break;
	case nss_lt_id:
		str = va_arg(ap, char *);
		len = va_arg(ap, int);
		type = va_arg(ap, int);
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

	ht = va_arg(ap, struct hostent *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);

	if (orig_buf_size <
	    buffer_size - sizeof(struct hostent) - sizeof(char *)) {
		errno = ERANGE;
		return (NS_RETURN);
	}

	memcpy(ht, buffer, sizeof(struct hostent));
	memcpy(&p, buffer + sizeof(struct hostent), sizeof(char *));

	orig_buf = (char *)_ALIGN(orig_buf);
	memcpy(orig_buf, buffer + sizeof(struct hostent) + sizeof(char *) +
	    _ALIGN(p) - (size_t)p,
	    buffer_size - sizeof(struct hostent) - sizeof(char *) -
	    _ALIGN(p) + (size_t)p);
	p = (char *)_ALIGN(p);

	NS_APPLY_OFFSET(ht->h_name, orig_buf, p, char *);
	if (ht->h_aliases != NULL) {
		NS_APPLY_OFFSET(ht->h_aliases, orig_buf, p, char **);

		for (iter = ht->h_aliases; *iter; ++iter)
			NS_APPLY_OFFSET(*iter, orig_buf, p, char *);
	}

	if (ht->h_addr_list != NULL) {
		NS_APPLY_OFFSET(ht->h_addr_list, orig_buf, p, char **);

		for (iter = ht->h_addr_list; *iter; ++iter)
			NS_APPLY_OFFSET(*iter, orig_buf, p, char *);
	}

	*((struct hostent **)retval) = ht;
	return (NS_SUCCESS);
}
#endif /* NS_CACHING */

void
sethostent(int stayopen)
{
	_sethosthtent(stayopen);
	_sethostdnsent(stayopen);
}

void
endhostent(void)
{
	_endhosthtent();
	_endhostdnsent();
}
