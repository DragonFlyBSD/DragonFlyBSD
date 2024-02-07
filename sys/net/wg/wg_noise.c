/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (C) 2015-2021 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2021 Matt Dunwoodie <ncon@noconroy.net>
 * Copyright (c) 2022 The FreeBSD Foundation
 * Copyright (c) 2023-2024 Aaron LI <aly@aaronly.me>
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
/*
 * This implements Noise_IKpsk2:
 * <- s
 * ******
 * -> e, es, s, ss, {t}
 * <- e, ee, se, psk, {}
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitops.h> /* ilog2() */
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/refcount.h>
#include <sys/time.h>

#include <machine/atomic.h>

#include <crypto/chachapoly.h>
#include <crypto/blake2/blake2s.h>
#include <crypto/curve25519/curve25519.h>
#include <crypto/siphash/siphash.h>

#include "wg_noise.h"

/* Protocol string constants */
#define NOISE_HANDSHAKE_NAME	"Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s"
#define NOISE_IDENTIFIER_NAME	"WireGuard v1 zx2c4 Jason@zx2c4.com"

/* Constants for the counter */
#define COUNTER_BITS_TOTAL	8192
#define COUNTER_BITS		(sizeof(unsigned long) * 8)
#define COUNTER_ORDER		ilog2(COUNTER_BITS)
#define COUNTER_WINDOW_SIZE	(COUNTER_BITS_TOTAL - COUNTER_BITS)
#define COUNTER_NUM		(COUNTER_BITS_TOTAL / COUNTER_BITS)
#define COUNTER_MASK		(COUNTER_NUM - 1)

/* Constants for the keypair */
#define REKEY_AFTER_MESSAGES	(1ULL << 60)
#define REJECT_AFTER_MESSAGES	(UINT64_MAX - COUNTER_WINDOW_SIZE - 1)
#define REKEY_AFTER_TIME	120
#define REKEY_AFTER_TIME_RECV	165
#define REJECT_INTERVAL		(1000000000 / 50) /* fifty times per sec */
#define REJECT_INTERVAL_MASK	(~((1ULL << ilog2(REJECT_INTERVAL)) - 1))

/* Constants for the hashtable */
#define HT_INDEX_SIZE		(1 << 13)
#define HT_INDEX_MASK		(HT_INDEX_SIZE - 1)
#define HT_REMOTE_SIZE		(1 << 11)
#define HT_REMOTE_MASK		(HT_REMOTE_SIZE - 1)
#define MAX_REMOTE_PER_LOCAL	(1 << 20)

struct noise_index {
	LIST_ENTRY(noise_index)	 i_entry;
	uint32_t		 i_local_index;
	uint32_t		 i_remote_index;
	bool			 i_is_keypair;
};

struct noise_keypair {
	struct noise_index	 kp_index;

	u_int			 kp_refcnt;
	bool			 kp_can_send;
	bool			 kp_is_initiator;
	struct timespec		 kp_birthdate; /* nanouptime */
	struct noise_remote	*kp_remote;

	uint8_t			 kp_send[NOISE_SYMMETRIC_KEY_LEN];
	uint8_t			 kp_recv[NOISE_SYMMETRIC_KEY_LEN];

	struct lock		 kp_counter_lock;
	uint64_t		 kp_counter_send; /* next counter available */
	uint64_t		 kp_counter_recv; /* max counter received */
	unsigned long		 kp_backtrack[COUNTER_NUM];
};

struct noise_handshake {
	uint8_t			 hs_e[NOISE_PUBLIC_KEY_LEN];
	uint8_t			 hs_hash[NOISE_HASH_LEN];
	uint8_t			 hs_ck[NOISE_HASH_LEN];
};

/* Handshake states of the remote/peer side. */
enum noise_handshake_state {
	HANDSHAKE_DEAD,
	HANDSHAKE_INITIATOR,
	HANDSHAKE_RESPONDER,
};

struct noise_remote {
	struct noise_index		 r_index;

	LIST_ENTRY(noise_remote)	 r_entry;
	bool				 r_entry_inserted;
	uint8_t				 r_public[NOISE_PUBLIC_KEY_LEN];

	struct lock			 r_handshake_lock;
	struct noise_handshake		 r_handshake;
	enum noise_handshake_state	 r_handshake_state;
	struct timespec			 r_last_sent; /* nanouptime */
	struct timespec			 r_last_init_recv; /* nanouptime */
	uint8_t				 r_timestamp[NOISE_TIMESTAMP_LEN];
	uint8_t				 r_psk[NOISE_SYMMETRIC_KEY_LEN];
	uint8_t				 r_ss[NOISE_PUBLIC_KEY_LEN];

	u_int				 r_refcnt;
	struct noise_local		*r_local;
	void				*r_arg;

	struct lock			 r_keypair_lock;
	struct noise_keypair		*r_keypair_next;
	struct noise_keypair		*r_keypair_current;
	struct noise_keypair		*r_keypair_previous;
};

struct noise_local {
	struct lock			 l_identity_lock;
	bool				 l_has_identity;
	uint8_t				 l_public[NOISE_PUBLIC_KEY_LEN];
	uint8_t				 l_private[NOISE_PUBLIC_KEY_LEN];

	u_int				 l_refcnt;
	uint8_t				 l_hash_key[SIPHASH_KEY_LENGTH];

	/* Hash table to lookup the remote from its public key. */
	struct lock			 l_remote_lock;
	size_t				 l_remote_num;
	LIST_HEAD(, noise_remote)	 l_remote_hash[HT_REMOTE_SIZE];

	/* Hash table to lookup the remote/keypair from its index. */
	struct lock			 l_index_lock;
	LIST_HEAD(, noise_index)	 l_index_hash[HT_INDEX_SIZE];
};

static void	noise_precompute_ss(struct noise_local *,
				    struct noise_remote *);

static struct noise_local *
		noise_local_ref(struct noise_local *);
static void	noise_local_put(struct noise_local *);

static uint32_t	noise_remote_index_insert(struct noise_local *,
					  struct noise_remote *);
static struct noise_remote *
		noise_remote_index_lookup(struct noise_local *,
					  uint32_t, bool);
static bool	noise_remote_index_remove(struct noise_local *,
					  struct noise_remote *);
static void	noise_remote_expire_current(struct noise_remote *);

static bool	noise_begin_session(struct noise_remote *);
static void	noise_keypair_drop(struct noise_keypair *);

static void	noise_kdf(uint8_t *, uint8_t *, uint8_t *, const uint8_t *,
			  size_t, size_t, size_t, size_t,
			  const uint8_t [NOISE_HASH_LEN]);
static bool	noise_mix_dh(uint8_t [NOISE_HASH_LEN],
			     uint8_t [NOISE_SYMMETRIC_KEY_LEN],
			     const uint8_t [NOISE_PUBLIC_KEY_LEN],
			     const uint8_t [NOISE_PUBLIC_KEY_LEN]);
static bool	noise_mix_ss(uint8_t ck[NOISE_HASH_LEN],
			     uint8_t [NOISE_SYMMETRIC_KEY_LEN],
			     const uint8_t [NOISE_PUBLIC_KEY_LEN]);
static void	noise_mix_hash(uint8_t [NOISE_HASH_LEN],
			       const uint8_t *, size_t);
static void	noise_mix_psk(uint8_t [NOISE_HASH_LEN],
			      uint8_t [NOISE_HASH_LEN],
			      uint8_t [NOISE_SYMMETRIC_KEY_LEN],
			      const uint8_t [NOISE_SYMMETRIC_KEY_LEN]);

static void	noise_param_init(uint8_t [NOISE_HASH_LEN],
				 uint8_t [NOISE_HASH_LEN],
				 const uint8_t [NOISE_PUBLIC_KEY_LEN]);
static void	noise_msg_encrypt(uint8_t *, const uint8_t *, size_t,
				  uint8_t [NOISE_SYMMETRIC_KEY_LEN],
				  uint8_t [NOISE_HASH_LEN]);
static bool	noise_msg_decrypt(uint8_t *, const uint8_t *, size_t,
				  uint8_t [NOISE_SYMMETRIC_KEY_LEN],
				  uint8_t [NOISE_HASH_LEN]);
static void	noise_msg_ephemeral(uint8_t [NOISE_HASH_LEN],
				    uint8_t [NOISE_HASH_LEN],
				    const uint8_t [NOISE_PUBLIC_KEY_LEN]);

static void	noise_tai64n_now(uint8_t [NOISE_TIMESTAMP_LEN]);

static MALLOC_DEFINE(M_NOISE, "NOISE", "wgnoise");


static inline uint64_t
siphash24(const uint8_t key[SIPHASH_KEY_LENGTH], const void *src, size_t len)
{
	SIPHASH_CTX ctx;
	return SipHashX(&ctx, 2, 4, key, src, len);
}

static inline bool
timer_expired(const struct timespec *birthdate, time_t sec, long nsec)
{
	struct timespec uptime;
	struct timespec expire = { .tv_sec = sec, .tv_nsec = nsec };

	if (__predict_false(!timespecisset(birthdate)))
		return (true);

	getnanouptime(&uptime);
	timespecadd(birthdate, &expire, &expire);
	return timespeccmp(&uptime, &expire, >);
}


/*----------------------------------------------------------------------------*/
/* Local configuration */

struct noise_local *
noise_local_alloc(void)
{
	struct noise_local *l;
	size_t i;

	l = kmalloc(sizeof(*l), M_NOISE, M_WAITOK | M_ZERO);

	lockinit(&l->l_identity_lock, "noise_identity", 0, 0);
	refcount_init(&l->l_refcnt, 1);
	karc4random_buf(l->l_hash_key, sizeof(l->l_hash_key));

	lockinit(&l->l_remote_lock, "noise_remote", 0, 0);
	for (i = 0; i < HT_REMOTE_SIZE; i++)
		LIST_INIT(&l->l_remote_hash[i]);

	lockinit(&l->l_index_lock, "noise_index", 0, 0);
	for (i = 0; i < HT_INDEX_SIZE; i++)
		LIST_INIT(&l->l_index_hash[i]);

	return (l);
}

static struct noise_local *
noise_local_ref(struct noise_local *l)
{
	refcount_acquire(&l->l_refcnt);
	return (l);
}

static void
noise_local_put(struct noise_local *l)
{
	if (refcount_release(&l->l_refcnt)) {
		lockuninit(&l->l_identity_lock);
		lockuninit(&l->l_remote_lock);
		lockuninit(&l->l_index_lock);
		explicit_bzero(l, sizeof(*l));
		kfree(l, M_NOISE);
	}
}

void
noise_local_free(struct noise_local *l)
{
	noise_local_put(l);
}

bool
noise_local_set_private(struct noise_local *l,
			const uint8_t private[NOISE_PUBLIC_KEY_LEN])
{
	struct noise_remote *r;
	size_t i;
	bool has_identity;

	lockmgr(&l->l_identity_lock, LK_EXCLUSIVE);

	/* Note: we might be removing the private key. */
	memcpy(l->l_private, private, NOISE_PUBLIC_KEY_LEN);
	curve25519_clamp_secret(l->l_private);
	has_identity = l->l_has_identity =
		curve25519_generate_public(l->l_public, l->l_private);

	/* Invalidate all existing handshakes. */
	lockmgr(&l->l_remote_lock, LK_SHARED);
	for (i = 0; i < HT_REMOTE_SIZE; i++) {
		LIST_FOREACH(r, &l->l_remote_hash[i], r_entry) {
			noise_precompute_ss(l, r);
			noise_remote_expire_current(r);
		}
	}
	lockmgr(&l->l_remote_lock, LK_RELEASE);

	lockmgr(&l->l_identity_lock, LK_RELEASE);
	return (has_identity);
}

bool
noise_local_keys(struct noise_local *l, uint8_t public[NOISE_PUBLIC_KEY_LEN],
		 uint8_t private[NOISE_PUBLIC_KEY_LEN])
{
	bool has_identity;

	lockmgr(&l->l_identity_lock, LK_SHARED);
	has_identity = l->l_has_identity;
	if (has_identity) {
		if (public != NULL)
			memcpy(public, l->l_public, NOISE_PUBLIC_KEY_LEN);
		if (private != NULL)
			memcpy(private, l->l_private, NOISE_PUBLIC_KEY_LEN);
	}
	lockmgr(&l->l_identity_lock, LK_RELEASE);

	return (has_identity);
}

static void
noise_precompute_ss(struct noise_local *l, struct noise_remote *r)
{
	KKASSERT(lockstatus(&l->l_identity_lock, curthread) != 0);

	lockmgr(&r->r_handshake_lock, LK_EXCLUSIVE);
	if (!l->l_has_identity ||
	    !curve25519(r->r_ss, l->l_private, r->r_public))
		bzero(r->r_ss, NOISE_PUBLIC_KEY_LEN);
	lockmgr(&r->r_handshake_lock, LK_RELEASE);
}

/*----------------------------------------------------------------------------*/
/* Remote configuration */

struct noise_remote *
noise_remote_alloc(struct noise_local *l,
		   const uint8_t public[NOISE_PUBLIC_KEY_LEN], void *arg)
{
	struct noise_remote *r;

	r = kmalloc(sizeof(*r), M_NOISE, M_WAITOK | M_ZERO);

	memcpy(r->r_public, public, NOISE_PUBLIC_KEY_LEN);

	lockinit(&r->r_handshake_lock, "noise_handshake", 0, 0);
	r->r_handshake_state = HANDSHAKE_DEAD;

	refcount_init(&r->r_refcnt, 1);
	r->r_local = noise_local_ref(l);
	r->r_arg = arg;

	lockinit(&r->r_keypair_lock, "noise_keypair", 0, 0);

	lockmgr(&l->l_identity_lock, LK_SHARED);
	noise_precompute_ss(l, r);
	lockmgr(&l->l_identity_lock, LK_RELEASE);

	return (r);
}

int
noise_remote_enable(struct noise_remote *r)
{
	struct noise_local *l = r->r_local;
	uint64_t idx;
	int ret = 0;

	idx = siphash24(l->l_hash_key, r->r_public, NOISE_PUBLIC_KEY_LEN);
	idx &= HT_REMOTE_MASK;

	lockmgr(&l->l_remote_lock, LK_EXCLUSIVE);
	if (!r->r_entry_inserted) {
		/* Insert to hashtable */
		if (l->l_remote_num < MAX_REMOTE_PER_LOCAL) {
			r->r_entry_inserted = true;
			l->l_remote_num++;
			LIST_INSERT_HEAD(&l->l_remote_hash[idx], r, r_entry);
		} else {
			ret = ENOSPC;
		}
	}
	lockmgr(&l->l_remote_lock, LK_RELEASE);

	return (ret);
}

void
noise_remote_disable(struct noise_remote *r)
{
	struct noise_local *l = r->r_local;

	/* Remove from hashtable */
	lockmgr(&l->l_remote_lock, LK_EXCLUSIVE);
	if (r->r_entry_inserted) {
		r->r_entry_inserted = false;
		LIST_REMOVE(r, r_entry);
		l->l_remote_num--;
	};
	lockmgr(&l->l_remote_lock, LK_RELEASE);
}

struct noise_remote *
noise_remote_lookup(struct noise_local *l,
		    const uint8_t public[NOISE_PUBLIC_KEY_LEN])
{
	struct noise_remote *r, *ret = NULL;
	uint64_t idx;

	idx = siphash24(l->l_hash_key, public, NOISE_PUBLIC_KEY_LEN);
	idx &= HT_REMOTE_MASK;

	lockmgr(&l->l_remote_lock, LK_SHARED);
	LIST_FOREACH(r, &l->l_remote_hash[idx], r_entry) {
		if (timingsafe_bcmp(r->r_public, public, NOISE_PUBLIC_KEY_LEN)
		    == 0) {
			ret = noise_remote_ref(r);
			break;
		}
	}
	lockmgr(&l->l_remote_lock, LK_RELEASE);

	return (ret);
}

static uint32_t
noise_remote_index_insert(struct noise_local *l, struct noise_remote *r)
{
	struct noise_index *i, *r_i = &r->r_index;
	uint32_t idx;

	noise_remote_index_remove(l, r);

	lockmgr(&l->l_index_lock, LK_EXCLUSIVE);
retry:
	r_i->i_local_index = karc4random();
	idx = r_i->i_local_index & HT_INDEX_MASK;
	LIST_FOREACH(i, &l->l_index_hash[idx], i_entry) {
		if (i->i_local_index == r_i->i_local_index)
			goto retry;
	}
	LIST_INSERT_HEAD(&l->l_index_hash[idx], r_i, i_entry);
	lockmgr(&l->l_index_lock, LK_RELEASE);

	return (r_i->i_local_index);
}

static struct noise_remote *
noise_remote_index_lookup(struct noise_local *l, uint32_t idx0,
			  bool lookup_keypair)
{
	struct noise_index *i;
	struct noise_keypair *kp;
	struct noise_remote *r, *ret = NULL;
	uint32_t idx = idx0 & HT_INDEX_MASK;

	lockmgr(&l->l_index_lock, LK_SHARED);
	LIST_FOREACH(i, &l->l_index_hash[idx], i_entry) {
		if (i->i_local_index == idx0) {
			if (!i->i_is_keypair) {
				r = (struct noise_remote *) i;
			} else if (lookup_keypair) {
				/* Also include keypair entries. */
				kp = (struct noise_keypair *) i;
				r = kp->kp_remote;
			} else {
				break;
			}
			ret = noise_remote_ref(r);
			break;
		}
	}
	lockmgr(&l->l_index_lock, LK_RELEASE);

	return (ret);
}

struct noise_remote *
noise_remote_index(struct noise_local *l, uint32_t idx)
{
	return noise_remote_index_lookup(l, idx, true);
}

static bool
noise_remote_index_remove(struct noise_local *l, struct noise_remote *r)
{
	KKASSERT(lockstatus(&r->r_handshake_lock, curthread) == LK_EXCLUSIVE);

	if (r->r_handshake_state != HANDSHAKE_DEAD) {
		lockmgr(&l->l_index_lock, LK_EXCLUSIVE);
		r->r_handshake_state = HANDSHAKE_DEAD;
		LIST_REMOVE(&r->r_index, i_entry);
		lockmgr(&l->l_index_lock, LK_RELEASE);
		return (true);
	}

	return (false);
}

struct noise_remote *
noise_remote_ref(struct noise_remote *r)
{
	refcount_acquire(&r->r_refcnt);
	return (r);
}

void
noise_remote_put(struct noise_remote *r)
{
	if (refcount_release(&r->r_refcnt)) {
		noise_local_put(r->r_local);
		lockuninit(&r->r_handshake_lock);
		lockuninit(&r->r_keypair_lock);
		explicit_bzero(r, sizeof(*r));
		kfree(r, M_NOISE);
	}
}

void
noise_remote_free(struct noise_remote *r)
{
	noise_remote_disable(r);
	noise_remote_handshake_clear(r);
	noise_remote_keypairs_clear(r);
	noise_remote_put(r);
}

void *
noise_remote_arg(struct noise_remote *r)
{
	return (r->r_arg);
}

void
noise_remote_set_psk(struct noise_remote *r,
		     const uint8_t psk[NOISE_SYMMETRIC_KEY_LEN])
{
	lockmgr(&r->r_handshake_lock, LK_EXCLUSIVE);
	if (psk == NULL)
		bzero(r->r_psk, NOISE_SYMMETRIC_KEY_LEN);
	else
		memcpy(r->r_psk, psk, NOISE_SYMMETRIC_KEY_LEN);
	lockmgr(&r->r_handshake_lock, LK_RELEASE);
}

bool
noise_remote_keys(struct noise_remote *r, uint8_t public[NOISE_PUBLIC_KEY_LEN],
		  uint8_t psk[NOISE_SYMMETRIC_KEY_LEN])
{
	static uint8_t null_psk[NOISE_SYMMETRIC_KEY_LEN];
	bool has_psk = false;

	if (public != NULL)
		memcpy(public, r->r_public, NOISE_PUBLIC_KEY_LEN);

	lockmgr(&r->r_handshake_lock, LK_SHARED);
	if (timingsafe_bcmp(r->r_psk, null_psk, NOISE_SYMMETRIC_KEY_LEN) != 0) {
		has_psk = true;
		if (psk != NULL)
			memcpy(psk, r->r_psk, NOISE_SYMMETRIC_KEY_LEN);
	}
	lockmgr(&r->r_handshake_lock, LK_RELEASE);

	return (has_psk);
}

bool
noise_remote_initiation_expired(struct noise_remote *r)
{
	bool expired;

	lockmgr(&r->r_handshake_lock, LK_SHARED);
	expired = timer_expired(&r->r_last_sent, REKEY_TIMEOUT, 0);
	lockmgr(&r->r_handshake_lock, LK_RELEASE);

	return (expired);
}

void
noise_remote_handshake_clear(struct noise_remote *r)
{
	lockmgr(&r->r_handshake_lock, LK_EXCLUSIVE);
	if (noise_remote_index_remove(r->r_local, r))
		bzero(&r->r_handshake, sizeof(r->r_handshake));
	bzero(&r->r_last_sent, sizeof(r->r_last_sent));
	lockmgr(&r->r_handshake_lock, LK_RELEASE);
}

void
noise_remote_keypairs_clear(struct noise_remote *r)
{
	struct noise_keypair *kp;

	lockmgr(&r->r_keypair_lock, LK_EXCLUSIVE);

	kp = atomic_load_ptr(&r->r_keypair_next);
	atomic_store_ptr(&r->r_keypair_next, NULL);
	noise_keypair_drop(kp);

	kp = atomic_load_ptr(&r->r_keypair_current);
	atomic_store_ptr(&r->r_keypair_current, NULL);
	noise_keypair_drop(kp);

	kp = atomic_load_ptr(&r->r_keypair_previous);
	atomic_store_ptr(&r->r_keypair_previous, NULL);
	noise_keypair_drop(kp);

	lockmgr(&r->r_keypair_lock, LK_RELEASE);
}

static void
noise_remote_expire_current(struct noise_remote *r)
{
	struct noise_keypair *kp;

	noise_remote_handshake_clear(r);

	lockmgr(&r->r_keypair_lock, LK_SHARED);
	kp = atomic_load_ptr(&r->r_keypair_next);
	if (kp != NULL)
		atomic_store_bool(&kp->kp_can_send, false);
	kp = atomic_load_ptr(&r->r_keypair_current);
	if (kp != NULL)
		atomic_store_bool(&kp->kp_can_send, false);
	lockmgr(&r->r_keypair_lock, LK_RELEASE);
}

/*----------------------------------------------------------------------------*/
/* Keypair functions */

struct noise_keypair *
noise_keypair_lookup(struct noise_local *l, uint32_t idx0)
{
	struct noise_index *i;
	struct noise_keypair *kp, *ret = NULL;
	uint32_t idx;

	idx = idx0 & HT_INDEX_MASK;

	lockmgr(&l->l_index_lock, LK_SHARED);
	LIST_FOREACH(i, &l->l_index_hash[idx], i_entry) {
		if (i->i_local_index == idx0 && i->i_is_keypair) {
			kp = (struct noise_keypair *) i;
			ret = noise_keypair_ref(kp);
			break;
		}
	}
	lockmgr(&l->l_index_lock, LK_RELEASE);

	return (ret);
}

struct noise_keypair *
noise_keypair_current(struct noise_remote *r)
{
	struct noise_keypair *kp, *ret = NULL;

	lockmgr(&r->r_keypair_lock, LK_SHARED);
	kp = atomic_load_ptr(&r->r_keypair_current);
	if (kp != NULL && atomic_load_bool(&kp->kp_can_send)) {
		if (timer_expired(&kp->kp_birthdate, REJECT_AFTER_TIME, 0))
			atomic_store_bool(&kp->kp_can_send, false);
		else
			ret = noise_keypair_ref(kp);
	}
	lockmgr(&r->r_keypair_lock, LK_RELEASE);

	return (ret);
}

bool
noise_keypair_received_with(struct noise_keypair *kp)
{
	struct noise_keypair *old;
	struct noise_remote *r = kp->kp_remote;

	if (kp != atomic_load_ptr(&r->r_keypair_next))
		return (false);

	lockmgr(&r->r_keypair_lock, LK_EXCLUSIVE);

	/* Double check after locking. */
	if (kp != atomic_load_ptr(&r->r_keypair_next)) {
		lockmgr(&r->r_keypair_lock, LK_RELEASE);
		return (false);
	}

	old = atomic_load_ptr(&r->r_keypair_previous);
	atomic_store_ptr(&r->r_keypair_previous,
			 atomic_load_ptr(&r->r_keypair_current));
	noise_keypair_drop(old);
	atomic_store_ptr(&r->r_keypair_current, kp);
	atomic_store_ptr(&r->r_keypair_next, NULL);

	lockmgr(&r->r_keypair_lock, LK_RELEASE);

	return (true);
}

struct noise_keypair *
noise_keypair_ref(struct noise_keypair *kp)
{
	refcount_acquire(&kp->kp_refcnt);
	return (kp);
}

void
noise_keypair_put(struct noise_keypair *kp)
{
	if (refcount_release(&kp->kp_refcnt)) {
		noise_remote_put(kp->kp_remote);
		lockuninit(&kp->kp_counter_lock);
		explicit_bzero(kp, sizeof(*kp));
		kfree(kp, M_NOISE);
	}
}

static void
noise_keypair_drop(struct noise_keypair *kp)
{
	struct noise_remote *r;
	struct noise_local *l;

	if (kp == NULL)
		return;

	r = kp->kp_remote;
	l = r->r_local;

	lockmgr(&l->l_index_lock, LK_EXCLUSIVE);
	LIST_REMOVE(&kp->kp_index, i_entry);
	lockmgr(&l->l_index_lock, LK_RELEASE);

	KKASSERT(lockstatus(&r->r_keypair_lock, curthread) == LK_EXCLUSIVE);
	noise_keypair_put(kp);
}

struct noise_remote *
noise_keypair_remote(struct noise_keypair *kp)
{
	return (noise_remote_ref(kp->kp_remote));
}

bool
noise_keypair_counter_next(struct noise_keypair *kp, uint64_t *send)
{
	if (!atomic_load_bool(&kp->kp_can_send))
		return (false);

#ifdef __LP64__
	*send = atomic_fetchadd_64(&kp->kp_counter_send, 1);
#else
	lockmgr(&kp->kp_counter_lock, LK_EXCLUSIVE);
	*send = kp->kp_counter_send++;
	lockmgr(&kp->kp_counter_lock, LK_RELEASE);
#endif
	if (*send < REJECT_AFTER_MESSAGES)
		return (true);

	atomic_store_bool(&kp->kp_can_send, false);
	return (false);
}

/*
 * Validate the received counter to avoid replay attacks.  A sliding window
 * is used to keep track of the received counters, since the UDP messages
 * can arrive out of order.
 *
 * NOTE: Validate the counter only *after* successful decryption, which
 *       ensures that the message and counter is authentic.
 *
 * This implements the algorithm from RFC 6479:
 * "IPsec Anti-Replay Algorithm without Bit Shifting"
 */
int
noise_keypair_counter_check(struct noise_keypair *kp, uint64_t recv)
{
	unsigned long index, index_current, top, i, bit;
	int ret;

	lockmgr(&kp->kp_counter_lock, LK_EXCLUSIVE);

	if (__predict_false(kp->kp_counter_recv >= REJECT_AFTER_MESSAGES ||
			    recv >= REJECT_AFTER_MESSAGES)) {
		ret = EINVAL;
		goto out;
	}

	if (__predict_false(recv + COUNTER_WINDOW_SIZE < kp->kp_counter_recv)) {
		ret = ESTALE;
		goto out;
	}

	index = recv >> COUNTER_ORDER;

	if (__predict_true(recv > kp->kp_counter_recv)) {
		/*
		 * The new counter is ahead of the current counter, so need
		 * to zero out the bitmap that has previously been used.
		 */
		index_current = kp->kp_counter_recv >> COUNTER_ORDER;
		top = MIN(index - index_current, COUNTER_NUM);
		for (i = 1; i <= top; i++)
			kp->kp_backtrack[(i+index_current) & COUNTER_MASK] = 0;
#ifdef __LP64__
		atomic_store_64(&kp->kp_counter_recv, recv);
#else
		kp->kp_counter_recv = recv;
#endif
	}

	index &= COUNTER_MASK;
	bit = 1UL << (recv & (COUNTER_BITS - 1));
	if (kp->kp_backtrack[index] & bit) {
		ret = EEXIST;
		goto out;
	}

	kp->kp_backtrack[index] |= bit;
	ret = 0;

out:
	lockmgr(&kp->kp_counter_lock, LK_RELEASE);
	return (ret);
}

/*
 * Check whether the current keypair of the given remote <r> is expiring soon
 * or already expired, and thus should do a refreshing.
 */
bool
noise_keypair_should_refresh(struct noise_remote *r, bool sending)
{
	struct noise_keypair *kp;
	uint64_t counter;
	bool refresh;

	lockmgr(&r->r_keypair_lock, LK_SHARED);

	kp = atomic_load_ptr(&r->r_keypair_current);
	refresh = (kp != NULL && atomic_load_bool(&kp->kp_can_send));
	if (__predict_false(!refresh))
		goto out;

	if (sending) {
		/* sending path */
#ifdef __LP64__
		counter = atomic_load_64(&kp->kp_counter_send);
#else
		lockmgr(&kp->kp_counter_lock, LK_SHARED);
		counter = kp->kp_counter_send;
		lockmgr(&kp->kp_counter_lock, LK_RELEASE);
#endif
		refresh = (counter > REKEY_AFTER_MESSAGES ||
			   (kp->kp_is_initiator &&
			    timer_expired(&kp->kp_birthdate,
					  REKEY_AFTER_TIME, 0)));
	} else {
		/* receiving path */
		refresh = (kp->kp_is_initiator &&
			   timer_expired(&kp->kp_birthdate, REJECT_AFTER_TIME -
					 KEEPALIVE_TIMEOUT - REKEY_TIMEOUT, 0));
	}

out:
	lockmgr(&r->r_keypair_lock, LK_RELEASE);
	return (refresh);
}

int
noise_keypair_encrypt(struct noise_keypair *kp, uint32_t *r_idx,
		      uint64_t counter, struct mbuf *m)
{
	uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE];
	int ret;

	/* 32 bits of zeros + 64-bit little-endian value of the counter */
	*(uint32_t *)nonce = 0;
	*(uint64_t *)(nonce + 4) = htole64(counter);

	ret = chacha20poly1305_encrypt_mbuf(m, NULL, 0, nonce, kp->kp_send);
	if (ret == 0)
		*r_idx = kp->kp_index.i_remote_index;

	return (ret);
}

int
noise_keypair_decrypt(struct noise_keypair *kp, uint64_t counter,
		      struct mbuf *m)
{
	uint64_t cur_counter;
	uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE];

#ifdef __LP64__
	cur_counter = atomic_load_64(&kp->kp_counter_recv);
#else
	lockmgr(&kp->kp_counter_lock, LK_SHARED);
	cur_counter = kp->kp_counter_recv;
	lockmgr(&kp->kp_counter_lock, LK_RELEASE);
#endif
	if (cur_counter >= REJECT_AFTER_MESSAGES ||
	    timer_expired(&kp->kp_birthdate, REJECT_AFTER_TIME, 0))
		return (EINVAL);

	*(uint32_t *)nonce = 0;
	*(uint64_t *)(nonce + 4) = htole64(counter);

	return chacha20poly1305_decrypt_mbuf(m, NULL, 0, nonce, kp->kp_recv);
}

/*----------------------------------------------------------------------------*/
/* Handshake functions */

bool
noise_create_initiation(struct noise_remote *r, uint32_t *s_idx,
			uint8_t ue[NOISE_PUBLIC_KEY_LEN],
			uint8_t es[NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN],
			uint8_t ets[NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN])
{
	struct noise_handshake *hs = &r->r_handshake;
	struct noise_local *l = r->r_local;
	uint8_t key[NOISE_SYMMETRIC_KEY_LEN];
	bool ok = false;

	lockmgr(&l->l_identity_lock, LK_SHARED);
	lockmgr(&r->r_handshake_lock, LK_EXCLUSIVE);

	if (!l->l_has_identity)
		goto error;
	if (!timer_expired(&r->r_last_sent, REKEY_TIMEOUT, 0))
		goto error;

	noise_param_init(hs->hs_ck, hs->hs_hash, r->r_public);

	/* e */
	curve25519_generate_secret(hs->hs_e);
	if (curve25519_generate_public(ue, hs->hs_e) == 0)
		goto error;
	noise_msg_ephemeral(hs->hs_ck, hs->hs_hash, ue);

	/* es */
	if (!noise_mix_dh(hs->hs_ck, key, hs->hs_e, r->r_public))
		goto error;

	/* s */
	noise_msg_encrypt(es, l->l_public, NOISE_PUBLIC_KEY_LEN,
			  key, hs->hs_hash);

	/* ss */
	if (!noise_mix_ss(hs->hs_ck, key, r->r_ss))
		goto error;

	/* {t} */
	noise_tai64n_now(ets);
	noise_msg_encrypt(ets, ets, NOISE_TIMESTAMP_LEN, key, hs->hs_hash);

	*s_idx = noise_remote_index_insert(l, r);
	r->r_handshake_state = HANDSHAKE_INITIATOR;
	getnanouptime(&r->r_last_sent);
	ok = true;

error:
	lockmgr(&r->r_handshake_lock, LK_RELEASE);
	lockmgr(&l->l_identity_lock, LK_RELEASE);
	explicit_bzero(key, sizeof(key));
	return (ok);
}

struct noise_remote *
noise_consume_initiation(struct noise_local *l, uint32_t s_idx,
			 uint8_t ue[NOISE_PUBLIC_KEY_LEN],
			 uint8_t es[NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN],
			 uint8_t ets[NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN])
{
	struct noise_remote *r, *ret = NULL;
	struct noise_handshake hs;
	uint8_t key[NOISE_SYMMETRIC_KEY_LEN];
	uint8_t r_public[NOISE_PUBLIC_KEY_LEN];
	uint8_t timestamp[NOISE_TIMESTAMP_LEN];

	lockmgr(&l->l_identity_lock, LK_SHARED);

	if (!l->l_has_identity)
		goto error;

	noise_param_init(hs.hs_ck, hs.hs_hash, l->l_public);

	/* e */
	noise_msg_ephemeral(hs.hs_ck, hs.hs_hash, ue);

	/* es */
	if (!noise_mix_dh(hs.hs_ck, key, l->l_private, ue))
		goto error;

	/* s */
	if (!noise_msg_decrypt(r_public, es,
			       NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN,
			       key, hs.hs_hash))
		goto error;

	/* Lookup the remote we received from */
	if ((r = noise_remote_lookup(l, r_public)) == NULL)
		goto error;

	/* ss */
	if (!noise_mix_ss(hs.hs_ck, key, r->r_ss))
		goto error_put;

	/* {t} */
	if (!noise_msg_decrypt(timestamp, ets,
			       NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN,
			       key, hs.hs_hash))
		goto error_put;

	memcpy(hs.hs_e, ue, NOISE_PUBLIC_KEY_LEN);

	/*
	 * We have successfully computed the same results, now we ensure that
	 * this is not an initiation replay, or a flood attack.
	 */
	lockmgr(&r->r_handshake_lock, LK_EXCLUSIVE);

	/* Replay */
	if (memcmp(timestamp, r->r_timestamp, NOISE_TIMESTAMP_LEN) > 0)
		memcpy(r->r_timestamp, timestamp, NOISE_TIMESTAMP_LEN);
	else
		goto error_set;
	/* Flood attack */
	if (timer_expired(&r->r_last_init_recv, 0, REJECT_INTERVAL))
		getnanouptime(&r->r_last_init_recv);
	else
		goto error_set;

	/* Ok, we're happy to accept this initiation now */
	noise_remote_index_insert(l, r);
	r->r_index.i_remote_index = s_idx;
	r->r_handshake_state = HANDSHAKE_RESPONDER;
	r->r_handshake = hs;
	ret = noise_remote_ref(r);

error_set:
	lockmgr(&r->r_handshake_lock, LK_RELEASE);
error_put:
	noise_remote_put(r);
error:
	lockmgr(&l->l_identity_lock, LK_RELEASE);
	explicit_bzero(key, sizeof(key));
	explicit_bzero(&hs, sizeof(hs));
	return (ret);
}

bool
noise_create_response(struct noise_remote *r, uint32_t *s_idx,
		      uint32_t *r_idx, uint8_t ue[NOISE_PUBLIC_KEY_LEN],
		      uint8_t en[0 + NOISE_AUTHTAG_LEN])
{
	struct noise_handshake *hs = &r->r_handshake;
	struct noise_local *l = r->r_local;
	uint8_t key[NOISE_SYMMETRIC_KEY_LEN];
	uint8_t e[NOISE_PUBLIC_KEY_LEN];
	bool ok = false;

	lockmgr(&l->l_identity_lock, LK_SHARED);
	lockmgr(&r->r_handshake_lock, LK_EXCLUSIVE);

	if (r->r_handshake_state != HANDSHAKE_RESPONDER)
		goto error;

	/* e */
	curve25519_generate_secret(e);
	if (curve25519_generate_public(ue, e) == 0)
		goto error;
	noise_msg_ephemeral(hs->hs_ck, hs->hs_hash, ue);

	/* ee */
	if (!noise_mix_dh(hs->hs_ck, NULL, e, hs->hs_e))
		goto error;

	/* se */
	if (!noise_mix_dh(hs->hs_ck, NULL, e, r->r_public))
		goto error;

	/* psk */
	noise_mix_psk(hs->hs_ck, hs->hs_hash, key, r->r_psk);

	/* {} */
	noise_msg_encrypt(en, NULL, 0, key, hs->hs_hash);

	if (noise_begin_session(r)) {
		getnanouptime(&r->r_last_sent);
		*s_idx = r->r_index.i_local_index;
		*r_idx = r->r_index.i_remote_index;
		ok = true;
	}

error:
	lockmgr(&r->r_handshake_lock, LK_RELEASE);
	lockmgr(&l->l_identity_lock, LK_RELEASE);
	explicit_bzero(key, sizeof(key));
	explicit_bzero(e, sizeof(e));
	return (ok);
}

struct noise_remote *
noise_consume_response(struct noise_local *l, uint32_t s_idx, uint32_t r_idx,
		       uint8_t ue[NOISE_PUBLIC_KEY_LEN],
		       uint8_t en[0 + NOISE_AUTHTAG_LEN])
{
	struct noise_remote *r, *ret = NULL;
	struct noise_handshake hs;
	uint8_t preshared_key[NOISE_SYMMETRIC_KEY_LEN];
	uint8_t key[NOISE_SYMMETRIC_KEY_LEN];

	r = noise_remote_index_lookup(l, r_idx, false);
	if (r == NULL)
		return (NULL);

	lockmgr(&l->l_identity_lock, LK_SHARED);
	if (!l->l_has_identity)
		goto error;

	lockmgr(&r->r_handshake_lock, LK_SHARED);
	if (r->r_handshake_state != HANDSHAKE_INITIATOR) {
		lockmgr(&r->r_handshake_lock, LK_RELEASE);
		goto error;
	}
	memcpy(preshared_key, r->r_psk, NOISE_SYMMETRIC_KEY_LEN);
	hs = r->r_handshake;
	lockmgr(&r->r_handshake_lock, LK_RELEASE);

	/* e */
	noise_msg_ephemeral(hs.hs_ck, hs.hs_hash, ue);

	/* ee */
	if (!noise_mix_dh(hs.hs_ck, NULL, hs.hs_e, ue))
		goto error_zero;

	/* se */
	if (!noise_mix_dh(hs.hs_ck, NULL, l->l_private, ue))
		goto error_zero;

	/* psk */
	noise_mix_psk(hs.hs_ck, hs.hs_hash, key, preshared_key);

	/* {} */
	if (!noise_msg_decrypt(NULL, en, 0 + NOISE_AUTHTAG_LEN, key,
			       hs.hs_hash))
		goto error_zero;

	lockmgr(&r->r_handshake_lock, LK_EXCLUSIVE);
	if (r->r_handshake_state == HANDSHAKE_INITIATOR &&
	    r->r_index.i_local_index == r_idx) {
		r->r_handshake = hs;
		r->r_index.i_remote_index = s_idx;
		if (noise_begin_session(r))
			ret = noise_remote_ref(r);
	}
	lockmgr(&r->r_handshake_lock, LK_RELEASE);

error_zero:
	explicit_bzero(preshared_key, sizeof(preshared_key));
	explicit_bzero(key, sizeof(key));
	explicit_bzero(&hs, sizeof(hs));
error:
	lockmgr(&l->l_identity_lock, LK_RELEASE);
	noise_remote_put(r);
	return (ret);
}

/*----------------------------------------------------------------------------*/
/* Handshake helper functions */

static bool
noise_begin_session(struct noise_remote *r)
{
	struct noise_local *l = r->r_local;
	struct noise_keypair *kp, *next, *current, *previous;
	struct noise_index *r_i;

	KKASSERT(lockstatus(&r->r_handshake_lock, curthread) == LK_EXCLUSIVE);

	kp = kmalloc(sizeof(*kp), M_NOISE, M_NOWAIT | M_ZERO);
	if (kp == NULL)
		return (false);

	/*
	 * Initialize the new keypair.
	 */
	refcount_init(&kp->kp_refcnt, 1);
	kp->kp_can_send = true;
	kp->kp_is_initiator = (r->r_handshake_state == HANDSHAKE_INITIATOR);
	kp->kp_remote = noise_remote_ref(r);
	getnanouptime(&kp->kp_birthdate);
	lockinit(&kp->kp_counter_lock, "noise_counter", 0, 0);

	noise_kdf((kp->kp_is_initiator ? kp->kp_send : kp->kp_recv),
		  (kp->kp_is_initiator ? kp->kp_recv : kp->kp_send),
		  NULL, NULL,
		  NOISE_SYMMETRIC_KEY_LEN, NOISE_SYMMETRIC_KEY_LEN, 0, 0,
		  r->r_handshake.hs_ck);

	/*
	 * Rotate existing keypairs and load the new one.
	 */
	lockmgr(&r->r_keypair_lock, LK_EXCLUSIVE);
	next = atomic_load_ptr(&r->r_keypair_next);
	current = atomic_load_ptr(&r->r_keypair_current);
	previous = atomic_load_ptr(&r->r_keypair_previous);
	if (kp->kp_is_initiator) {
		/*
		 * Received a confirmation response, which means this new
		 * keypair can now be used.
		 *
		 * Rotate the existing keypair ("current" or "next" slot)
		 * to the "previous" slot, and load the new keypair to the
		 * "current" slot.
		 */
		if (next != NULL) {
			atomic_store_ptr(&r->r_keypair_next, NULL);
			atomic_store_ptr(&r->r_keypair_previous, next);
			noise_keypair_drop(current);
		} else {
			atomic_store_ptr(&r->r_keypair_previous, current);
		}
		noise_keypair_drop(previous);
		atomic_store_ptr(&r->r_keypair_current, kp);
	} else {
		/*
		 * This new keypair cannot be used until we receive a
		 * confirmation via the first data packet.
		 *
		 * So drop the "previous" keypair, the possibly existing
		 * "next" one, and load the new keypair to the "next" slot.
		 */
		atomic_store_ptr(&r->r_keypair_next, kp);
		noise_keypair_drop(next);
		atomic_store_ptr(&r->r_keypair_previous, NULL);
		noise_keypair_drop(previous);
	}
	lockmgr(&r->r_keypair_lock, LK_RELEASE);

	/*
	 * Insert into index hashtable, replacing the existing remote index
	 * (added with handshake initiation creation/consumption).
	 */
	r_i = &r->r_index;
	kp->kp_index.i_is_keypair = true;
	kp->kp_index.i_local_index = r_i->i_local_index;
	kp->kp_index.i_remote_index = r_i->i_remote_index;

	KKASSERT(lockstatus(&r->r_handshake_lock, curthread) == LK_EXCLUSIVE);
	lockmgr(&l->l_index_lock, LK_EXCLUSIVE);
	LIST_INSERT_BEFORE(r_i, &kp->kp_index, i_entry);
	r->r_handshake_state = HANDSHAKE_DEAD;
	LIST_REMOVE(r_i, i_entry);
	lockmgr(&l->l_index_lock, LK_RELEASE);

	explicit_bzero(&r->r_handshake, sizeof(r->r_handshake));
	return (true);
}

static void
noise_kdf(uint8_t *a, uint8_t *b, uint8_t *c, const uint8_t *x,
	  size_t a_len, size_t b_len, size_t c_len, size_t x_len,
	  const uint8_t ck[NOISE_HASH_LEN])
{
	uint8_t out[BLAKE2S_HASH_SIZE + 1];
	uint8_t sec[BLAKE2S_HASH_SIZE];

	KKASSERT(a != NULL && a_len > 0);
	KKASSERT(a_len <= BLAKE2S_HASH_SIZE &&
		 b_len <= BLAKE2S_HASH_SIZE &&
		 c_len <= BLAKE2S_HASH_SIZE);

	/* Extract entropy from "x" into sec */
	blake2s_hmac(sec, x, ck, BLAKE2S_HASH_SIZE /* outlen */,
		     x_len /* inlen */, NOISE_HASH_LEN);

	/* Expand first key: key = sec, data = 0x1 */
	out[0] = 1;
	blake2s_hmac(out, out, sec, BLAKE2S_HASH_SIZE /* outlen */,
		     1 /* inlen */, BLAKE2S_HASH_SIZE);
	memcpy(a, out, a_len);

	if (b == NULL || b_len == 0)
		goto out;

	/* Expand second key: key = sec, data = "a" || 0x2 */
	out[BLAKE2S_HASH_SIZE] = 2;
	blake2s_hmac(out, out, sec, BLAKE2S_HASH_SIZE /* outlen */,
		     BLAKE2S_HASH_SIZE + 1 /* inlen */, BLAKE2S_HASH_SIZE);
	memcpy(b, out, b_len);

	if (c == NULL || c_len == 0)
		goto out;

	/* Expand third key: key = sec, data = "b" || 0x3 */
	out[BLAKE2S_HASH_SIZE] = 3;
	blake2s_hmac(out, out, sec, BLAKE2S_HASH_SIZE /* outlen */,
		     BLAKE2S_HASH_SIZE + 1 /* inlen */, BLAKE2S_HASH_SIZE);
	memcpy(c, out, c_len);

out:
	/* Clear sensitive data from stack */
	explicit_bzero(sec, sizeof(sec));
	explicit_bzero(out, sizeof(out));
}

static bool
noise_mix_dh(uint8_t ck[NOISE_HASH_LEN], uint8_t key[NOISE_SYMMETRIC_KEY_LEN],
	     const uint8_t private[NOISE_PUBLIC_KEY_LEN],
	     const uint8_t public[NOISE_PUBLIC_KEY_LEN])
{
	uint8_t dh[NOISE_PUBLIC_KEY_LEN];

	if (!curve25519(dh, private, public))
		return (false);

	noise_kdf(ck, key, NULL, dh,
		  NOISE_HASH_LEN, NOISE_SYMMETRIC_KEY_LEN, 0,
		  NOISE_PUBLIC_KEY_LEN, ck);
	explicit_bzero(dh, NOISE_PUBLIC_KEY_LEN);
	return (true);
}

static bool
noise_mix_ss(uint8_t ck[NOISE_HASH_LEN], uint8_t key[NOISE_SYMMETRIC_KEY_LEN],
	     const uint8_t ss[NOISE_PUBLIC_KEY_LEN])
{
	static uint8_t null_point[NOISE_PUBLIC_KEY_LEN];

	if (timingsafe_bcmp(ss, null_point, NOISE_PUBLIC_KEY_LEN) == 0)
		return (false);

	noise_kdf(ck, key, NULL, ss,
		  NOISE_HASH_LEN, NOISE_SYMMETRIC_KEY_LEN,
		  0, NOISE_PUBLIC_KEY_LEN, ck);
	return (true);
}

static void
noise_mix_hash(uint8_t hash[NOISE_HASH_LEN], const uint8_t *src,
	       size_t src_len)
{
	struct blake2s_state blake;

	blake2s_init(&blake, NOISE_HASH_LEN);
	blake2s_update(&blake, hash, NOISE_HASH_LEN);
	blake2s_update(&blake, src, src_len);
	blake2s_final(&blake, hash);
}

static void
noise_mix_psk(uint8_t ck[NOISE_HASH_LEN], uint8_t hash[NOISE_HASH_LEN],
	      uint8_t key[NOISE_SYMMETRIC_KEY_LEN],
	      const uint8_t psk[NOISE_SYMMETRIC_KEY_LEN])
{
	uint8_t tmp[NOISE_HASH_LEN];

	noise_kdf(ck, tmp, key, psk,
		  NOISE_HASH_LEN, NOISE_HASH_LEN, NOISE_SYMMETRIC_KEY_LEN,
		  NOISE_SYMMETRIC_KEY_LEN, ck);
	noise_mix_hash(hash, tmp, NOISE_HASH_LEN);
	explicit_bzero(tmp, NOISE_HASH_LEN);
}

static void
noise_param_init(uint8_t ck[NOISE_HASH_LEN], uint8_t hash[NOISE_HASH_LEN],
		 const uint8_t s[NOISE_PUBLIC_KEY_LEN])
{
	struct blake2s_state blake;

	blake2s(ck, NOISE_HANDSHAKE_NAME, NULL,
		NOISE_HASH_LEN, sizeof(NOISE_HANDSHAKE_NAME) - 1, 0);
	blake2s_init(&blake, NOISE_HASH_LEN);
	blake2s_update(&blake, ck, NOISE_HASH_LEN);
	blake2s_update(&blake, NOISE_IDENTIFIER_NAME,
		       sizeof(NOISE_IDENTIFIER_NAME) - 1);
	blake2s_final(&blake, hash);

	noise_mix_hash(hash, s, NOISE_PUBLIC_KEY_LEN);
}

static void
noise_msg_encrypt(uint8_t *dst, const uint8_t *src, size_t src_len,
		  uint8_t key[NOISE_SYMMETRIC_KEY_LEN],
		  uint8_t hash[NOISE_HASH_LEN])
{
	/* Nonce always zero for Noise_IK */
	static const uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE] = { 0 };

	chacha20poly1305_encrypt(dst, src, src_len, hash, NOISE_HASH_LEN,
				 nonce, key);
	noise_mix_hash(hash, dst, src_len + NOISE_AUTHTAG_LEN);
}

static bool
noise_msg_decrypt(uint8_t *dst, const uint8_t *src, size_t src_len,
		  uint8_t key[NOISE_SYMMETRIC_KEY_LEN],
		  uint8_t hash[NOISE_HASH_LEN])
{
	/* Nonce always zero for Noise_IK */
	static const uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE] = { 0 };

	if (!chacha20poly1305_decrypt(dst, src, src_len,
				      hash, NOISE_HASH_LEN, nonce, key))
		return (false);

	noise_mix_hash(hash, src, src_len);
	return (true);
}

static void
noise_msg_ephemeral(uint8_t ck[NOISE_HASH_LEN], uint8_t hash[NOISE_HASH_LEN],
		    const uint8_t src[NOISE_PUBLIC_KEY_LEN])
{
	noise_mix_hash(hash, src, NOISE_PUBLIC_KEY_LEN);
	noise_kdf(ck, NULL, NULL, src, NOISE_HASH_LEN, 0, 0,
		  NOISE_PUBLIC_KEY_LEN, ck);
}

static void
noise_tai64n_now(uint8_t output[NOISE_TIMESTAMP_LEN])
{
	struct timespec time;
	uint64_t sec;
	uint32_t nsec;

	getnanotime(&time);

	/* Round down the nsec counter to limit precise timing leak. */
	time.tv_nsec &= REJECT_INTERVAL_MASK;

	/* https://cr.yp.to/libtai/tai64.html */
	sec = htobe64(0x400000000000000aULL + time.tv_sec);
	nsec = htobe32(time.tv_nsec);

	/* memcpy to output buffer, assuming output could be unaligned. */
	memcpy(output, &sec, sizeof(sec));
	memcpy(output + sizeof(sec), &nsec, sizeof(nsec));
}

#ifdef WG_SELFTESTS
#include "selftest/counter.c"
#endif /* WG_SELFTESTS */
