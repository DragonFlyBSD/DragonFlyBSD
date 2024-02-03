/*-
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2015-2021 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (c) 2024 Aaron LI <aly@aaronly.me>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

static bool
test_aip_init(struct wg_softc *sc)
{
	memset(sc, 0, sizeof(*sc));

	lockinit(&sc->sc_aip_lock, "aip_lock (test)", 0, 0);

	if (!rn_inithead(&sc->sc_aip4, wg_maskhead,
			 offsetof(struct aip_addr, in)))
		return (false);
	if (!rn_inithead(&sc->sc_aip6, wg_maskhead,
			 offsetof(struct aip_addr, in6)))
		return (false);

	return (true);
}

static void
test_aip_deinit(struct wg_softc *sc)
{
	if (sc->sc_aip4 != NULL) {
		rn_freehead(sc->sc_aip4);
		sc->sc_aip4 = NULL;
	}
	if (sc->sc_aip6 != NULL) {
		rn_freehead(sc->sc_aip6);
		sc->sc_aip6 = NULL;
	}

	lockuninit(&sc->sc_aip_lock);
}

static struct wg_peer *
test_aip_peer_new(struct wg_softc *sc)
{
	struct wg_peer *peer;

	peer = kmalloc(sizeof(*peer), M_WG, M_NOWAIT | M_ZERO);
	if (peer == NULL)
		return (NULL);

	peer->p_sc = sc;
	LIST_INIT(&peer->p_aips);
	peer->p_aips_num = 0;
	/*
	 * WARNING:
	 * wg_aip_lookup() will reference 'peer->p_remote', so this member
	 * cannot be NULL.  Do a hack and just assign itself to it, so as
	 * no need to bother with creating noise_local and noise_remote.
	 * This is kind of dangerous, but probably fine.
	 */
	peer->p_remote = (struct noise_remote *)peer;

	return (peer);
}

#ifdef WG_ALLOWEDIPS_RANDOMIZED_TEST

struct horrible_allowedips {
	LIST_HEAD(, horrible_allowedips_node)	head;
};

struct horrible_allowedips_node {
	LIST_ENTRY(horrible_allowedips_node)	entry;
	struct aip_addr				ip;
	struct aip_addr				mask;
	uint8_t					ip_version;
	void					*value;
};

static void
horrible_allowedips_init(struct horrible_allowedips *table)
{
	LIST_INIT(&table->head);
}

static void
horrible_allowedips_flush(struct horrible_allowedips *table)
{
	struct horrible_allowedips_node *node, *node_;

	LIST_FOREACH_MUTABLE(node, &table->head, entry, node_) {
		LIST_REMOVE(node, entry);
		kfree(node, M_WG);
	}
}

static inline void
horrible_cidr_to_mask(uint8_t cidr, struct aip_addr *mask)
{
	uint8_t n;

	memset(&mask->in6, 0x00, sizeof(mask->in6));
	memset(&mask->in6, 0xff, cidr / 8);
	if ((n = cidr % 32) != 0)
		mask->ip6[cidr / 32] =
		    (uint32_t)htonl((0xFFFFFFFFUL << (32 - n)) & 0xFFFFFFFFUL);
}

static inline uint8_t
horrible_mask_to_cidr(const struct aip_addr *mask)
{
	return (bitcount32(mask->ip6[0]) +
		bitcount32(mask->ip6[1]) +
		bitcount32(mask->ip6[2]) +
		bitcount32(mask->ip6[3]));
}

static inline void
horrible_mask_self(struct horrible_allowedips_node *node)
{
	KKASSERT(node->ip_version == 4 || node->ip_version == 6);

	if (node->ip_version == 4) {
		node->ip.ip &= node->mask.ip;
	} else {
		node->ip.ip6[0] &= node->mask.ip6[0];
		node->ip.ip6[1] &= node->mask.ip6[1];
		node->ip.ip6[2] &= node->mask.ip6[2];
		node->ip.ip6[3] &= node->mask.ip6[3];
	}
}

static inline bool
horrible_match_v4(const struct horrible_allowedips_node *node,
		  const struct in_addr *ip)
{
	return ((ip->s_addr & node->mask.ip) == node->ip.ip);
}

static inline bool
horrible_match_v6(const struct horrible_allowedips_node *node,
		  const struct in6_addr *ip)
{
#define IP6MATCH_SEG(ip, node, n) \
	((ip->__u6_addr.__u6_addr32[n] & node->mask.ip6[n]) \
	 == node->ip.ip6[n])

	return (IP6MATCH_SEG(ip, node, 0) &&
		IP6MATCH_SEG(ip, node, 1) &&
		IP6MATCH_SEG(ip, node, 2) &&
		IP6MATCH_SEG(ip, node, 3));

#undef IP6MATCH_SEG
}

static void
horrible_insert_ordered(struct horrible_allowedips *table,
			struct horrible_allowedips_node *node)
{
	struct horrible_allowedips_node *other, *where;
	uint8_t my_cidr;

	other = where = NULL;
	my_cidr = horrible_mask_to_cidr(&node->mask);

	LIST_FOREACH(other, &table->head, entry) {
		if (!memcmp(&other->mask, &node->mask,
			    sizeof(struct aip_addr)) &&
		    !memcmp(&other->ip, &node->ip,
			    sizeof(struct aip_addr)) &&
		    other->ip_version == node->ip_version)
		{
			other->value = node->value;
			kfree(node, M_WG);
			return;
		}
		where = other;
		if (horrible_mask_to_cidr(&other->mask) <= my_cidr)
			break;
	}

	if (other == NULL && where == NULL)
		LIST_INSERT_HEAD(&table->head, node, entry);
	else if (other == NULL)
		LIST_INSERT_AFTER(where, node, entry);
	else
		LIST_INSERT_BEFORE(where, node, entry);
}

static int
horrible_allowedips_insert_v4(struct horrible_allowedips *table,
			      const void *ip, uint8_t cidr, void *value)
{
	struct horrible_allowedips_node *node;

	node = kmalloc(sizeof(*node), M_WG, M_NOWAIT | M_ZERO);
	if (node == NULL)
		return (ENOMEM);

	node->ip.in = *(const struct in_addr *)ip;
	horrible_cidr_to_mask(cidr, &node->mask);
	node->ip_version = 4;
	node->value = value;

	horrible_mask_self(node);
	horrible_insert_ordered(table, node);

	return (0);
}

static int
horrible_allowedips_insert_v6(struct horrible_allowedips *table,
			      const void *ip, uint8_t cidr, void *value)
{
	struct horrible_allowedips_node *node;

	node = kmalloc(sizeof(*node), M_WG, M_NOWAIT | M_ZERO);
	if (node == NULL)
		return (ENOMEM);

	node->ip.in6 = *(const struct in6_addr *)ip;
	horrible_cidr_to_mask(cidr, &node->mask);
	node->ip_version = 6;
	node->value = value;

	horrible_mask_self(node);
	horrible_insert_ordered(table, node);

	return (0);
}

static void *
horrible_allowedips_lookup_v4(const struct horrible_allowedips *table,
			      const void *ip)
{
	struct horrible_allowedips_node *node;
	void *ret = NULL;

	LIST_FOREACH(node, &table->head, entry) {
		if (node->ip_version != 4)
			continue;
		if (horrible_match_v4(node, (const struct in_addr *)ip)) {
			ret = node->value;
			break;
		}
	}

	return (ret);
}

static void *
horrible_allowedips_lookup_v6(const struct horrible_allowedips *table,
			      const void *ip)
{
	struct horrible_allowedips_node *node;
	void *ret = NULL;

	LIST_FOREACH(node, &table->head, entry) {
		if (node->ip_version != 6)
			continue;
		if (horrible_match_v6(node, (const struct in6_addr *)ip)) {
			ret = node->value;
			break;
		}
	}

	return (ret);
}

#define T_NUM_PEERS		2000
#define T_NUM_RAND_ROUTES	400
#define T_NUM_MUTATED_ROUTES	100
#define T_NUM_QUERIES		(T_NUM_RAND_ROUTES * T_NUM_MUTATED_ROUTES * 30)

static bool
wg_allowedips_randomized_test(void)
{
	struct horrible_allowedips h;
	struct wg_softc sc;
	struct wg_peer **peers, *peer;
	unsigned int i, j, k, p, nextp, mutate_amount;
	uint8_t ip[16], mutated[16], mutate_mask[16], cidr;
	bool ret = false;

	peers = NULL;

	horrible_allowedips_init(&h);
	if (!test_aip_init(&sc)) {
		kprintf("%s: FAIL: test_aip_init\n", __func__);
		goto error;
	}
	peers = kmalloc(T_NUM_PEERS * sizeof(*peers), M_WG, M_NOWAIT | M_ZERO);
	if (peers == NULL) {
		kprintf("%s: FAIL: peers malloc\n", __func__);
		goto error;
	}
	for (i = 0; i < T_NUM_PEERS; ++i) {
		peers[i] = test_aip_peer_new(&sc);
		if (peers[i] == NULL) {
			kprintf("%s: FAIL: peer_new\n", __func__);
			goto error;
		}
	}

	kprintf("%s: inserting v4 routes: ", __func__);
	for (i = 0, nextp = 0; i < T_NUM_RAND_ROUTES; ++i) {
		if ((p = i * 100 / T_NUM_RAND_ROUTES) == nextp) {
			kprintf("%d%%...", p);
			nextp += 10;
		}
		karc4random_buf(ip, 4);
		cidr = karc4random_uniform(32) + 1;
		peer = peers[karc4random_uniform(T_NUM_PEERS)];
		if (wg_aip_add(&sc, peer, AF_INET, ip, cidr)) {
			kprintf("%s: FAIL: wg_aip_add(v4)\n", __func__);
			goto error;
		}
		if (horrible_allowedips_insert_v4(&h, ip, cidr, peer)) {
			kprintf("%s: FAIL: insert_v4\n", __func__);
			goto error;
		}
		for (j = 0; j < T_NUM_MUTATED_ROUTES; ++j) {
			memcpy(mutated, ip, 4);
			karc4random_buf(mutate_mask, 4);
			mutate_amount = karc4random_uniform(32);
			for (k = 0; k < mutate_amount / 8; ++k)
				mutate_mask[k] = 0xff;
			mutate_mask[k] =
			    0xff << ((8 - (mutate_amount % 8)) % 8);
			for (; k < 4; ++k)
				mutate_mask[k] = 0;
			for (k = 0; k < 4; ++k) {
				mutated[k] = (mutated[k] & mutate_mask[k]) |
					     (~mutate_mask[k] &
					      karc4random_uniform(256));
			}
			cidr = karc4random_uniform(32) + 1;
			peer = peers[karc4random_uniform(T_NUM_PEERS)];
			if (wg_aip_add(&sc, peer, AF_INET, mutated, cidr)) {
				kprintf("%s: FAIL: wg_aip_add(v4)\n", __func__);
				goto error;
			}
			if (horrible_allowedips_insert_v4(&h, mutated, cidr,
							  peer)) {
				kprintf("%s: FAIL: insert_v4\n", __func__);
				goto error;
			}
		}
	}
	kprintf("done\n");

	kprintf("%s: v4 looking up: ", __func__);
	for (i = 0, nextp = 0; i < T_NUM_QUERIES; ++i) {
		if ((p = i * 100 / T_NUM_QUERIES) == nextp) {
			kprintf("%d%%...", p);
			nextp += 5;
		}
		karc4random_buf(ip, 4);
		if (wg_aip_lookup(&sc, AF_INET, ip) !=
		    horrible_allowedips_lookup_v4(&h, ip)) {
			kprintf("%s: FAIL: lookup_v4\n", __func__);
			goto error;
		}
	}
	kprintf("pass\n");

	/*
	 * Flush existing v4 routes for the following v6 test so as to
	 * significantly reduce the test time.
	 */
	horrible_allowedips_flush(&h);

	kprintf("%s: inserting v6 routes: ", __func__);
	for (i = 0, nextp = 0; i < T_NUM_RAND_ROUTES; ++i) {
		if ((p = i * 100 / T_NUM_RAND_ROUTES) == nextp) {
			kprintf("%d%%...", p);
			nextp += 10;
		}
		karc4random_buf(ip, 16);
		cidr = karc4random_uniform(128) + 1;
		peer = peers[karc4random_uniform(T_NUM_PEERS)];
		if (wg_aip_add(&sc, peer, AF_INET6, ip, cidr)) {
			kprintf("%s: FAIL: wg_aip_add(v6)\n", __func__);
			goto error;
		}
		if (horrible_allowedips_insert_v6(&h, ip, cidr, peer)) {
			kprintf("%s: FAIL: insert_v6\n", __func__);
			goto error;
		}
		for (j = 0; j < T_NUM_MUTATED_ROUTES; ++j) {
			memcpy(mutated, ip, 16);
			karc4random_buf(mutate_mask, 16);
			mutate_amount = karc4random_uniform(128);
			for (k = 0; k < mutate_amount / 8; ++k)
				mutate_mask[k] = 0xff;
			mutate_mask[k] =
			    0xff << ((8 - (mutate_amount % 8)) % 8);
			for (; k < 4; ++k)
				mutate_mask[k] = 0;
			for (k = 0; k < 4; ++k) {
				mutated[k] = (mutated[k] & mutate_mask[k]) |
					     (~mutate_mask[k] &
					      karc4random_uniform(256));
			}
			cidr = karc4random_uniform(128) + 1;
			peer = peers[karc4random_uniform(T_NUM_PEERS)];
			if (wg_aip_add(&sc, peer, AF_INET6, mutated, cidr)) {
				kprintf("%s: FAIL: wg_aip_add(v6)\n", __func__);
				goto error;
			}
			if (horrible_allowedips_insert_v6(&h, mutated, cidr,
							  peer)) {
				kprintf("%s: FAIL: insert_v6\n", __func__);
				goto error;
			}
		}
	}
	kprintf("done\n");

	kprintf("%s: v6 looking up: ", __func__);
	for (i = 0, nextp = 0; i < T_NUM_QUERIES; ++i) {
		if ((p = i * 100 / T_NUM_QUERIES) == nextp) {
			kprintf("%d%%...", p);
			nextp += 5;
		}
		karc4random_buf(ip, 16);
		if (wg_aip_lookup(&sc, AF_INET6, ip) !=
		    horrible_allowedips_lookup_v6(&h, ip)) {
			kprintf("%s: FAIL: lookup_v6\n", __func__);
			goto error;
		}
	}
	kprintf("pass\n");

	ret = true;
	kprintf("%s: pass\n", __func__);

error:
	horrible_allowedips_flush(&h);
	if (peers != NULL) {
		for (i = 0; i < T_NUM_PEERS; ++i) {
			if (peers[i] != NULL) {
				wg_aip_remove_all(&sc, peers[i]);
				kfree(peers[i], M_WG);
			}
		}
		kfree(peers, M_WG);
	}
	test_aip_deinit(&sc);

	return (ret);
}

#else /* !WG_ALLOWEDIPS_RANDOMIZED_TEST */

static inline bool
wg_allowedips_randomized_test(void)
{
	return (true);
}

#endif /* WG_ALLOWEDIPS_RANDOMIZED_TEST */

static inline void *
ip_make_v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	static struct in_addr ip;
	uint8_t *split = (uint8_t *)&ip;

	split[0] = a;
	split[1] = b;
	split[2] = c;
	split[3] = d;

	return (void *)&ip;
}

static inline void *
ip_make_v6(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	static struct in6_addr ip;
	uint32_t *split = ip.__u6_addr.__u6_addr32;

	split[0] = htobe32(a);
	split[1] = htobe32(b);
	split[2] = htobe32(c);
	split[3] = htobe32(d);

	return (void *)&ip;
}

static inline bool
ip_equal_v4(const void *ip, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	return (memcmp(ip, ip_make_v4(a, b, c, d), 4) == 0);
}

static inline bool
ip_equal_v6(const void *ip, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	return (memcmp(ip, ip_make_v6(a, b, c, d), 16) == 0);
}

#define T_INSERT(n, peer, af, ipa, ipb, ipc, ipd, cidr) do {		\
	const void *_ip = (af == AF_INET) ?				\
			  ip_make_v4(ipa, ipb, ipc, ipd) :		\
			  ip_make_v6(ipa, ipb, ipc, ipd);		\
	if (wg_aip_add(&sc, peer, af, _ip, cidr)) {			\
		kprintf("%s: insert #%d: FAIL\n", __func__, n);		\
		success = false;					\
	}								\
} while (0)

#define T_LOOKUP(n, op, peer, af, ipa, ipb, ipc, ipd) do {		\
	const void *_ip = (af == AF_INET) ?				\
			  ip_make_v4(ipa, ipb, ipc, ipd) :		\
			  ip_make_v6(ipa, ipb, ipc, ipd);		\
	if (!(wg_aip_lookup(&sc, af, _ip) op peer)) {			\
		kprintf("%s: lookup #%d: FAIL\n", __func__, n);		\
		success = false;					\
	}								\
} while (0)

#define T_NEW_PEER(p) do { 						\
	if ((p = test_aip_peer_new(&sc)) == NULL) {			\
		kprintf("%s: FAIL: peer_new(%s)\n", __func__, #p);	\
		goto error;						\
	}								\
} while (0)

#define T_NEW_PEERS() do {						\
	T_NEW_PEER(a);							\
	T_NEW_PEER(b);							\
	T_NEW_PEER(c);							\
	T_NEW_PEER(d);							\
	T_NEW_PEER(e);							\
	T_NEW_PEER(f);							\
	T_NEW_PEER(g);							\
	T_NEW_PEER(h);							\
} while (0)

#define T_CLEAR_PEERS() do {						\
	if (a != NULL)							\
		wg_aip_remove_all(&sc, a);				\
	if (b != NULL)							\
		wg_aip_remove_all(&sc, b);				\
	if (c != NULL)							\
		wg_aip_remove_all(&sc, c);				\
	if (d != NULL)							\
		wg_aip_remove_all(&sc, d);				\
	if (e != NULL)							\
		wg_aip_remove_all(&sc, e);				\
	if (f != NULL)							\
		wg_aip_remove_all(&sc, f);				\
	if (g != NULL)							\
		wg_aip_remove_all(&sc, g);				\
	if (h != NULL)							\
		wg_aip_remove_all(&sc, h);				\
} while (0)

#define T_FREE_PEERS() do {						\
	T_CLEAR_PEERS();						\
	if (a != NULL)							\
		kfree(a, M_WG);						\
	if (b != NULL)							\
		kfree(b, M_WG);						\
	if (c != NULL)							\
		kfree(c, M_WG);						\
	if (d != NULL)							\
		kfree(d, M_WG);						\
	if (e != NULL)							\
		kfree(e, M_WG);						\
	if (f != NULL)							\
		kfree(f, M_WG);						\
	if (g != NULL)							\
		kfree(g, M_WG);						\
	if (h != NULL)							\
		kfree(h, M_WG);						\
} while (0)

static bool
wg_allowedips_lookup_test(void)
{
	struct wg_softc sc;
	struct wg_peer *a = NULL, *b = NULL, *c = NULL, *d = NULL;
	struct wg_peer *e = NULL, *f = NULL, *g = NULL, *h = NULL;
	struct wg_aip *aip;
	size_t i, count;
	bool found_a, found_b, found_c, found_d, found_e, found_other;
	bool success = false;

	if (!test_aip_init(&sc)) {
		kprintf("%s: FAIL: test_aip_init\n", __func__);
		goto error;
	}

	T_NEW_PEERS();

	T_INSERT( 1, a, AF_INET, 192, 168, 4, 0, 24);
	T_INSERT( 2, b, AF_INET, 192, 168, 4, 4, 32);
	T_INSERT( 3, c, AF_INET, 192, 168, 0, 0, 16);
	T_INSERT( 4, d, AF_INET, 192, 95, 5, 64, 27);
	/* replaces previous entry, and maskself is required */
	T_INSERT( 5, c, AF_INET, 192, 95, 5, 65, 27);
	T_INSERT( 6, d, AF_INET6,
		 0x26075300, 0x60006b00, 0, 0xc05f0543, 128);
	T_INSERT( 7, c, AF_INET6,
		 0x26075300, 0x60006b00, 0, 0, 64);
	T_INSERT( 8, e, AF_INET, 0, 0, 0, 0, 0);
	T_INSERT( 9, e, AF_INET6, 0, 0, 0, 0, 0);
	/* replaces previous entry */
	T_INSERT(10, f, AF_INET6, 0, 0, 0, 0, 0);
	T_INSERT(11, g, AF_INET6, 0x24046800, 0, 0, 0, 32);
	/* maskself is required */
	T_INSERT(12, h, AF_INET6,
		 0x24046800, 0x40040800, 0xdeadbeef, 0xdeadbeef, 64);
	T_INSERT(13, a, AF_INET6,
		 0x24046800, 0x40040800, 0xdeadbeef, 0xdeadbeef, 128);
	T_INSERT(14, c, AF_INET6,
		0x24446800, 0x40e40800, 0xdeaebeef, 0xdefbeef, 128);
	T_INSERT(15, b, AF_INET6,
		 0x24446800, 0xf0e40800, 0xeeaebeef, 0, 98);
	T_INSERT(16, g, AF_INET, 64, 15, 112, 0, 20);
	/* maskself is required */
	T_INSERT(17, h, AF_INET, 64, 15, 123, 211, 25);
	T_INSERT(18, a, AF_INET, 10, 0, 0, 0, 25);
	T_INSERT(19, b, AF_INET, 10, 0, 0, 128, 25);
	T_INSERT(20, a, AF_INET, 10, 1, 0, 0, 30);
	T_INSERT(21, b, AF_INET, 10, 1, 0, 4, 30);
	T_INSERT(22, c, AF_INET, 10, 1, 0, 8, 29);
	T_INSERT(23, d, AF_INET, 10, 1, 0, 16, 29);

	success = true;

	T_LOOKUP( 1, ==, a, AF_INET, 192, 168, 4, 20);
	T_LOOKUP( 2, ==, a, AF_INET, 192, 168, 4, 0);
	T_LOOKUP( 3, ==, b, AF_INET, 192, 168, 4, 4);
	T_LOOKUP( 4, ==, c, AF_INET, 192, 168, 200, 182);
	T_LOOKUP( 5, ==, c, AF_INET, 192, 95, 5, 68);
	T_LOOKUP( 6, ==, e, AF_INET, 192, 95, 5, 96);
	T_LOOKUP( 7, ==, d, AF_INET6,
		 0x26075300, 0x60006b00, 0, 0xc05f0543);
	T_LOOKUP( 8, ==, c, AF_INET6,
		 0x26075300, 0x60006b00, 0, 0xc02e01ee);
	T_LOOKUP( 9, ==, f, AF_INET6,
		 0x26075300, 0x60006b01, 0, 0);
	T_LOOKUP(10, ==, g, AF_INET6,
		 0x24046800, 0x40040806, 0, 0x1006);
	T_LOOKUP(11, ==, g, AF_INET6,
		 0x24046800, 0x40040806, 0x1234, 0x5678);
	T_LOOKUP(12, ==, f, AF_INET6,
		 0x240467ff, 0x40040806, 0x1234, 0x5678);
	T_LOOKUP(13, ==, f, AF_INET6,
		 0x24046801, 0x40040806, 0x1234, 0x5678);
	T_LOOKUP(14, ==, h, AF_INET6,
		 0x24046800, 0x40040800, 0x1234, 0x5678);
	T_LOOKUP(15, ==, h, AF_INET6,
		 0x24046800, 0x40040800, 0, 0);
	T_LOOKUP(16, ==, h, AF_INET6,
		 0x24046800, 0x40040800, 0x10101010, 0x10101010);
	T_LOOKUP(17, ==, a, AF_INET6,
		 0x24046800, 0x40040800, 0xdeadbeef, 0xdeadbeef);
	T_LOOKUP(18, ==, g, AF_INET, 64, 15, 116, 26);
	T_LOOKUP(19, ==, g, AF_INET, 64, 15, 127, 3);
	T_LOOKUP(20, ==, g, AF_INET, 64, 15, 123, 1);
	T_LOOKUP(21, ==, h, AF_INET, 64, 15, 123, 128);
	T_LOOKUP(22, ==, h, AF_INET, 64, 15, 123, 129);
	T_LOOKUP(23, ==, a, AF_INET, 10, 0, 0, 52);
	T_LOOKUP(24, ==, b, AF_INET, 10, 0, 0, 220);
	T_LOOKUP(25, ==, a, AF_INET, 10, 1, 0, 2);
	T_LOOKUP(26, ==, b, AF_INET, 10, 1, 0, 6);
	T_LOOKUP(27, ==, c, AF_INET, 10, 1, 0, 10);
	T_LOOKUP(28, ==, d, AF_INET, 10, 1, 0, 20);

	T_INSERT(24, a, AF_INET, 1, 0, 0, 0, 32);
	T_INSERT(25, a, AF_INET, 64, 0, 0, 0, 32);
	T_INSERT(26, a, AF_INET, 128, 0, 0, 0, 32);
	T_INSERT(27, a, AF_INET, 192, 0, 0, 0, 32);
	T_INSERT(28, a, AF_INET, 255, 0, 0, 0, 32);
	wg_aip_remove_all(&sc, a);
	T_LOOKUP(29, !=, a, AF_INET, 1, 0, 0, 0);
	T_LOOKUP(30, !=, a, AF_INET, 64, 0, 0, 0);
	T_LOOKUP(31, !=, a, AF_INET, 128, 0, 0, 0);
	T_LOOKUP(32, !=, a, AF_INET, 192, 0, 0, 0);
	T_LOOKUP(33, !=, a, AF_INET, 255, 0, 0, 0);

	T_CLEAR_PEERS();
	T_INSERT(29, a, AF_INET, 192, 168, 0, 0, 16);
	T_INSERT(30, a, AF_INET, 192, 168, 0, 0, 24);
	wg_aip_remove_all(&sc, a);
	T_LOOKUP(34, !=, a, AF_INET, 192, 168, 0, 1);

	for (i = 0; i < 128; ++i) {
		uint64_t part = htobe64(~(1LLU << (i % 64)));
		struct in6_addr addr;
		memset(&addr, 0xff, 16);
		memcpy((uint8_t *)&addr + (i < 64) * 8, &part, 8);
		wg_aip_add(&sc, a, AF_INET6, &addr, 128);
	}

	T_CLEAR_PEERS();
	T_INSERT(31, a, AF_INET, 192, 95, 5, 93, 27);
	T_INSERT(32, a, AF_INET6,
		 0x26075300, 0x60006b00, 0, 0xc05f0543, 128);
	T_INSERT(33, a, AF_INET, 10, 1, 0, 20, 29);
	T_INSERT(34, a, AF_INET6,
		 0x26075300, 0x6d8a6bf8, 0xdab1f1df, 0xc05f1523, 83);
	T_INSERT(35, a, AF_INET6,
		 0x26075300, 0x6d8a6bf8, 0xdab1f1df, 0xc05f1523, 21);
	count = 0;
	found_a = found_b = found_c = found_d = found_e = found_other = false;
	LIST_FOREACH(aip, &a->p_aips, a_entry) {
		sa_family_t family = aip->a_af;
		uint8_t *ip = aip->a_addr.bytes;
		uint8_t cidr;

		if (family == AF_INET)
			cidr = bitcount32(aip->a_mask.ip);
		else if (family == AF_INET6)
			cidr = in6_mask2len(&aip->a_mask.in6, NULL);
		else
			continue;

		count++;

		if (cidr == 27 && family == AF_INET &&
		    ip_equal_v4(ip, 192, 95, 5, 64))
			found_a = true;
		else if (cidr == 128 && family == AF_INET6 &&
			 ip_equal_v6(ip, 0x26075300, 0x60006b00, 0, 0xc05f0543))
			found_b = true;
		else if (cidr == 29 && family == AF_INET &&
			 ip_equal_v4(ip, 10, 1, 0, 16))
			found_c = true;
		else if (cidr == 83 && family == AF_INET6 &&
			 ip_equal_v6(ip, 0x26075300, 0x6d8a6bf8, 0xdab1e000, 0))
			found_d = true;
		else if (cidr == 21 && family == AF_INET6 &&
			 ip_equal_v6(ip, 0x26075000, 0, 0, 0))
			found_e = true;
		else
			found_other = true;
	}
	if (!(count == 5 &&
	      found_a && found_b && found_c && found_d && found_e &&
	      !found_other)) {
		kprintf("%s: aips lookup: FAIL\n", __func__);
		success = false;
	}

	kprintf("%s: %s\n", __func__, success ? "pass" : "FAIL");

error:
	T_FREE_PEERS();
	test_aip_deinit(&sc);
	return (success);
}

#undef T_INSERT
#undef T_LOOKUP
#undef T_NEW_PEER
#undef T_NEW_PEERS
#undef T_CLEAR_PEERS
#undef T_FREE_PEERS

static bool
wg_allowedips_selftest(void)
{
	bool ret = true;

	ret &= wg_allowedips_lookup_test();
	ret &= wg_allowedips_randomized_test();

	kprintf("%s: %s\n", __func__, ret ? "pass" : "FAIL");
	return (ret);
}
