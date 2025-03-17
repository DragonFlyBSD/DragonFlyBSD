/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (C) 2015-2021 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2021 Matt Dunwoodie <ncon@noconroy.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/objcache.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#include <crypto/chachapoly.h>
#include <crypto/blake2/blake2s.h>
#include <crypto/siphash/siphash.h>

#include "wg_cookie.h"

/* Constants for cookies */
#define COOKIE_KEY_SIZE		BLAKE2S_KEY_SIZE
#define COOKIE_SECRET_SIZE	32
#define COOKIE_MAC1_KEY_LABEL	"mac1----"
#define COOKIE_COOKIE_KEY_LABEL	"cookie--"
#define COOKIE_SECRET_MAX_AGE	120
#define COOKIE_SECRET_LATENCY	5

/* Constants for initiation rate limiting */
#define RATELIMIT_SIZE		(1 << 13)
#define RATELIMIT_MASK		(RATELIMIT_SIZE - 1)
#define RATELIMIT_SIZE_MAX	(RATELIMIT_SIZE * 8)
#define NSEC_PER_SEC		1000000000LL
#define INITIATIONS_PER_SECOND	20
#define INITIATIONS_BURSTABLE	5
#define INITIATION_COST		(NSEC_PER_SEC / INITIATIONS_PER_SECOND)
#define TOKEN_MAX		(INITIATION_COST * INITIATIONS_BURSTABLE)
#define ELEMENT_TIMEOUT		1 /* second */
#define IPV4_MASK_SIZE		4 /* Use all 4 bytes of IPv4 address */
#define IPV6_MASK_SIZE		8 /* Use top 8 bytes (/64) of IPv6 address */

struct cookie_maker {
	uint8_t		cm_mac1_key[COOKIE_KEY_SIZE];
	uint8_t		cm_cookie_key[COOKIE_KEY_SIZE];

	struct lock	cm_lock;
	bool		cm_cookie_valid;
	uint8_t		cm_cookie[COOKIE_COOKIE_SIZE];
	struct timespec	cm_cookie_birthdate;	/* nanouptime */
	bool		cm_mac1_sent;
	uint8_t		cm_mac1_last[COOKIE_MAC_SIZE];
};

struct cookie_checker {
	struct lock	cc_key_lock;
	uint8_t		cc_mac1_key[COOKIE_KEY_SIZE];
	uint8_t		cc_cookie_key[COOKIE_KEY_SIZE];

	struct lock	cc_secret_mtx;
	struct timespec	cc_secret_birthdate;	/* nanouptime */
	uint8_t		cc_secret[COOKIE_SECRET_SIZE];
};

struct ratelimit_key {
	uint8_t ip[IPV6_MASK_SIZE];
};

struct ratelimit_entry {
	LIST_ENTRY(ratelimit_entry)	r_entry;
	struct ratelimit_key		r_key;
	struct timespec			r_last_time;	/* nanouptime */
	uint64_t			r_tokens;
};

struct ratelimit {
	uint8_t				rl_secret[SIPHASH_KEY_LENGTH];
	struct lock			rl_mtx;
	struct callout			rl_gc;
	LIST_HEAD(, ratelimit_entry)	rl_table[RATELIMIT_SIZE];
	size_t				rl_table_num;
	bool				rl_initialized;
};


static void	macs_mac1(struct cookie_macs *, const void *, size_t,
			  const uint8_t[COOKIE_KEY_SIZE]);
static void	macs_mac2(struct cookie_macs *, const void *, size_t,
			  const uint8_t[COOKIE_COOKIE_SIZE]);
static void	make_cookie(struct cookie_checker *,
			    uint8_t[COOKIE_COOKIE_SIZE],
			    const struct sockaddr *);
static void	precompute_key(uint8_t[COOKIE_KEY_SIZE],
			       const uint8_t[COOKIE_INPUT_SIZE],
			       const uint8_t *, size_t);
static void	ratelimit_init(struct ratelimit *);
static void	ratelimit_deinit(struct ratelimit *);
static void	ratelimit_gc_callout(void *);
static void	ratelimit_gc_schedule(struct ratelimit *);
static void	ratelimit_gc(struct ratelimit *, bool);
static int	ratelimit_allow(struct ratelimit *, const struct sockaddr *);


static struct ratelimit ratelimit_v4;
#ifdef INET6
static struct ratelimit ratelimit_v6;
#endif

static struct objcache *ratelimit_zone;
static MALLOC_DEFINE(M_WG_RATELIMIT, "WG ratelimit", "wireguard ratelimit");
static MALLOC_DEFINE(M_WG_COOKIE, "WG cookie", "wireguard cookie");


static inline uint64_t
siphash13(const uint8_t key[SIPHASH_KEY_LENGTH], const void *src, size_t len)
{
	SIPHASH_CTX ctx;
	return SipHashX(&ctx, 1, 3, key, src, len);
}

static inline bool
timer_expired(const struct timespec *birthdate, time_t sec, long nsec)
{
	struct timespec uptime;
	struct timespec expire = { .tv_sec = sec, .tv_nsec = nsec };

	if (birthdate->tv_sec == 0 && birthdate->tv_nsec == 0)
		return (true);

	getnanouptime(&uptime);
	timespecadd(birthdate, &expire, &expire);
	return timespeccmp(&uptime, &expire, >);
}

/*----------------------------------------------------------------------------*/
/* Public Functions */

int
cookie_init(void)
{
	ratelimit_zone = objcache_create_simple(
	    M_WG_RATELIMIT, sizeof(struct ratelimit_entry));
	if (ratelimit_zone == NULL)
		return (ENOMEM);

	ratelimit_init(&ratelimit_v4);
#ifdef INET6
	ratelimit_init(&ratelimit_v6);
#endif

	return (0);
}

void
cookie_deinit(void)
{
	ratelimit_deinit(&ratelimit_v4);
#ifdef INET6
	ratelimit_deinit(&ratelimit_v6);
#endif
	if (ratelimit_zone != NULL)
		objcache_destroy(ratelimit_zone);
}

struct cookie_checker *
cookie_checker_alloc(void)
{
	struct cookie_checker *cc;

	cc = kmalloc(sizeof(*cc), M_WG_COOKIE, M_WAITOK | M_ZERO);
	lockinit(&cc->cc_key_lock, "cookie_checker_key", 0, 0);
	lockinit(&cc->cc_secret_mtx, "cookie_checker_secret", 0, 0);

	return (cc);
}

void
cookie_checker_free(struct cookie_checker *cc)
{
	lockuninit(&cc->cc_key_lock);
	lockuninit(&cc->cc_secret_mtx);
	explicit_bzero(cc, sizeof(*cc));
	kfree(cc, M_WG_COOKIE);
}

void
cookie_checker_update(struct cookie_checker *cc,
		      const uint8_t key[COOKIE_INPUT_SIZE])
{
	lockmgr(&cc->cc_key_lock, LK_EXCLUSIVE);
	if (key != NULL) {
		precompute_key(cc->cc_mac1_key, key, COOKIE_MAC1_KEY_LABEL,
			       sizeof(COOKIE_MAC1_KEY_LABEL) - 1);
		precompute_key(cc->cc_cookie_key, key, COOKIE_COOKIE_KEY_LABEL,
			       sizeof(COOKIE_COOKIE_KEY_LABEL) - 1);
	} else {
		bzero(cc->cc_mac1_key, sizeof(cc->cc_mac1_key));
		bzero(cc->cc_cookie_key, sizeof(cc->cc_cookie_key));
	}
	lockmgr(&cc->cc_key_lock, LK_RELEASE);
}

void
cookie_checker_create_payload(struct cookie_checker *cc,
			      const struct cookie_macs *macs,
			      uint8_t nonce[COOKIE_NONCE_SIZE],
			      uint8_t ecookie[COOKIE_ENCRYPTED_SIZE],
			      const struct sockaddr *sa)
{
	uint8_t cookie[COOKIE_COOKIE_SIZE];

	make_cookie(cc, cookie, sa);
	karc4random_buf(nonce, COOKIE_NONCE_SIZE);

	lockmgr(&cc->cc_key_lock, LK_SHARED);
	xchacha20poly1305_encrypt(ecookie, cookie, COOKIE_COOKIE_SIZE,
				  macs->mac1, COOKIE_MAC_SIZE, nonce,
				  cc->cc_cookie_key);
	lockmgr(&cc->cc_key_lock, LK_RELEASE);

	explicit_bzero(cookie, sizeof(cookie));
}

int
cookie_checker_validate_macs(struct cookie_checker *cc,
			     const struct cookie_macs *macs,
			     const void *buf, size_t len, bool check_cookie,
			     const struct sockaddr *sa)
{
	struct cookie_macs our_macs;
	uint8_t cookie[COOKIE_COOKIE_SIZE];

	/* Validate incoming MACs */
	lockmgr(&cc->cc_key_lock, LK_SHARED);
	macs_mac1(&our_macs, buf, len, cc->cc_mac1_key);
	lockmgr(&cc->cc_key_lock, LK_RELEASE);

	/* If mac1 is invald, we want to drop the packet */
	if (timingsafe_bcmp(our_macs.mac1, macs->mac1, COOKIE_MAC_SIZE) != 0)
		return (EINVAL);

	if (check_cookie) {
		make_cookie(cc, cookie, sa);
		macs_mac2(&our_macs, buf, len, cookie);

		/* If mac2 is invalid, we want to send a cookie response. */
		if (timingsafe_bcmp(our_macs.mac2, macs->mac2, COOKIE_MAC_SIZE)
		    != 0)
			return (EAGAIN);

		/*
		 * If the mac2 is valid, we may want to rate limit the peer.
		 * ratelimit_allow() will return either 0 or ECONNREFUSED,
		 * implying there is no ratelimiting, or we should ratelimit
		 * (refuse), respectively.
		 */
		if (sa->sa_family == AF_INET)
			return ratelimit_allow(&ratelimit_v4, sa);
#ifdef INET6
		else if (sa->sa_family == AF_INET6)
			return ratelimit_allow(&ratelimit_v6, sa);
#endif
		else
			return (EAFNOSUPPORT);
	}

	return (0);
}

struct cookie_maker *
cookie_maker_alloc(const uint8_t key[COOKIE_INPUT_SIZE])
{
	struct cookie_maker *cm;

	cm = kmalloc(sizeof(*cm), M_WG_COOKIE, M_WAITOK | M_ZERO);
	precompute_key(cm->cm_mac1_key, key, COOKIE_MAC1_KEY_LABEL,
		       sizeof(COOKIE_MAC1_KEY_LABEL) - 1);
	precompute_key(cm->cm_cookie_key, key, COOKIE_COOKIE_KEY_LABEL,
		       sizeof(COOKIE_COOKIE_KEY_LABEL) - 1);
	lockinit(&cm->cm_lock, "cookie_maker", 0, 0);

	return (cm);
}

void
cookie_maker_free(struct cookie_maker *cm)
{
	lockuninit(&cm->cm_lock);
	explicit_bzero(cm, sizeof(*cm));
	kfree(cm, M_WG_COOKIE);
}

int
cookie_maker_consume_payload(struct cookie_maker *cm,
			     const uint8_t nonce[COOKIE_NONCE_SIZE],
			     const uint8_t ecookie[COOKIE_ENCRYPTED_SIZE])
{
	uint8_t cookie[COOKIE_COOKIE_SIZE];
	int ret = 0;

	lockmgr(&cm->cm_lock, LK_SHARED);

	if (!cm->cm_mac1_sent) {
		ret = ETIMEDOUT;
		goto out;
	}

	if (!xchacha20poly1305_decrypt(cookie, ecookie, COOKIE_ENCRYPTED_SIZE,
				       cm->cm_mac1_last, COOKIE_MAC_SIZE,
				       nonce, cm->cm_cookie_key)) {
		ret = EINVAL;
		goto out;
	}

	lockmgr(&cm->cm_lock, LK_RELEASE);
	lockmgr(&cm->cm_lock, LK_EXCLUSIVE);

	memcpy(cm->cm_cookie, cookie, COOKIE_COOKIE_SIZE);
	getnanouptime(&cm->cm_cookie_birthdate);
	cm->cm_cookie_valid = true;
	cm->cm_mac1_sent = false;

out:
	lockmgr(&cm->cm_lock, LK_RELEASE);
	return (ret);
}

void
cookie_maker_mac(struct cookie_maker *cm, struct cookie_macs *macs,
		 const void *buf, size_t len)
{
	lockmgr(&cm->cm_lock, LK_EXCLUSIVE);

	macs_mac1(macs, buf, len, cm->cm_mac1_key);
	memcpy(cm->cm_mac1_last, macs->mac1, COOKIE_MAC_SIZE);
	cm->cm_mac1_sent = true;

	if (cm->cm_cookie_valid &&
	    !timer_expired(&cm->cm_cookie_birthdate,
			   COOKIE_SECRET_MAX_AGE - COOKIE_SECRET_LATENCY, 0)) {
		macs_mac2(macs, buf, len, cm->cm_cookie);
	} else {
		bzero(macs->mac2, COOKIE_MAC_SIZE);
		cm->cm_cookie_valid = false;
	}

	lockmgr(&cm->cm_lock, LK_RELEASE);
}

/*----------------------------------------------------------------------------*/
/* Private functions */

static void
precompute_key(uint8_t key[COOKIE_KEY_SIZE],
	       const uint8_t input[COOKIE_INPUT_SIZE],
	       const uint8_t *label, size_t label_len)
{
	struct blake2s_state blake;

	blake2s_init(&blake, COOKIE_KEY_SIZE);
	blake2s_update(&blake, label, label_len);
	blake2s_update(&blake, input, COOKIE_INPUT_SIZE);
	blake2s_final(&blake, key);
}

static void
macs_mac1(struct cookie_macs *macs, const void *buf, size_t len,
	  const uint8_t key[COOKIE_KEY_SIZE])
{
	struct blake2s_state state;

	blake2s_init_key(&state, COOKIE_MAC_SIZE, key, COOKIE_KEY_SIZE);
	blake2s_update(&state, buf, len);
	blake2s_final(&state, macs->mac1);
}

static void
macs_mac2(struct cookie_macs *macs, const void *buf, size_t len,
	  const uint8_t key[COOKIE_COOKIE_SIZE])
{
	struct blake2s_state state;

	blake2s_init_key(&state, COOKIE_MAC_SIZE, key, COOKIE_COOKIE_SIZE);
	blake2s_update(&state, buf, len);
	blake2s_update(&state, macs->mac1, COOKIE_MAC_SIZE);
	blake2s_final(&state, macs->mac2);
}

static void
make_cookie(struct cookie_checker *cc, uint8_t cookie[COOKIE_COOKIE_SIZE],
	    const struct sockaddr *sa)
{
	struct blake2s_state state;

	lockmgr(&cc->cc_secret_mtx, LK_EXCLUSIVE);
	if (timer_expired(&cc->cc_secret_birthdate,
			  COOKIE_SECRET_MAX_AGE, 0)) {
		karc4random_buf(cc->cc_secret, COOKIE_SECRET_SIZE);
		getnanouptime(&cc->cc_secret_birthdate);
	}
	blake2s_init_key(&state, COOKIE_COOKIE_SIZE, cc->cc_secret,
			 COOKIE_SECRET_SIZE);
	lockmgr(&cc->cc_secret_mtx, LK_RELEASE);

	if (sa->sa_family == AF_INET) {
		const struct sockaddr_in *sin = (const void *)sa;
		blake2s_update(&state, (const uint8_t *)&sin->sin_addr,
			       sizeof(sin->sin_addr));
		blake2s_update(&state, (const uint8_t *)&sin->sin_port,
			       sizeof(sin->sin_port));
		blake2s_final(&state, cookie);
#ifdef INET6
	} else if (sa->sa_family == AF_INET6) {
		const struct sockaddr_in6 *sin6 = (const void *)sa;
		blake2s_update(&state, (const uint8_t *)&sin6->sin6_addr,
			       sizeof(sin6->sin6_addr));
		blake2s_update(&state, (const uint8_t *)&sin6->sin6_port,
			       sizeof(sin6->sin6_port));
		blake2s_final(&state, cookie);
#endif
	} else {
		karc4random_buf(cookie, COOKIE_COOKIE_SIZE);
	}
}


static void
ratelimit_init(struct ratelimit *rl)
{
	size_t i;

	bzero(rl, sizeof(*rl));
	lockinit(&rl->rl_mtx, "ratelimit_lock", 0, 0);
	callout_init_lk(&rl->rl_gc, &rl->rl_mtx);
	karc4random_buf(rl->rl_secret, sizeof(rl->rl_secret));
	for (i = 0; i < RATELIMIT_SIZE; i++)
		LIST_INIT(&rl->rl_table[i]);
	rl->rl_table_num = 0;

	rl->rl_initialized = true;
}

static void
ratelimit_deinit(struct ratelimit *rl)
{
	if (!rl->rl_initialized)
		return;

	lockmgr(&rl->rl_mtx, LK_EXCLUSIVE);
	callout_stop(&rl->rl_gc);
	callout_terminate(&rl->rl_gc);
	ratelimit_gc(rl, true);
	lockmgr(&rl->rl_mtx, LK_RELEASE);
	lockuninit(&rl->rl_mtx);

	rl->rl_initialized = false;
}

static void
ratelimit_gc_callout(void *_rl)
{
	/* callout will lock for us */
	ratelimit_gc(_rl, false);
}

static void
ratelimit_gc_schedule(struct ratelimit *rl)
{
	/*
	 * Trigger another GC if needed.  There is no point calling GC if
	 * there are no entries in the table.  We also want to ensure that
	 * GC occurs on a regular interval, so don't override a currently
	 * pending GC.
	 */
	if (rl->rl_table_num > 0 && !callout_pending(&rl->rl_gc))
		callout_reset(&rl->rl_gc, ELEMENT_TIMEOUT * hz,
			      ratelimit_gc_callout, rl);
}

static void
ratelimit_gc(struct ratelimit *rl, bool force)
{
	struct ratelimit_entry *r, *tr;
	struct timespec expiry;
	size_t i;

	KKASSERT(lockstatus(&rl->rl_mtx, curthread) == LK_EXCLUSIVE);

	if (rl->rl_table_num == 0)
		return;

	getnanouptime(&expiry);
	expiry.tv_sec -= ELEMENT_TIMEOUT;

	for (i = 0; i < RATELIMIT_SIZE; i++) {
		LIST_FOREACH_MUTABLE(r, &rl->rl_table[i], r_entry, tr) {
			if (force ||
			    timespeccmp(&r->r_last_time, &expiry, <)) {
				rl->rl_table_num--;
				LIST_REMOVE(r, r_entry);
				objcache_put(ratelimit_zone, r);
			}
		}
	}

	/*
	 * There will be no entries left after a forced GC, so no need to
	 * schedule another GC.
	 */
	if (force)
		KKASSERT(rl->rl_table_num == 0);
	else
		ratelimit_gc_schedule(rl);
}

static int
ratelimit_allow(struct ratelimit *rl, const struct sockaddr *sa)
{
	struct timespec diff;
	struct ratelimit_entry *r;
	struct ratelimit_key key = { 0 };
	uint64_t bucket, tokens;
	size_t len;
	int ret = ECONNREFUSED;

	if (sa->sa_family == AF_INET) {
		len = IPV4_MASK_SIZE;
		memcpy(key.ip, &((const struct sockaddr_in *)sa)->sin_addr,
		       len);
	}
#ifdef INET6
	else if (sa->sa_family == AF_INET6) {
		len = IPV6_MASK_SIZE;
		memcpy(key.ip, &((const struct sockaddr_in6 *)sa)->sin6_addr,
		       len);
	}
#endif
	else {
		return (ret);
	}

	bucket = siphash13(rl->rl_secret, &key, len) & RATELIMIT_MASK;
	lockmgr(&rl->rl_mtx, LK_EXCLUSIVE);

	LIST_FOREACH(r, &rl->rl_table[bucket], r_entry) {
		if (memcmp(&r->r_key, &key, len) != 0)
			continue;

		/*
		 * Found an entry for the endpoint.  We apply standard token
		 * bucket, by calculating the time lapsed since last_time,
		 * adding that, ensuring that we cap the tokens at TOKEN_MAX.
		 * If the endpoint has no tokens left (i.e., tokens <
		 * INITIATION_COST) then we block the request.  Otherwise, we
		 * subtract the INITITIATION_COST and return OK.
		 */
		diff = r->r_last_time;
		getnanouptime(&r->r_last_time);
		timespecsub(&r->r_last_time, &diff, &diff);

		tokens = r->r_tokens;
		tokens += diff.tv_sec * NSEC_PER_SEC + diff.tv_nsec;
		if (tokens > TOKEN_MAX)
			tokens = TOKEN_MAX;

		if (tokens >= INITIATION_COST) {
			r->r_tokens = tokens - INITIATION_COST;
			goto ok;
		} else {
			r->r_tokens = tokens;
			goto error;
		}
	}

	/*
	 * Didn't have an entry for the endpoint, so let's add one if we
	 * have space.
	 */
	if (rl->rl_table_num >= RATELIMIT_SIZE_MAX)
		goto error;

	if ((r = objcache_get(ratelimit_zone, M_NOWAIT)) == NULL)
		goto error;
	bzero(r, sizeof(*r)); /* objcache_get() doesn't ensure M_ZERO. */

	rl->rl_table_num++;

	/* Insert the new entry and initialize it. */
	LIST_INSERT_HEAD(&rl->rl_table[bucket], r, r_entry);
	r->r_key = key;
	r->r_tokens = TOKEN_MAX - INITIATION_COST;
	getnanouptime(&r->r_last_time);

	/* We've added a new entry; let's trigger GC. */
	ratelimit_gc_schedule(rl);

ok:
	ret = 0;
error:
	lockmgr(&rl->rl_mtx, LK_RELEASE);
	return (ret);
}


#ifdef WG_SELFTESTS
#include "selftest/cookie.c"
#endif /* WG_SELFTESTS */
