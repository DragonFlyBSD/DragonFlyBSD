/* $NetBSD: citrus_module.c,v 1.5 2005/11/29 03:11:58 christos Exp $ */
/* $DragonFly: src/lib/libc/citrus/citrus_module.c,v 1.5 2008/04/10 10:21:01 hasso Exp $ */

/*-
 * Copyright (c)1999, 2000, 2001, 2002 Citrus Project,
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
 */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
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
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <stddef.h>
#include <paths.h>
#include <wchar.h>
#include "citrus_module.h"

#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>

#ifdef __PIC__

static int _getdewey(int [], char *);
static int _cmpndewey(int [], int, int [], int);
static const char *_findshlib(char *, int *, int *);

static const char *_pathI18nModule = NULL;

/* from libexec/ld.aout_so/shlib.c */
#undef major
#undef minor
#define MAXDEWEY	3	/*ELF*/

static int
_getdewey(int dewey[], char *cp)
{
	int	i, n;

	_DIAGASSERT(dewey != NULL);
	_DIAGASSERT(cp != NULL);

	for (n = 0, i = 0; i < MAXDEWEY; i++) {
		if (*cp == '\0')
			break;

		if (*cp == '.') cp++;
		if (*cp < '0' || '9' < *cp)
			return 0;

		dewey[n++] = (int)strtol(cp, &cp, 10);
	}

	return n;
}

/*
 * Compare two dewey arrays.
 * Return -1 if `d1' represents a smaller value than `d2'.
 * Return  1 if `d1' represents a greater value than `d2'.
 * Return  0 if equal.
 */
static int
_cmpndewey(int d1[], int n1, int d2[], int n2)
{
	int	i;

	_DIAGASSERT(d1 != NULL);
	_DIAGASSERT(d2 != NULL);

	for (i = 0; i < n1 && i < n2; i++) {
		if (d1[i] < d2[i])
			return -1;
		if (d1[i] > d2[i])
			return 1;
	}

	if (n1 == n2)
		return 0;

	if (i == n1)
		return -1;

	if (i == n2)
		return 1;

	/* XXX cannot happen */
	return 0;
}

static const char *
_findshlib(char *name, int *majorp, int *minorp)
{
	int		dewey[MAXDEWEY];
	int		ndewey;
	int		tmp[MAXDEWEY];
	int		i;
	int		len;
	char		*lname;
	static char	path[PATH_MAX];
	int		major, minor;
	const char	*search_dirs[1];
	const int	n_search_dirs = 1;

	_DIAGASSERT(name != NULL);
	_DIAGASSERT(majorp != NULL);
	_DIAGASSERT(minorp != NULL);

	major = *majorp;
	minor = *minorp;
	path[0] = '\0';
	search_dirs[0] = _pathI18nModule;
	len = strlen(name);
	lname = name;

	ndewey = 0;

	for (i = 0; i < n_search_dirs; i++) {
		DIR		*dd = opendir(search_dirs[i]);
		struct dirent	*dp;
		int		found_dot_a = 0;
		int		found_dot_so = 0;

		if (dd == NULL)
			continue;

		while ((dp = readdir(dd)) != NULL) {
			int	n;

			if (dp->d_namlen < len + 4)
				continue;
			if (strncmp(dp->d_name, lname, (size_t)len) != 0)
				continue;
			if (strncmp(dp->d_name+len, ".so.", 4) != 0)
				continue;

			if ((n = _getdewey(tmp, dp->d_name+len+4)) == 0)
				continue;

			if (major != -1 && found_dot_a)
				found_dot_a = 0;

			/* XXX should verify the library is a.out/ELF? */

			if (major == -1 && minor == -1) {
				goto compare_version;
			} else if (major != -1 && minor == -1) {
				if (tmp[0] == major)
					goto compare_version;
			} else if (major != -1 && minor != -1) {
				if (tmp[0] == major) {
					if (n == 1 || tmp[1] >= minor)
						goto compare_version;
				}
			}

			/* else, this file does not qualify */
			continue;

		compare_version:
			if (_cmpndewey(tmp, n, dewey, ndewey) <= 0)
				continue;

			/* We have a better version */
			found_dot_so = 1;
			snprintf(path, sizeof(path), "%s/%s", search_dirs[i],
			    dp->d_name);
			found_dot_a = 0;
			bcopy(tmp, dewey, sizeof(dewey));
			ndewey = n;
			*majorp = dewey[0];
			*minorp = dewey[1];
		}
		closedir(dd);

		if (found_dot_a || found_dot_so)
			/*
			 * There's a lib in this dir; take it.
			 */
			return path[0] ? path : NULL;
	}

	return path[0] ? path : NULL;
}

void *
_citrus_find_getops(_citrus_module_t handle, const char *modname,
		    const char *ifname)
{
	char name[PATH_MAX];
	void *p;

	_DIAGASSERT(handle != NULL);
	_DIAGASSERT(modname != NULL);
	_DIAGASSERT(ifname != NULL);

	snprintf(name, sizeof(name), "_citrus_%s_%s_getops", modname, ifname);
	p = dlsym((void *)handle, name);
	return p;
}

int
_citrus_load_module(_citrus_module_t *rhandle, const char *encname)
{
	const char *p;
	char path[PATH_MAX];
	int maj, min;
	void *handle;

	_DIAGASSERT(rhandle != NULL);

	if (_pathI18nModule == NULL) {
		p = getenv("PATH_I18NMODULE");
		if (p != NULL && !issetugid()) {
			_pathI18nModule = strdup(p);
			if (_pathI18nModule == NULL)
				return ENOMEM;
		} else
			_pathI18nModule = _PATH_I18NMODULE;
	}

	snprintf(path, sizeof(path), "lib%s", encname);
	maj = I18NMODULE_MAJOR;
	min = -1;
	p = _findshlib(path, &maj, &min);
	if (!p)
		return (EINVAL);
	handle = dlopen(p, RTLD_LAZY);
	if (!handle)
		return (EINVAL);

	*rhandle = (_citrus_module_t)handle;

	return (0);
}

void
_citrus_unload_module(_citrus_module_t handle)
{
	if (handle)
		dlclose((void *)handle);
}
#elif defined(_I18N_STATIC)
/*
 * Compiled-in multibyte locale support for statically linked programs.
 */
#include "citrus_ctype.h"
#include "sys/queue.h"
#include "citrus_types.h"
#include "citrus_hash.h"
#include "citrus_namespace.h"
#include "citrus_region.h"
#include "citrus_iconv_local.h"
#include "citrus_mapper_local.h"
#include "citrus_stdenc_local.h"
#include "modules/citrus_mapper_serial.h"
#include "modules/citrus_mapper_std.h"
#include "modules/citrus_mapper_none.h"
#include "modules/citrus_iconv_std.h"
#include "modules/citrus_utf1632.h"

#ifdef _I18N_STATIC_BIG5
#include "modules/citrus_big5.h"
#endif
#ifdef _I18N_STATIC_EUC
#include "modules/citrus_euc.h"
#endif
#ifdef _I18N_STATIC_EUCTW
#include "modules/citrus_euctw.h"
#endif
#ifdef _I18N_STATIC_ISO2022
#include "modules/citrus_iso2022.h"
#endif
#ifdef _I18N_STATIC_MSKanji
#include "modules/citrus_mskanji.h"
#endif
#ifdef _I18N_STATIC_UTF8
#include "modules/citrus_utf8.h"
#endif

#define _CITRUS_GETOPS_FUNC(_m_, _if_) _citrus_##_m_##_##_if_##_getops
/* only ctype is supported */
#define _CITRUS_LOCALE_TABLE_ENTRY(_n_) \
{ #_n_, "ctype", _CITRUS_GETOPS_FUNC(_n_, ctype) }

#define _CITRUS_MODULE_TABLE_ENTRY(_n_, _if_) \
{ #_n_, #_if_, _CITRUS_GETOPS_FUNC(_n_, _if_) }
/*
 * Table of compiled-in modules.
 */
struct citrus_metadata module_table[] = {
 _CITRUS_MODULE_TABLE_ENTRY(iconv_std, iconv),
 _CITRUS_MODULE_TABLE_ENTRY(mapper_std, mapper),
 _CITRUS_MODULE_TABLE_ENTRY(mapper_serial, mapper),
 _CITRUS_MODULE_TABLE_ENTRY(mapper_none, mapper),
 _CITRUS_MODULE_TABLE_ENTRY(UTF1632, stdenc),
#ifdef _I18N_STATIC_BIG5
 _CITRUS_LOCALE_TABLE_ENTRY(BIG5),
#endif
#ifdef _I18N_STATIC_EUC
 _CITRUS_LOCALE_TABLE_ENTRY(EUC),
#endif
#ifdef _I18N_STATIC_EUCTW
 _CITRUS_LOCALE_TABLE_ENTRY(EUCTW),
#endif
#ifdef _I18N_STATIC_ISO2022
 _CITRUS_LOCALE_TABLE_ENTRY(ISO2022),
#endif
#ifdef _I18N_STATIC_MSKanji
 _CITRUS_LOCALE_TABLE_ENTRY(MSKanji),
#endif
#ifdef _I18N_STATIC_UTF8
 _CITRUS_LOCALE_TABLE_ENTRY(UTF8),
#endif
 { NULL, NULL, NULL },
};

SET_DECLARE(citrus_set, struct citrus_metadata);

DATA_SET(citrus_set, module_table);

#define MAGIC_HANDLE	(void *)(0xC178C178)

void *
/*ARGSUSED*/
_citrus_find_getops(_citrus_module_t handle __unused, const char *modname,
		    const char *ifname)
{
	struct citrus_metadata **mdp, *mod;

	SET_FOREACH(mdp, citrus_set) {
		mod = *mdp;
		if (mod == NULL || mod->module_name == NULL || mod->interface_name == NULL)
			continue;
		if (strcmp(mod->module_name, modname) != 0)
			continue;
		if (strcmp(mod->interface_name, ifname) != 0)
			continue;	
		return(mod->module_ops);
	}
	return (NULL);
}

int
/*ARGSUSED*/
_citrus_load_module(_citrus_module_t *rhandle, char const *modname)
{
	struct citrus_metadata **mdp, *mod;

	SET_FOREACH(mdp, citrus_set) {
		mod = *mdp;
		if (mod == NULL || mod->module_name == NULL)
			continue;
		if (strcmp(mod->module_name, modname) != 0)
			continue;
		*rhandle = (_citrus_module_t)mod;
		return(0);
	}
	return (EINVAL);
}

void
/*ARGSUSED*/
_citrus_unload_module(_citrus_module_t handle __unused)
{
}
#else
SET_DECLARE(citrus_set, struct citrus_metadata);

struct citrus_metadata empty = {
    NULL, NULL, NULL
};

DATA_SET(citrus_set, empty);

#define MAGIC_HANDLE    (void *)(0xC178C178)

void *
/*ARGSUSED*/
_citrus_find_getops(_citrus_module_t handle __unused, const char *modname,
                    const char *ifname)
{
        struct citrus_metadata **mdp, *mod;

        _DIAGASSERT(handle == MAGIC_HANDLE);

        SET_FOREACH(mdp, citrus_set) {
                mod = *mdp;
                if (mod == NULL || mod->module_name == NULL || mod->interface_name == NULL)
                        continue;
                if (strcmp(mod->module_name, modname) != 0)
                        continue;
                if (strcmp(mod->interface_name, ifname) != 0)
                        continue;
                return(mod->module_ops);
        }
        return (NULL);
}

int
/*ARGSUSED*/
_citrus_load_module(_citrus_module_t *rhandle, char const *modname)
{
        struct citrus_metadata **mdp, *mod;

        SET_FOREACH(mdp, citrus_set) {
                mod = *mdp;
                if (mod == NULL || mod->module_name == NULL)
                        continue;
                if (strcmp(mod->module_name, modname) != 0)
                        continue;
                *rhandle = MAGIC_HANDLE;
                return(0);
        }
        return (EINVAL);
}

void
/*ARGSUSED*/
_citrus_unload_module(_citrus_module_t handle __unused)
{
}
#endif
