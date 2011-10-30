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

#define TBRIDGE_LOADTEST	_IOW('T', 0xBB, struct plistref)
#define TBRIDGE_GETRESULT	_IOR('T', 0xBC, struct plistref)

#if defined(_KERNEL)

typedef int (*tbridge_abort_t)(void);
typedef void (*tbridge_run_t)(void *);

struct tbridge_testcase {
	tbridge_abort_t	tb_abort;
	tbridge_run_t	tb_run;
};

int tbridge_printf(const char *fmt, ...);
void tbridge_test_done(int result);

/* safemem functions */
void *_alloc_safe_mem(size_t req_sz, const char *file, int line);
void _free_safe_mem(void *mem, const char *file, int line);
void check_and_purge_safe_mem(void);
void safemem_reset_error_count(void);
int safemem_get_error_count(void);

#define alloc_testcase_mem(x) \
	_alloc_safe_mem(x, __FILE__, __LINE__)

#define free_testcase_mem(x) \
	_free_safe_mem(x, __FILE__, __LINE__)



/* module magic */
int tbridge_testcase_modhandler(module_t mod, int type, void *arg);

#define TBRIDGE_TESTCASE_MODULE(name, testcase)			\
static moduledata_t name##_mod = {				\
	#name,							\
	tbridge_testcase_modhandler,				\
	testcase						\
};								\
DECLARE_MODULE(name, name##_mod, SI_SUB_DRIVERS, SI_ORDER_ANY)

#endif
