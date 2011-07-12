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

#define TC_OK	0
#define TC_ERR	-1

typedef struct tc_api_opts {
	/* Common fields */
	char		*tc_device;
	char		*tc_passphrase;
	const char	**tc_keyfiles;

	/* Fields for mapping */
	char		*tc_map_name;
	int		tc_password_retries;
	int		tc_interactive_prompt;
	unsigned long	tc_prompt_timeout;

	/* Fields for creation */
	char		*tc_cipher;
	char		*tc_prf_hash;
	char		*tc_cipher_hidden;
	char		*tc_prf_hash_hidden;
	size_t		tc_size_hidden_in_blocks;
	char		*tc_passphrase_hidden;
	const char	**tc_keyfiles_hidden;
} tc_api_opts;

int tc_api_init(int verbose);
int tc_api_uninit(void);
int tc_api_create_volume(tc_api_opts *api_opts);
int tc_api_map_volume(tc_api_opts *api_opts);
int tc_api_unmap_volume(tc_api_opts *api_opts);
int tc_api_check_cipher(tc_api_opts *api_opts);
int tc_api_check_prf_hash(tc_api_opts *api_opts);
const char *tc_api_get_error_msg(void);
const char *tc_api_get_summary(void);

