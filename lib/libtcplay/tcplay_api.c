/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "tcplay.h"
#include "tcplay_api.h"
#include "tcplay_api_internal.h"


int
tc_api_init(int verbose)
{
	int error;

	tc_internal_verbose = verbose;

	if ((error = tc_play_init()) != 0)
		return TC_ERR;
	else
		return TC_OK;
}

int
tc_api_uninit(void)
{
	check_and_purge_safe_mem();
	return TC_OK;
}


static const char *_caps[] = {
	"trim",
	NULL
};

int
tc_api_has(const char *feature)
{
	const char *cap;
	int i;

	for (cap = _caps[0], i = 0; cap != NULL; cap = _caps[++i]) {
		if ((strcmp(cap, feature)) == 0)
			return TC_OK;
	}

	return TC_ERR_UNIMPL;
}

int
tc_api_cipher_iterate(tc_api_cipher_iterator_fn fn, void *priv)
{
	int i;
	struct tc_cipher_chain *chain;
	int klen;
	int length;
	char buf[1024];

	if (fn == NULL) {
		errno = EFAULT;
		return TC_ERR;
	}

	for (i = 0, chain = tc_cipher_chains[0]; chain != NULL;
	     chain = tc_cipher_chains[++i]) {
		tc_cipher_chain_sprint(buf, sizeof(buf), chain);
		klen = tc_cipher_chain_klen(chain);
		length = tc_cipher_chain_length(chain);
		if ((fn(priv, buf, klen, length)) < 0)
			break;
	}

	return TC_OK;
}

int
tc_api_prf_iterate(tc_api_prf_iterator_fn fn, void *priv)
{
	int i;

	if (fn == NULL) {
		errno = EFAULT;
		return TC_ERR;
	}

	/* start at 1 due to RIPEMD weirdness... */
	for (i = 1; pbkdf_prf_algos[i].name != NULL; i++) {
		if ((fn(priv, pbkdf_prf_algos[i].name)) < 0)
			break;
	}

	return TC_OK;
}


const char *
tc_api_task_get_error(tc_api_task task __unused)
{
	return tc_internal_log_buffer;
}


#define _match(k, v) (strcmp(k, v) == 0)

tc_api_task
tc_api_task_init(const char *op)
{
	tc_api_task task = NULL;
	int fail = 1;

	if ((task = alloc_safe_mem(sizeof(*task))) == NULL) {
		errno = ENOMEM;
		goto out;
	}

	if ((task->opts = opts_init()) == NULL) {
		errno = ENOMEM;
		goto out;
	}

	if (_match(op, "create")) {
		task->op = TC_OP_CREATE;
	} else if (_match(op, "map")) {
		task->op = TC_OP_MAP;
	} else if (_match(op, "unmap")) {
		task->op = TC_OP_UNMAP;
	} else if (_match(op, "info")) {
		task->op = TC_OP_INFO;
	} else if (_match(op, "info_mapped")) {
		task->op = TC_OP_INFO_MAPPED;
	} else if (_match(op, "modify")) {
		task->op = TC_OP_MODIFY;
	} else if (_match(op, "restore")) {
		task->op = TC_OP_RESTORE;
	} else {
		errno = EINVAL;
		goto out;
	}

	fail = 0;

out:
	if (fail && task != NULL) {
		if (task->opts != NULL)
			opts_free(task->opts);
		free_safe_mem(task);
	}

	return fail ? NULL : task;
}

int
tc_api_task_uninit(tc_api_task task)
{
	if (task->last_info != NULL)
		free_info(task->last_info);
	opts_free(task->opts);
	free_safe_mem(task);

	return TC_OK;
}


#define _set_str(k) \
	do {							\
		if ((opts->k = strdup_safe_mem(s)) == NULL) {	\
			errno = ENOMEM;				\
			r = TC_ERR;				\
			goto out;				\
		}						\
	} while (0)

#define _clr_str(k) \
	do {							\
		if (opts->k)					\
			free_safe_mem(opts->k);			\
		opts->k = NULL;					\
	} while (0)

int
tc_api_task_set(tc_api_task task, const char *key, ...)
{
	struct tcplay_opts *opts;
	va_list ap;
	const char *s;
	int64_t i64;
	int i;
	tc_api_state_change_fn sc_fn;
	void *vp;
	int r = TC_OK;

	if (task == NULL || ((opts = task->opts) == NULL)) {
		errno = EFAULT;
		return TC_ERR;
	}

	va_start(ap, key);

	if (_match(key, "interactive")) {
		i = va_arg(ap, int);
		opts->interactive = i;
	} else if (_match(key, "weak_keys_and_salt")) {
		i = va_arg(ap, int);
		opts->weak_keys_and_salt = i;
	} else if (_match(key, "secure_erase")) {
		i = va_arg(ap, int);
		opts->secure_erase = i;
	} else if (_match(key, "protect_hidden")) {
		i = va_arg(ap, int);
		opts->protect_hidden = i;
	} else if (_match(key, "fde")) {
		i = va_arg(ap, int);
		if (i)
			opts->flags |= TC_FLAG_FDE;
		else
			opts->flags &= ~TC_FLAG_FDE;
	} else if (_match(key, "use_backup_header")) {
		i = va_arg(ap, int);
		if (i)
			opts->flags |= TC_FLAG_BACKUP;
		else
			opts->flags &= ~TC_FLAG_BACKUP;
	} else if (_match(key, "allow_trim")) {
		i = va_arg(ap, int);
		if (i)
			opts->flags |= TC_FLAG_ALLOW_TRIM;
		else
			opts->flags &= ~TC_FLAG_ALLOW_TRIM;
	} else if (_match(key, "hidden_size_bytes")) {
		i64 = va_arg(ap, int64_t);
		opts->hidden_size_bytes = (disksz_t)i64;
		opts->hidden = (i64 > 0);
	} else if (_match(key, "retries")) {
		i = va_arg(ap, int);
		opts->retries = i;
	} else if (_match(key, "timeout")) {
		i = va_arg(ap, int);
		opts->timeout = (time_t)i;
	} else if (_match(key, "save_header_to_file")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			_set_str(hdr_file_out);
			opts->flags |= TC_FLAG_SAVE_TO_FILE;
		} else {
			_clr_str(hdr_file_out);
			opts->flags &= ~TC_FLAG_SAVE_TO_FILE;
		}
	} else if (_match(key, "header_from_file")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			_set_str(hdr_file_in);
			opts->flags |= TC_FLAG_HDR_FROM_FILE;
		} else {
			_clr_str(hdr_file_in);
			opts->flags &= ~TC_FLAG_HDR_FROM_FILE;
		}
	} else if (_match(key, "hidden_header_from_file")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			_set_str(h_hdr_file_in);
			opts->flags |= TC_FLAG_H_HDR_FROM_FILE;
		} else {
			_clr_str(h_hdr_file_in);
			opts->flags &= ~TC_FLAG_H_HDR_FROM_FILE;
		}
	} else if (_match(key, "sys")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			_set_str(sys_dev);
			opts->flags |= TC_FLAG_SYS;
		} else {
			_clr_str(sys_dev);
			opts->flags &= ~TC_FLAG_SYS;
		}
	} else if (_match(key, "passphrase")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			_set_str(passphrase);
		} else {
			_clr_str(passphrase);
		}
	} else if (_match(key, "h_passphrase")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			_set_str(h_passphrase);
		} else {
			_clr_str(h_passphrase);
		}
	} else if (_match(key, "new_passphrase")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			_set_str(new_passphrase);
		} else {
			_clr_str(new_passphrase);
		}
	} else if (_match(key, "dev")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			_set_str(dev);
		} else {
			_clr_str(dev);
		}
	} else if (_match(key, "map_name")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			_set_str(map_name);
		} else {
			_clr_str(map_name);
		}
	} else if (_match(key, "keyfiles")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			opts_add_keyfile(opts, s);
		} else {
			opts_clear_keyfile(opts);
		}
	} else if (_match(key, "h_keyfiles")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			opts_add_keyfile_hidden(opts, s);
		} else {
			opts_clear_keyfile_hidden(opts);
		}
	} else if (_match(key, "new_keyfiles")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			opts_add_keyfile_new(opts, s);
		} else {
			opts_clear_keyfile_new(opts);
		}
	} else if (_match(key, "prf_algo")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			if ((opts->prf_algo = check_prf_algo(s, 1)) == NULL) {
				errno = ENOENT;
				r = TC_ERR;
				goto out;
			}
		} else {
			opts->prf_algo = NULL;
		}
	} else if (_match(key, "h_prf_algo")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			if ((opts->h_prf_algo = check_prf_algo(s, 1)) == NULL) {
				errno = ENOENT;
				r = TC_ERR;
				goto out;
			}
		} else {
			opts->h_prf_algo = NULL;
		}
	} else if (_match(key, "new_prf_algo")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			if ((opts->new_prf_algo = check_prf_algo(s, 1)) == NULL) {
				errno = ENOENT;
				r = TC_ERR;
				goto out;
			}
		} else {
			opts->new_prf_algo = NULL;
		}
	} else if (_match(key, "cipher_chain")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			if ((opts->cipher_chain = check_cipher_chain(s, 1)) == NULL) {
				errno = ENOENT;
				r = TC_ERR;
				goto out;
			}
		} else {
			opts->cipher_chain = NULL;
		}
	} else if (_match(key, "h_cipher_chain")) {
		s = va_arg(ap, const char *);
		if (s != NULL) {
			if ((opts->h_cipher_chain = check_cipher_chain(s, 1)) == NULL) {
				errno = ENOENT;
				r = TC_ERR;
				goto out;
			}
		} else {
			opts->h_cipher_chain = NULL;
		}
	} else if (_match(key, "state_change_fn")) {
		sc_fn = va_arg(ap, tc_api_state_change_fn);
		opts->state_change_fn = sc_fn;
		vp = va_arg(ap, void *);
		opts->api_ctx = vp;
	} else {
		r = TC_ERR_UNIMPL;
	}

out:
	va_end(ap);

	return r;
}

#define _not_null(x) \
	if (opts->x == NULL) {	\
		return -1;	\
	}

#define _null(x) \
	if (opts->x != NULL) {	\
		return -1;	\
	}

#define _zero(x) \
	if (opts->x != 0) {	\
		return -1;	\
	}

#define _not_set(x) \
	if (TC_FLAG_SET(opts->flags, x)) {	\
		return -1;			\
	}

static
int
_opts_check_create(struct tcplay_opts *opts)
{
	_not_null(dev);
	_not_set(SYS);
	_not_set(FDE);
	_not_set(BACKUP);
	_not_set(ONLY_RESTORE);
	_not_set(ALLOW_TRIM);
	_not_set(SAVE_TO_FILE);
	_not_set(HDR_FROM_FILE);
	_not_set(H_HDR_FROM_FILE);

	_null(map_name);
	_zero(protect_hidden);
	_null(new_passphrase);
	_null(new_prf_algo);
	_zero(n_newkeyfiles);

	if (opts->hidden_size_bytes && !opts->hidden) {
		return -1;
	}

	return 0;
}

static
int
_opts_check_map(struct tcplay_opts *opts)
{
	_not_null(dev);
	_not_null(map_name);
	_not_set(ONLY_RESTORE);
	_not_set(SAVE_TO_FILE);
	_zero(hidden);
	_zero(hidden_size_bytes);
	_null(new_passphrase);
	_null(new_prf_algo);
	_zero(n_newkeyfiles);
	_null(prf_algo);
	_null(h_prf_algo);
	_null(cipher_chain);
	_null(h_cipher_chain);

	if (!opts->protect_hidden) {
		_zero(n_hkeyfiles);
		//_null(h_passphrase);
	}

	return 0;
}

static
int
_opts_check_unmap(struct tcplay_opts *opts)
{
	_not_null(map_name);
	/* XXX: _not_null(dev); ? */
	_zero(nkeyfiles);
	_zero(n_hkeyfiles);
	_null(prf_algo);
	_null(cipher_chain);
	_null(h_prf_algo);
	_null(h_cipher_chain);
	_null(passphrase);
	_null(h_passphrase);
	_zero(hidden);
	_zero(protect_hidden);
	_null(new_prf_algo);
	_null(new_passphrase);
	_zero(n_newkeyfiles);
	_not_set(SYS);
	_not_set(FDE);
	_not_set(BACKUP);
	_not_set(ONLY_RESTORE);
	_not_set(ALLOW_TRIM);
	_not_set(SAVE_TO_FILE);
	_not_set(HDR_FROM_FILE);
	_not_set(H_HDR_FROM_FILE);

	return 0;
}

static
int
_opts_check_info(struct tcplay_opts *opts)
{
	_not_null(dev);
	_null(map_name);
	_not_set(ONLY_RESTORE);
	_not_set(SAVE_TO_FILE);
	_zero(hidden);
	_zero(hidden_size_bytes);
	_null(new_passphrase);
	_null(new_prf_algo);
	_zero(n_newkeyfiles);
	_null(prf_algo);
	_null(h_prf_algo);
	_null(cipher_chain);
	_null(h_cipher_chain);

	if (!opts->protect_hidden) {
		_zero(n_hkeyfiles);
		//_null(h_passphrase);
	}

	return 0;
}

static
int
_opts_check_info_mapped(struct tcplay_opts *opts)
{
	_not_null(map_name);
	/* XXX: _not_null(dev); ? */
	_zero(nkeyfiles);
	_zero(n_hkeyfiles);
	_null(prf_algo);
	_null(cipher_chain);
	_null(h_prf_algo);
	_null(h_cipher_chain);
	_null(passphrase);
	_null(h_passphrase);
	_zero(hidden);
	_zero(protect_hidden);
	_null(new_prf_algo);
	_null(new_passphrase);
	_zero(n_newkeyfiles);
	_not_set(SYS);
	_not_set(FDE);
	_not_set(BACKUP);
	_not_set(ONLY_RESTORE);
	_not_set(ALLOW_TRIM);
	_not_set(SAVE_TO_FILE);
	_not_set(HDR_FROM_FILE);
	_not_set(H_HDR_FROM_FILE);

	return 0;
}

static
int
_opts_check_modify(struct tcplay_opts *opts)
{
	_not_null(dev);
	_null(map_name);
	_zero(hidden);
	_zero(hidden_size_bytes);
	_null(prf_algo);
	_null(h_prf_algo);
	_null(cipher_chain);
	_null(h_cipher_chain);

	if (!opts->protect_hidden) {
		_zero(n_hkeyfiles);
		_null(h_passphrase);
	}

	return 0;
}


static
int
_opts_check_restore(struct tcplay_opts *opts)
{
	if ((_opts_check_modify(opts)) < 0)
		return -1;

	_null(new_prf_algo);
	_zero(n_newkeyfiles);
	_null(new_passphrase);

	return 0;
}

int
tc_api_task_do(tc_api_task task)
{
	struct tcplay_opts *opts;
	int r = TC_OK;

	if (task == NULL || ((opts = task->opts) == NULL)) {
		errno = EFAULT;
		return TC_ERR;
	}

	if (task->last_info != NULL) {
		free_info(task->last_info);
	}

	switch (task->op) {
	case TC_OP_CREATE:
		if ((r = _opts_check_create(task->opts)) != 0) {
			errno = EINVAL;
			return r;
		}
		r = create_volume(opts);
		break;

	case TC_OP_MAP:
		if ((r = _opts_check_map(task->opts)) != 0) {
			errno = EINVAL;
			return r;
		}
		r = map_volume(opts);
		break;

	case TC_OP_UNMAP:
		if ((r = _opts_check_unmap(task->opts)) != 0) {
			errno = EINVAL;
			return r;
		}
		r = dm_teardown(opts->map_name, opts->dev);
		break;

	case TC_OP_INFO:
		if ((r = _opts_check_info(task->opts)) != 0) {
			errno = EINVAL;
			return r;
		}
		if ((task->last_info = info_map_common(opts, NULL)) == NULL) {
			r = TC_ERR;
		}
		break;

	case TC_OP_INFO_MAPPED:
		if ((r = _opts_check_info_mapped(task->opts)) != 0) {
			errno = EINVAL;
			return r;
		}
		if ((task->last_info = dm_info_map(opts->map_name)) == NULL) {
			r = TC_ERR;
		}
		break;

	case TC_OP_MODIFY:
		if ((r = _opts_check_modify(task->opts)) != 0) {
			errno = EINVAL;
			return r;
		}
		r = modify_volume(opts);
		break;

	case TC_OP_RESTORE:
		if ((r = _opts_check_restore(task->opts)) != 0) {
			errno = EINVAL;
			return r;
		}
		opts->flags |= TC_FLAG_ONLY_RESTORE;
		r = modify_volume(opts);
		opts->flags &= ~TC_FLAG_ONLY_RESTORE;
		break;
	}

	return r;
}


int
tc_api_task_info_get(tc_api_task task, const char *key, ...)
{
	char buf[1024];
	va_list ap;
	struct tcplay_info *info;
	char *s;
	int *ip;
	int64_t *i64p;
	int r = TC_OK;
	size_t sz;

	if (task == NULL || ((info = task->last_info) == NULL)) {
		errno = EFAULT;
		return TC_ERR;
	}

	va_start(ap, key);
	sz = va_arg(ap, size_t);
	if (sz < 1) {
		errno = EINVAL;
		r = TC_ERR;
		goto out;
	}

	if (_match(key, "device")) {
		s = va_arg(ap, char *);
		strncpy(s, info->dev, sz);
		s[sz-1] = '\0';
	} else if (_match(key, "cipher")) {
		s = va_arg(ap, char *);
		tc_cipher_chain_sprint(buf, sizeof(buf), info->cipher_chain);
		strncpy(s, buf, sz);
		s[sz-1] = '\0';
	} else if (_match(key, "prf")) {
		s = va_arg(ap, char *);
		if (info->pbkdf_prf)
			strncpy(s, info->pbkdf_prf->name, sz);
		else
			strncpy(s, "(unknown)", sz);
		s[sz-1] = '\0';
	} else if (_match(key, "key_bits")) {
		if (sz != sizeof(int)) {
			errno = EFAULT;
			r = TC_ERR;
			goto out;
		}
		ip = va_arg(ap, int *);
		*ip = 8*tc_cipher_chain_klen(info->cipher_chain);
	} else if (_match(key, "size")) {
		if (sz != sizeof(int64_t)) {
			errno = EFAULT;
			r = TC_ERR;
			goto out;
		}
		i64p = va_arg(ap, int64_t *);
		if (info->hdr)
			*i64p = (int64_t)info->size * (int64_t)info->hdr->sec_sz;
		else
			*i64p = (int64_t)info->size * (int64_t)info->blk_sz;
	} else if (_match(key, "iv_offset")) {
		if (sz != sizeof(int64_t)) {
			errno = EFAULT;
			r = TC_ERR;
			goto out;
		}
		i64p = va_arg(ap, int64_t *);
		if (info->hdr)
			*i64p = (int64_t)info->skip * (int64_t)info->hdr->sec_sz;
		else
			*i64p = (int64_t)info->skip * (int64_t)info->blk_sz;
	} else if (_match(key, "block_offset")) {
		if (sz != sizeof(int64_t)) {
			errno = EFAULT;
			r = TC_ERR;
			goto out;
		}
		i64p = va_arg(ap, int64_t *);
		if (info->hdr)
			*i64p = (int64_t)info->offset * (int64_t)info->hdr->sec_sz;
		else
			*i64p = (int64_t)info->offset * (int64_t)info->blk_sz;
	} else {
		r = TC_ERR_UNIMPL;
	}

out:
	va_end(ap);

	return r;
}
