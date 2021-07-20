/*
 * Copyright (c) 2000-2002,2010,2013,2016,2020 by Solar Designer.  See LICENSE.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS /* we use fopen(), sprintf(), strncat() */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "passwdqc.h" /* also provides <pwd.h> or equivalent "struct passwd" */
#include "passwdqc_filter.h"
#include "wordset_4k.h"

#include "passwdqc_i18n.h"

#define REASON_ERROR \
	_("check failed")

#define REASON_SAME \
	_("is the same as the old one")
#define REASON_SIMILAR \
	_("is based on the old one")

#define REASON_SHORT \
	_("too short")
#define REASON_LONG \
	_("too long")

#define REASON_SIMPLESHORT \
	_("not enough different characters or classes for this length")
#define REASON_SIMPLE \
	_("not enough different characters or classes")

#define REASON_PERSONAL \
	_("based on personal login information")

#define REASON_WORD \
	_("based on a dictionary word and not a passphrase")

#define REASON_SEQ \
	_("based on a common sequence of characters and not a passphrase")

#define REASON_WORDLIST \
	_("based on a word list entry")

#define REASON_DENYLIST \
	_("is in deny list")

#define REASON_FILTER \
	_("appears to be in a database")

#define FIXED_BITS			15

typedef unsigned long fixed;

/*
 * Calculates the expected number of different characters for a random
 * password of a given length.  The result is rounded down.  We use this
 * with the _requested_ minimum length (so longer passwords don't have
 * to meet this strict requirement for their length).
 */
static int expected_different(int charset, int length)
{
	fixed x, y, z;

	x = ((fixed)(charset - 1) << FIXED_BITS) / charset;
	y = x;
	while (--length > 0)
		y = (y * x) >> FIXED_BITS;
	z = (fixed)charset * (((fixed)1 << FIXED_BITS) - y);

	return (int)(z >> FIXED_BITS);
}

/*
 * A password is too simple if it is too short for its class, or doesn't
 * contain enough different characters for its class, or doesn't contain
 * enough words for a passphrase.
 *
 * The biases are added to the length, and they may be positive or negative.
 * The passphrase length check uses passphrase_bias instead of bias so that
 * zero may be passed for this parameter when the (other) bias is non-zero
 * because of a dictionary word, which is perfectly normal for a passphrase.
 * The biases do not affect the number of different characters, character
 * classes, and word count.
 */
static int is_simple(const passwdqc_params_qc_t *params, const char *newpass,
    int bias, int passphrase_bias)
{
	int length, classes, words, chars;
	int digits, lowers, uppers, others, unknowns;
	int c, p;

	length = classes = words = chars = 0;
	digits = lowers = uppers = others = unknowns = 0;
	p = ' ';
	while ((c = (unsigned char)newpass[length])) {
		length++;

		if (!isascii(c))
			unknowns++;
		else if (isdigit(c))
			digits++;
		else if (islower(c))
			lowers++;
		else if (isupper(c))
			uppers++;
		else
			others++;

/* A word starts when a letter follows a non-letter or when a non-ASCII
 * character follows a space character.  We treat all non-ASCII characters
 * as non-spaces, which is not entirely correct (there's the non-breaking
 * space character at 0xa0, 0x9a, or 0xff), but it should not hurt. */
		if (isascii(p)) {
			if (isascii(c)) {
				if (isalpha(c) && !isalpha(p))
					words++;
			} else if (isspace(p))
				words++;
		}
		p = c;

/* Count this character just once: when we're not going to see it anymore */
		if (!strchr(&newpass[length], c))
			chars++;
	}

	if (!length)
		return 1;

/* Upper case characters and digits used in common ways don't increase the
 * strength of a password */
	c = (unsigned char)newpass[0];
	if (uppers && isascii(c) && isupper(c))
		uppers--;
	c = (unsigned char)newpass[length - 1];
	if (digits && isascii(c) && isdigit(c))
		digits--;

/* Count the number of different character classes we've seen.  We assume
 * that there are no non-ASCII characters for digits. */
	classes = 0;
	if (digits)
		classes++;
	if (lowers)
		classes++;
	if (uppers)
		classes++;
	if (others)
		classes++;
	if (unknowns && classes <= 1 && (!classes || digits || words >= 2))
		classes++;

	for (; classes > 0; classes--)
	switch (classes) {
	case 1:
		if (length + bias >= params->min[0] &&
		    chars >= expected_different(10, params->min[0]) - 1)
			return 0;
		return 1;

	case 2:
		if (length + bias >= params->min[1] &&
		    chars >= expected_different(36, params->min[1]) - 1)
			return 0;
		if (!params->passphrase_words ||
		    words < params->passphrase_words)
			continue;
		if (length + passphrase_bias >= params->min[2] &&
		    chars >= expected_different(27, params->min[2]) - 1)
			return 0;
		continue;

	case 3:
		if (length + bias >= params->min[3] &&
		    chars >= expected_different(62, params->min[3]) - 1)
			return 0;
		continue;

	case 4:
		if (length + bias >= params->min[4] &&
		    chars >= expected_different(95, params->min[4]) - 1)
			return 0;
		continue;
	}

	return 1;
}

static char *unify(char *dst, const char *src)
{
	const char *sptr;
	char *dptr;
	int c;

	if (!dst && !(dst = malloc(strlen(src) + 1)))
		return NULL;

	sptr = src;
	dptr = dst;
	do {
		c = (unsigned char)*sptr;
		if (isascii(c) && isupper(c))
			c = tolower(c);
		switch (c) {
		case 'a': case '@':
			c = '4'; break;
		case 'e':
			c = '3'; break;
/* Unfortunately, if we translate both 'i' and 'l' to '1', this would
 * associate these two letters with each other - e.g., "mile" would
 * match "MLLE", which is undesired.  To solve this, we'd need to test
 * different translations separately, which is not implemented yet. */
		case 'i': case '|':
			c = '!'; break;
		case 'l':
			c = '1'; break;
		case 'o':
			c = '0'; break;
		case 's': case '$':
			c = '5'; break;
		case 't': case '+':
			c = '7'; break;
		}
		*dptr++ = c;
	} while (*sptr++);

	return dst;
}

static char *reverse(const char *src)
{
	const char *sptr;
	char *dst, *dptr;

	if (!(dst = malloc(strlen(src) + 1)))
		return NULL;

	sptr = &src[strlen(src)];
	dptr = dst;
	while (sptr > src)
		*dptr++ = *--sptr;
	*dptr = '\0';

	return dst;
}

static void clean(char *dst)
{
	if (!dst)
		return;
	_passwdqc_memzero(dst, strlen(dst));
	free(dst);
}

/*
 * Needle is based on haystack if both contain a long enough common
 * substring and needle would be too simple for a password with the
 * substring either removed with partial length credit for it added
 * or partially discounted for the purpose of the length check.
 */
static int is_based(const passwdqc_params_qc_t *params,
    const char *haystack, const char *needle, const char *original,
    int mode)
{
	char *scratch;
	int length;
	int i, j;
	const char *p;
	int worst_bias;

	if (!params->match_length)	/* disabled */
		return 0;

	if (params->match_length < 0)	/* misconfigured */
		return 1;

	scratch = NULL;
	worst_bias = 0;

	length = (int)strlen(needle);
	for (i = 0; i <= length - params->match_length; i++)
	for (j = params->match_length; i + j <= length; j++) {
		int bias = 0, j1 = j - 1;
		const char q0 = needle[i], *q1 = &needle[i + 1];
		for (p = haystack; *p; p++)
		if (*p == q0 && !strncmp(p + 1, q1, j1)) { /* or memcmp() */
			if ((mode & 0xff) == 0) { /* remove & credit */
				if (!scratch) {
					if (!(scratch = malloc(length + 1)))
						return 1;
				}
				/* remove j chars */
				{
					int pos = length - (i + j);
					if (!(mode & 0x100)) /* not reversed */
						pos = i;
					memcpy(scratch, original, pos);
					memcpy(&scratch[pos],
					    &original[pos + j],
					    length + 1 - (pos + j));
				}
				/* add credit for match_length - 1 chars */
				bias = params->match_length - 1;
				if (is_simple(params, scratch, bias, bias)) {
					clean(scratch);
					return 1;
				}
			} else { /* discount */
/* Require a 1 character longer match for substrings containing leetspeak
 * when matching against dictionary words */
				bias = -1;
				if ((mode & 0xff) == 1) { /* words */
					int pos = i, end = i + j;
					if (mode & 0x100) { /* reversed */
						pos = length - end;
						end = length - i;
					}
					for (; pos < end; pos++)
					if (!isalpha((int)(unsigned char)
					    original[pos])) {
						if (j == params->match_length)
							goto next_match_length;
						bias = 0;
						break;
					}
				}

				/* discount j - (match_length + bias) chars */
				bias += (int)params->match_length - j;
				/* bias <= -1 */
				if (bias < worst_bias) {
					if (is_simple(params, original, bias,
					    (mode & 0xff) == 1 ? 0 : bias))
						return 1;
					worst_bias = bias;
				}
			}
		}
/* Zero bias implies that there were no matches for this length.  If so,
 * there's no reason to try the next substring length (it would result in
 * no matches as well).  We break out of the substring length loop and
 * proceed with all substring lengths for the next position in needle. */
		if (!bias)
			break;
next_match_length:
		;
	}

	clean(scratch);

	return 0;
}

#define READ_LINE_MAX 8192
#define READ_LINE_SIZE (READ_LINE_MAX + 2)

static char *read_line(FILE *f, char *buf)
{
	buf[READ_LINE_MAX] = '\n';

	if (!fgets(buf, READ_LINE_SIZE, f))
		return NULL;

	if (buf[READ_LINE_MAX] != '\n') {
		int c;
		do {
			c = getc(f);
		} while (c != EOF && c != '\n');
		if (ferror(f))
			return NULL;
	}

	char *p;
	if ((p = strpbrk(buf, "\r\n")))
		*p = '\0';

	return buf;
}

/*
 * Common sequences of characters.
 * We don't need to list any of the entire strings in reverse order because the
 * code checks the new password in both "unified" and "unified and reversed"
 * form against these strings (unifying them first indeed).  We also don't have
 * to include common repeats of characters (e.g., "777", "!!!", "1000") because
 * these are often taken care of by the requirement on the number of different
 * characters.
 */
const char * const seq[] = {
	"0123456789",
	"`1234567890-=",
	"~!@#$%^&*()_+",
	"abcdefghijklmnopqrstuvwxyz",
	"a1b2c3d4e5f6g7h8i9j0",
	"1a2b3c4d5e6f7g8h9i0j",
	"abc123",
	"qwertyuiop[]\\asdfghjkl;'zxcvbnm,./",
	"qwertyuiop{}|asdfghjkl:\"zxcvbnm<>?",
	"qwertyuiopasdfghjklzxcvbnm",
	"1qaz2wsx3edc4rfv5tgb6yhn7ujm8ik,9ol.0p;/-['=]\\",
	"!qaz@wsx#edc$rfv%tgb^yhn&ujm*ik<(ol>)p:?_{\"+}|",
	"qazwsxedcrfvtgbyhnujmikolp",
	"1q2w3e4r5t6y7u8i9o0p-[=]",
	"q1w2e3r4t5y6u7i8o9p0[-]=\\",
	"1qaz1qaz",
	"1qaz!qaz", /* can't unify '1' and '!' - see comment in unify() */
	"1qazzaq1",
	"zaq!1qaz",
	"zaq!2wsx"
};

/*
 * This wordlist check is now the least important given the checks above
 * and the support for passphrases (which are based on dictionary words,
 * and checked by other means).  It is still useful to trap simple short
 * passwords (if short passwords are allowed) that are word-based, but
 * passed the other checks due to uncommon capitalization, digits, and
 * special characters.  We (mis)use the same set of words that are used
 * to generate random passwords.  This list is much smaller than those
 * used for password crackers, and it doesn't contain common passwords
 * that aren't short English words.  We also support optional external
 * wordlist (for inexact matching) and deny list (for exact matching).
 */
static const char *is_word_based(const passwdqc_params_qc_t *params,
    const char *unified, const char *reversed, const char *original)
{
	const char *reason = REASON_ERROR;
	char word[WORDSET_4K_LENGTH_MAX + 1], *buf = NULL;
	FILE *f = NULL;
	unsigned int i;

	word[WORDSET_4K_LENGTH_MAX] = '\0';
	if (params->match_length)
	for (i = 0; _passwdqc_wordset_4k[i][0]; i++) {
		memcpy(word, _passwdqc_wordset_4k[i], WORDSET_4K_LENGTH_MAX);
		int length = (int)strlen(word);
		if (length < params->match_length)
			continue;
		if (!memcmp(word, _passwdqc_wordset_4k[i + 1], length))
			continue;
		unify(word, word);
		if (is_based(params, word, unified, original, 1) ||
		    is_based(params, word, reversed, original, 0x101)) {
			reason = REASON_WORD;
			goto out;
		}
	}

	if (params->match_length)
	for (i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
		char *seq_i = unify(NULL, seq[i]);
		if (!seq_i)
			goto out;
		if (is_based(params, seq_i, unified, original, 2) ||
		    is_based(params, seq_i, reversed, original, 0x102)) {
			clean(seq_i);
			reason = REASON_SEQ;
			goto out;
		}
		clean(seq_i);
	}

	if (params->match_length && params->match_length <= 4)
	for (i = 1900; i <= 2039; i++) {
		sprintf(word, "%u", i);
		if (is_based(params, word, unified, original, 2) ||
		    is_based(params, word, reversed, original, 0x102)) {
			reason = REASON_SEQ;
			goto out;
		}
	}

	if (params->wordlist || params->denylist)
		if (!(buf = malloc(READ_LINE_SIZE)))
			goto out;

	if (params->wordlist) {
		if (!(f = fopen(params->wordlist, "r")))
			goto out;
		while (read_line(f, buf)) {
			unify(buf, buf);
			if (!strcmp(buf, unified) || !strcmp(buf, reversed))
				goto out_wordlist;
			if (!params->match_length ||
			    strlen(buf) < (size_t)params->match_length)
				continue;
			if (is_based(params, buf, unified, original, 1) ||
			    is_based(params, buf, reversed, original, 0x101)) {
out_wordlist:
				reason = REASON_WORDLIST;
				goto out;
			}
		}
		if (ferror(f))
			goto out;
		fclose(f); f = NULL;
	}

	if (params->denylist) {
		if (!(f = fopen(params->denylist, "r")))
			goto out;
		while (read_line(f, buf)) {
			if (!strcmp(buf, original)) {
				reason = REASON_DENYLIST;
				goto out;
			}
		}
		if (ferror(f))
			goto out;
	}

	reason = NULL;

out:
	if (f)
		fclose(f);
	if (buf) {
		_passwdqc_memzero(buf, READ_LINE_SIZE);
		free(buf);
	}
	_passwdqc_memzero(word, sizeof(word));
	return reason;
}

const char *passwdqc_check(const passwdqc_params_qc_t *params,
    const char *newpass, const char *oldpass, const struct passwd *pw)
{
	char truncated[9];
	char *u_newpass = NULL, *u_reversed = NULL;
	char *u_oldpass = NULL;
	char *u_name = NULL, *u_gecos = NULL, *u_dir = NULL;
	const char *reason = REASON_ERROR;

	size_t length = strlen(newpass);

	if (length < (size_t)params->min[4]) {
		reason = REASON_SHORT;
		goto out;
	}

	if (length > 10000) {
		reason = REASON_LONG;
		goto out;
	}

	if (length > (size_t)params->max) {
		if (params->max == 8) {
			truncated[0] = '\0';
			strncat(truncated, newpass, 8);
			newpass = truncated;
			length = 8;
			if (oldpass && !strncmp(oldpass, newpass, 8)) {
				reason = REASON_SAME;
				goto out;
			}
		} else {
			reason = REASON_LONG;
			goto out;
		}
	}

	if (oldpass && !strcmp(oldpass, newpass)) {
		reason = REASON_SAME;
		goto out;
	}

	if (is_simple(params, newpass, 0, 0)) {
		reason = REASON_SIMPLE;
		if (length < (size_t)params->min[1] &&
		    params->min[1] <= params->max)
			reason = REASON_SIMPLESHORT;
		goto out;
	}

	if (!(u_newpass = unify(NULL, newpass)))
		goto out; /* REASON_ERROR */
	if (!(u_reversed = reverse(u_newpass)))
		goto out;
	if (oldpass && !(u_oldpass = unify(NULL, oldpass)))
		goto out;
	if (pw) {
		if (!(u_name = unify(NULL, pw->pw_name)) ||
		    !(u_gecos = unify(NULL, pw->pw_gecos)) ||
		    !(u_dir = unify(NULL, pw->pw_dir)))
			goto out;
	}

	if (oldpass && params->similar_deny &&
	    (is_based(params, u_oldpass, u_newpass, newpass, 0) ||
	     is_based(params, u_oldpass, u_reversed, newpass, 0x100))) {
		reason = REASON_SIMILAR;
		goto out;
	}

	if (pw &&
	    (is_based(params, u_name, u_newpass, newpass, 0) ||
	     is_based(params, u_name, u_reversed, newpass, 0x100) ||
	     is_based(params, u_gecos, u_newpass, newpass, 0) ||
	     is_based(params, u_gecos, u_reversed, newpass, 0x100) ||
	     is_based(params, u_dir, u_newpass, newpass, 0) ||
	     is_based(params, u_dir, u_reversed, newpass, 0x100))) {
		reason = REASON_PERSONAL;
		goto out;
	}

	reason = is_word_based(params, u_newpass, u_reversed, newpass);

	if (!reason && params->filter) {
		passwdqc_filter_t flt;
		reason = REASON_ERROR;
		if (passwdqc_filter_open(&flt, params->filter))
			goto out;
		int result = passwdqc_filter_lookup(&flt, newpass);
		passwdqc_filter_close(&flt);
		if (result < 0)
			goto out;
		reason = result ? REASON_FILTER : NULL;
	}

out:
	_passwdqc_memzero(truncated, sizeof(truncated));
	clean(u_newpass);
	clean(u_reversed);
	clean(u_oldpass);
	clean(u_name);
	clean(u_gecos);
	clean(u_dir);

	return reason;
}
