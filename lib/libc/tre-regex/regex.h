/*
 * Copyright (c) 2001-2009 Ville Laurikari <vl@iki.fi>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _REGEX_H_
#define	_REGEX_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <wchar.h>
#include <xlocale.h>

#define tre_regcomp   regcomp
#define tre_regcomp_l regcomp_l
#define tre_regexec   regexec
#define tre_regerror  regerror
#define tre_regfree   regfree

#define tre_regncomp  regncomp
#define tre_regncomp_l regncomp_l
#define tre_regnexec  regnexec
#define tre_regwcomp  regwcomp
#define tre_regwcomp_l regwcomp_l
#define tre_regwexec  regwexec
#define tre_regwncomp regwncomp
#define tre_regwncomp_l regwncomp_l
#define tre_regwnexec regwnexec

typedef enum {
#if __BSD_VISIBLE || __POSIX_VISIBLE <= 200112
  REG_ENOSYS = -1,	/* Reserved */
#endif
  REG_OK = 0,		/* No error. */
  REG_NOMATCH,		/* No match. */
  REG_BADPAT,		/* Invalid regexp. */
  REG_ECOLLATE,		/* Unknown collating element. */
  REG_ECTYPE,		/* Unknown character class name. */
  REG_EESCAPE,		/* Trailing backslash. */
  REG_ESUBREG,		/* Invalid back reference. */
  REG_EBRACK,		/* "[]" imbalance */
  REG_EPAREN,		/* "\(\)" or "()" imbalance */
  REG_EBRACE,		/* "\{\}" or "{}" imbalance */
  REG_BADBR,		/* Invalid content of {} */
  REG_ERANGE,		/* Invalid use of range operator */
  REG_ESPACE,		/* Out of memory.  */
  REG_BADRPT,           /* Invalid use of repetition operators. */
  REG_EMPTY,            /* rexexp was zero-length string */
  REG_INVARG,           /* invalid argument to regex routine */
  REG_ILLSEQ            /* illegal byte sequence */
} reg_errcode_t;

enum {
  TRE_CONFIG_APPROX,
  TRE_CONFIG_WCHAR,
  TRE_CONFIG_MULTIBYTE,
  TRE_CONFIG_SYSTEM_ABI,
  TRE_CONFIG_VERSION
};

typedef int regoff_t;
typedef wchar_t tre_char_t;

typedef struct {
  int re_magic;
  size_t re_nsub;  /* Number of parenthesized subexpressions. */
  const void *re_endp; /* regex string end pointer (REG_PEND) */
  void *value;	   /* For internal use only. */
} regex_t;

typedef struct {
  regoff_t rm_so;
  regoff_t rm_eo;
} regmatch_t;

/* Approximate matching parameter struct. */
typedef struct {
  int cost_ins;		/* Default cost of an inserted character. */
  int cost_del;		/* Default cost of a deleted character. */
  int cost_subst;	/* Default cost of a substituted character. */
  int max_cost;		/* Maximum allowed cost of a match. */

  int max_ins;		/* Maximum allowed number of inserts. */
  int max_del;		/* Maximum allowed number of deletes. */
  int max_subst;	/* Maximum allowed number of substitutes. */
  int max_err;		/* Maximum allowed number of errors total. */
} regaparams_t;

/* Approximate matching result struct. */
typedef struct {
  size_t nmatch;	/* Length of pmatch[] array. */
  regmatch_t *pmatch;	/* Submatch data. */
  int cost;		/* Cost of the match. */
  int num_ins;		/* Number of inserts in the match. */
  int num_del;		/* Number of deletes in the match. */
  int num_subst;	/* Number of substitutes in the match. */
} regamatch_t;

typedef struct {
  int (*get_next_char)(tre_char_t *c, unsigned int *pos_add, void *context);
  void (*rewind)(size_t pos, void *context);
  int (*compare)(size_t pos1, size_t pos2, size_t len, void *context);
  void *context;
} tre_str_source;

/* POSIX tre_regcomp() flags. */
#define REG_EXTENDED	1
#define REG_ICASE	(REG_EXTENDED << 1)
#define REG_NEWLINE	(REG_ICASE << 1)
#define REG_NOSUB	(REG_NEWLINE << 1)

/* Extra tre_regcomp() flags. */
#define REG_BASIC	0
#define REG_LITERAL	(REG_NOSUB << 1)
#define REG_RIGHT_ASSOC (REG_LITERAL << 1)
#define REG_UNGREEDY    (REG_RIGHT_ASSOC << 1)
#define REG_PEND	(REG_UNGREEDY << 1)
#define REG_ENHANCED	(REG_PEND << 1)

/* alias regcomp flags. */
#define REG_NOSPEC	REG_LITERAL
#define REG_MINIMAL	REG_UNGREEDY

/* POSIX tre_regexec() flags. */
#define REG_NOTBOL	1
#define REG_NOTEOL	(REG_NOTBOL << 1)
#define REG_STARTEND	(REG_NOTEOL << 1)
#define	REG_BACKR	(REG_STARTEND << 1)

/* Extra tre_regexec() flags. */
#define REG_APPROX_MATCHER	 (REG_NOTEOL << 1)
#define REG_BACKTRACKING_MATCHER (REG_APPROX_MATCHER << 1)

/* The maximum number of iterations in a bound expression. */
#define RE_DUP_MAX 255

#define _REG_nexec 1

__BEGIN_DECLS

/* The POSIX.2 regexp functions */
int
tre_regcomp(regex_t * __restrict preg, const char * __restrict regex,
    int cflags);

int
tre_regexec(const regex_t * __restrict preg, const char * __restrict string,
    size_t nmatch, regmatch_t pmatch[__restrict_arr], int eflags);

size_t
tre_regerror(int errcode, const regex_t * __restrict preg,
    char * __restrict errbuf, size_t errbuf_size);

void
tre_regfree(regex_t *preg);

/* Wide character versions (not in POSIX.2). */
int
tre_regwcomp(regex_t *preg, const wchar_t *regex, int cflags);

int
tre_regwexec(const regex_t *preg, const wchar_t *string,
	 size_t nmatch, regmatch_t pmatch[], int eflags);

/* Versions with a maximum length argument and therefore the capability to
   handle null characters in the middle of the strings (not in POSIX.2). */
int
tre_regncomp(regex_t *preg, const char *regex, size_t len, int cflags);

int
tre_regnexec(const regex_t *preg, const char *string, size_t len,
	 size_t nmatch, regmatch_t pmatch[], int eflags);

int
tre_regwncomp(regex_t *preg, const wchar_t *regex, size_t len, int cflags);

int
tre_regwnexec(const regex_t *preg, const wchar_t *string, size_t len,
	  size_t nmatch, regmatch_t pmatch[], int eflags);

/* Returns the version string.	The returned string is static. */
char *
tre_version(void);

/* Returns the value for a config parameter.  The type to which `result'
   must point to depends of the value of `query', see documentation for
   more details. */
int
tre_config(int query, void *result);

/* Returns 1 if the compiled pattern has back references, 0 if not. */
int
tre_have_backrefs(const regex_t *preg);

/* Returns 1 if the compiled pattern uses approximate matching features,
   0 if not. */
int
tre_have_approx(const regex_t *preg);
__END_DECLS

/* The POSIX.2 regexp functions, locale version */
int
tre_regcomp_l(regex_t *preg, const char *regex, int cflags, locale_t locale);

int
tre_regncomp_l(regex_t *preg, const char *regex, size_t len, int cflags,
    locale_t locale);

int
tre_regwcomp_l(regex_t *preg, const wchar_t *regex, int cflags,
    locale_t locale);

int
tre_regwncomp_l(regex_t *preg, const wchar_t *regex, size_t len, int cflags,
    locale_t locale);

#endif /* !_REGEX_H_ */
