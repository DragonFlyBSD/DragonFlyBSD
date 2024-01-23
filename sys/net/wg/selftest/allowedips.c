/* SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2015-2021 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

static bool test_aip_init(struct wg_softc *sc)
{
	if (!rn_inithead((void **)&sc->sc_aip4, offsetof(struct aip_addr, in) * NBBY))
		return false;
	if (!rn_inithead((void **)&sc->sc_aip6, offsetof(struct aip_addr, in6) * NBBY))
		return false;
	RADIX_NODE_HEAD_LOCK_INIT(sc->sc_aip4);
	RADIX_NODE_HEAD_LOCK_INIT(sc->sc_aip6);
	return true;
}

static void test_aip_deinit(struct wg_softc *sc)
{
	if (sc->sc_aip4) {
		RADIX_NODE_HEAD_DESTROY(sc->sc_aip4);
		rn_detachhead((void **)&sc->sc_aip4);
	}
	if (sc->sc_aip6) {
		RADIX_NODE_HEAD_DESTROY(sc->sc_aip6);
		rn_detachhead((void **)&sc->sc_aip6);
	}
}

#ifdef WG_ALLOWEDIPS_RANDOMIZED_TEST
enum {
	NUM_PEERS = 2000,
	NUM_RAND_ROUTES = 400,
	NUM_MUTATED_ROUTES = 100,
	NUM_QUERIES = NUM_RAND_ROUTES * NUM_MUTATED_ROUTES * 30
};

struct horrible_allowedips {
	LIST_HEAD(, horrible_allowedips_node) head;
};

struct horrible_allowedips_node {
	LIST_ENTRY(horrible_allowedips_node) table;
	struct aip_addr ip;
	struct aip_addr mask;
	uint8_t ip_version;
	void *value;
};

static void horrible_allowedips_init(struct horrible_allowedips *table)
{
	LIST_INIT(&table->head);
}

static void horrible_allowedips_free(struct horrible_allowedips *table)
{
	struct horrible_allowedips_node *node, *temp_node;

	LIST_FOREACH_SAFE(node, &table->head, table, temp_node) {
		LIST_REMOVE(node, table);
		free(node, M_WG);
	}
}

static inline struct aip_addr horrible_cidr_to_mask(uint8_t cidr)
{
	struct aip_addr mask;

	memset(&mask.in6, 0x00, 128 / 8);
	memset(&mask.in6, 0xff, cidr / 8);
	if (cidr % 32)
		mask.ip6[cidr / 32] = (uint32_t)htonl(
			(0xFFFFFFFFUL << (32 - (cidr % 32))) & 0xFFFFFFFFUL);
	return mask;
}

static inline uint8_t horrible_mask_to_cidr(struct aip_addr subnet)
{
	return bitcount32(subnet.ip6[0]) + bitcount32(subnet.ip6[1]) +
	       bitcount32(subnet.ip6[2]) + bitcount32(subnet.ip6[3]);
}

static inline void
horrible_mask_self(struct horrible_allowedips_node *node)
{
	if (node->ip_version == 4) {
		node->ip.ip &= node->mask.ip;
	} else if (node->ip_version == 6) {
		node->ip.ip6[0] &= node->mask.ip6[0];
		node->ip.ip6[1] &= node->mask.ip6[1];
		node->ip.ip6[2] &= node->mask.ip6[2];
		node->ip.ip6[3] &= node->mask.ip6[3];
	}
}

static inline bool
horrible_match_v4(const struct horrible_allowedips_node *node,
		  struct in_addr *ip)
{
	return (ip->s_addr & node->mask.ip) == node->ip.ip;
}

static inline bool
horrible_match_v6(const struct horrible_allowedips_node *node,
		  struct in6_addr *ip)
{
	return (ip->__u6_addr.__u6_addr32[0] & node->mask.ip6[0]) ==
		       node->ip.ip6[0] &&
	       (ip->__u6_addr.__u6_addr32[1] & node->mask.ip6[1]) ==
		       node->ip.ip6[1] &&
	       (ip->__u6_addr.__u6_addr32[2] & node->mask.ip6[2]) ==
		       node->ip.ip6[2] &&
	       (ip->__u6_addr.__u6_addr32[3] & node->mask.ip6[3]) ==
		       node->ip.ip6[3];
}

static void
horrible_insert_ordered(struct horrible_allowedips *table,
			struct horrible_allowedips_node *node)
{
	struct horrible_allowedips_node *other = NULL, *where = NULL;
	uint8_t my_cidr = horrible_mask_to_cidr(node->mask);

	LIST_FOREACH(other, &table->head, table) {
		if (!memcmp(&other->mask, &node->mask,
			    sizeof(struct aip_addr)) &&
		    !memcmp(&other->ip, &node->ip,
			    sizeof(struct aip_addr)) &&
		    other->ip_version == node->ip_version) {
			other->value = node->value;
			free(node, M_WG);
			return;
		}
		where = other;
		if (horrible_mask_to_cidr(other->mask) <= my_cidr)
			break;
	}
	if (!other && !where)
		LIST_INSERT_HEAD(&table->head, node, table);
	else if (!other)
		LIST_INSERT_AFTER(where, node, table);
	else
		LIST_INSERT_BEFORE(where, node, table);
}

static int
horrible_allowedips_insert_v4(struct horrible_allowedips *table,
			      struct in_addr *ip, uint8_t cidr, void *value)
{
	struct horrible_allowedips_node *node = malloc(sizeof(*node), M_WG, M_NOWAIT | M_ZERO);

	if (!node)
		return ENOMEM;
	node->ip.in = *ip;
	node->mask = horrible_cidr_to_mask(cidr);
	node->ip_version = 4;
	node->value = value;
	horrible_mask_self(node);
	horrible_insert_ordered(table, node);
	return 0;
}

static int
horrible_allowedips_insert_v6(struct horrible_allowedips *table,
			      struct in6_addr *ip, uint8_t cidr, void *value)
{
	struct horrible_allowedips_node *node = malloc(sizeof(*node), M_WG, M_NOWAIT | M_ZERO);

	if (!node)
		return ENOMEM;
	node->ip.in6 = *ip;
	node->mask = horrible_cidr_to_mask(cidr);
	node->ip_version = 6;
	node->value = value;
	horrible_mask_self(node);
	horrible_insert_ordered(table, node);
	return 0;
}

static void *
horrible_allowedips_lookup_v4(struct horrible_allowedips *table,
			      struct in_addr *ip)
{
	struct horrible_allowedips_node *node;
	void *ret = NULL;

	LIST_FOREACH(node, &table->head, table) {
		if (node->ip_version != 4)
			continue;
		if (horrible_match_v4(node, ip)) {
			ret = node->value;
			break;
		}
	}
	return ret;
}

static void *
horrible_allowedips_lookup_v6(struct horrible_allowedips *table,
			      struct in6_addr *ip)
{
	struct horrible_allowedips_node *node;
	void *ret = NULL;

	LIST_FOREACH(node, &table->head, table) {
		if (node->ip_version != 6)
			continue;
		if (horrible_match_v6(node, ip)) {
			ret = node->value;
			break;
		}
	}
	return ret;
}

static bool randomized_test(void)
{
	unsigned int i, j, k, mutate_amount, cidr;
	uint8_t ip[16], mutate_mask[16], mutated[16];
	struct wg_peer **peers, *peer;
	struct horrible_allowedips h;
	struct wg_softc sc = {{ 0 }};
	bool ret = false;
	peers = mallocarray(NUM_PEERS, sizeof(*peers), M_WG, M_NOWAIT | M_ZERO);
	if (!peers) {
		printf("allowedips random self-test malloc: FAIL\n");
		goto free;
	}
	for (i = 0; i < NUM_PEERS; ++i) {
		peers[i] = malloc(sizeof(*peers[i]), M_WG, M_NOWAIT | M_ZERO);
		if (!peers[i]) {
			printf("allowedips random self-test malloc: FAIL\n");
			goto free;
		}
		LIST_INIT(&peers[i]->p_aips);
		peers[i]->p_aips_num = 0;
		peers[i]->p_remote = (struct noise_remote *)peers[i];
	}

	if (!test_aip_init(&sc)) {
		printf("allowedips random self-test malloc: FAIL\n");
		goto free;
	}
	horrible_allowedips_init(&h);

	for (i = 0; i < NUM_RAND_ROUTES; ++i) {
		arc4random_buf(ip, 4);
		cidr = arc4random_uniform(32) + 1;
		peer = peers[arc4random_uniform(NUM_PEERS)];
		if (wg_aip_add(&sc, peer, AF_INET, ip, cidr)) {
			printf("allowedips random self-test malloc: FAIL\n");
			goto free;
		}
		if (horrible_allowedips_insert_v4(&h, (struct in_addr *)ip,
						  cidr, peer)) {
			printf("allowedips random self-test malloc: FAIL\n");
			goto free;
		}
		for (j = 0; j < NUM_MUTATED_ROUTES; ++j) {
			memcpy(mutated, ip, 4);
			arc4random_buf(mutate_mask, 4);
			mutate_amount = arc4random_uniform(32);
			for (k = 0; k < mutate_amount / 8; ++k)
				mutate_mask[k] = 0xff;
			mutate_mask[k] = 0xff
					 << ((8 - (mutate_amount % 8)) % 8);
			for (; k < 4; ++k)
				mutate_mask[k] = 0;
			for (k = 0; k < 4; ++k)
				mutated[k] = (mutated[k] & mutate_mask[k]) |
					     (~mutate_mask[k] &
					      arc4random_uniform(256));
			cidr = arc4random_uniform(32) + 1;
			peer = peers[arc4random_uniform(NUM_PEERS)];
			if (wg_aip_add(&sc, peer, AF_INET, mutated, cidr)) {
				printf("allowedips random self-test malloc: FAIL\n");
				goto free;
			}
			if (horrible_allowedips_insert_v4(&h,
				(struct in_addr *)mutated, cidr, peer)) {
				printf("allowedips random self-test malloc: FAIL\n");
				goto free;
			}
		}
	}

	for (i = 0; i < NUM_RAND_ROUTES; ++i) {
		arc4random_buf(ip, 16);
		cidr = arc4random_uniform(128) + 1;
		peer = peers[arc4random_uniform(NUM_PEERS)];
		if (wg_aip_add(&sc, peer, AF_INET6, ip, cidr)) {
			printf("allowedips random self-test malloc: FAIL\n");
			goto free;
		}
		if (horrible_allowedips_insert_v6(&h, (struct in6_addr *)ip,
						  cidr, peer)) {
			printf("allowedips random self-test malloc: FAIL\n");
			goto free;
		}
		for (j = 0; j < NUM_MUTATED_ROUTES; ++j) {
			memcpy(mutated, ip, 16);
			arc4random_buf(mutate_mask, 16);
			mutate_amount = arc4random_uniform(128);
			for (k = 0; k < mutate_amount / 8; ++k)
				mutate_mask[k] = 0xff;
			mutate_mask[k] = 0xff
					 << ((8 - (mutate_amount % 8)) % 8);
			for (; k < 4; ++k)
				mutate_mask[k] = 0;
			for (k = 0; k < 4; ++k)
				mutated[k] = (mutated[k] & mutate_mask[k]) |
					     (~mutate_mask[k] &
					      arc4random_uniform(256));
			cidr = arc4random_uniform(128) + 1;
			peer = peers[arc4random_uniform(NUM_PEERS)];
			if (wg_aip_add(&sc, peer, AF_INET6, mutated, cidr)) {
				printf("allowedips random self-test malloc: FAIL\n");
				goto free;
			}
			if (horrible_allowedips_insert_v6(
				    &h, (struct in6_addr *)mutated, cidr,
				    peer)) {
				printf("allowedips random self-test malloc: FAIL\n");
				goto free;
			}
		}
	}

	for (i = 0; i < NUM_QUERIES; ++i) {
		arc4random_buf(ip, 4);
		if (wg_aip_lookup(&sc, AF_INET, ip) !=
		    horrible_allowedips_lookup_v4(&h, (struct in_addr *)ip)) {
			printf("allowedips random self-test: FAIL\n");
			goto free;
		}
	}

	for (i = 0; i < NUM_QUERIES; ++i) {
		arc4random_buf(ip, 16);
		if (wg_aip_lookup(&sc, AF_INET6, ip) !=
		    horrible_allowedips_lookup_v6(&h, (struct in6_addr *)ip)) {
			printf("allowedips random self-test: FAIL\n");
			goto free;
		}
	}
	ret = true;

free:
	horrible_allowedips_free(&h);
	if (peers) {
		for (i = 0; i < NUM_PEERS; ++i)
			wg_aip_remove_all(&sc, peers[i]);
		for (i = 0; i < NUM_PEERS; ++i)
			free(peers[i], M_WG);
	}
	free(peers, M_WG);
	test_aip_deinit(&sc);
	return ret;
}
#endif

static struct in_addr *ip4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	static struct in_addr ip;
	uint8_t *split = (uint8_t *)&ip;

	split[0] = a;
	split[1] = b;
	split[2] = c;
	split[3] = d;
	return &ip;
}

static struct in6_addr *ip6(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	static struct in6_addr ip;
	uint32_t *split = ip.__u6_addr.__u6_addr32;

	split[0] = htobe32(a);
	split[1] = htobe32(b);
	split[2] = htobe32(c);
	split[3] = htobe32(d);
	return &ip;
}

static struct wg_peer *init_peer(void)
{
	struct wg_peer *peer = malloc(sizeof(*peer), M_WG, M_NOWAIT | M_ZERO);

	if (!peer)
		return NULL;
	LIST_INIT(&peer->p_aips);
	peer->p_aips_num = 0;
	peer->p_remote = (struct noise_remote *)peer; // Kind of dangerous, but probably fine.
	return peer;
}

#define insert(version, mem, ipa, ipb, ipc, ipd, cidr) do {                         \
		int _r = wg_aip_add(&sc, mem, (version) == 6 ? AF_INET6 : AF_INET,  \
				     ip##version(ipa, ipb, ipc, ipd), cidr);        \
		if (_r) {                                                           \
			printf("allowedips self-test insertion: FAIL (%d)\n", _r);  \
			success = false;                                            \
		}                                                                   \
	} while (0)

#define maybe_fail() do {                                              \
		++i;                                                   \
		if (!_s) {                                             \
			printf("allowedips self-test %zu: FAIL\n", i); \
			success = false;                               \
		}                                                      \
	} while (0)

#define test(version, mem, ipa, ipb, ipc, ipd) do {                                \
		bool _s = wg_aip_lookup(&sc, (version) == 6 ? AF_INET6 : AF_INET,  \
					ip##version(ipa, ipb, ipc, ipd)) == (mem); \
		maybe_fail();                                                      \
	} while (0)

#define test_negative(version, mem, ipa, ipb, ipc, ipd) do {                 \
		bool _s = wg_aip_lookup(&sc, (version) == 6 ? AF_INET6 : AF_INET,  \
					ip##version(ipa, ipb, ipc, ipd)) != (mem); \
		maybe_fail();                                                \
	} while (0)

#define test_boolean(cond) do {   \
		bool _s = (cond); \
		maybe_fail();     \
	} while (0)

#define free_all() do { \
		if (a) wg_aip_remove_all(&sc, a); \
		if (b) wg_aip_remove_all(&sc, b); \
		if (c) wg_aip_remove_all(&sc, c); \
		if (d) wg_aip_remove_all(&sc, d); \
		if (e) wg_aip_remove_all(&sc, e); \
		if (f) wg_aip_remove_all(&sc, f); \
		if (g) wg_aip_remove_all(&sc, g); \
		if (h) wg_aip_remove_all(&sc, h); \
	} while (0)

static bool wg_allowedips_selftest(void)
{
	bool found_a = false, found_b = false, found_c = false, found_d = false,
	     found_e = false, found_other = false;
	struct wg_peer *a = init_peer(), *b = init_peer(), *c = init_peer(),
		       *d = init_peer(), *e = init_peer(), *f = init_peer(),
		       *g = init_peer(), *h = init_peer();
	struct wg_softc sc = {{ 0 }};
	struct wg_aip *iter_node;
	size_t i = 0, count = 0;
	bool success = false;
	struct in6_addr ip;
	uint64_t part;

	if (!test_aip_init(&sc)) {
		printf("allowedips self-test malloc: FAIL\n");
		goto free;
	}

	if (!a || !b || !c || !d || !e || !f || !g || !h) {
		printf("allowedips self-test malloc: FAIL\n");
		goto free;
	}

	insert(4, a, 192, 168, 4, 0, 24);
	insert(4, b, 192, 168, 4, 4, 32);
	insert(4, c, 192, 168, 0, 0, 16);
	insert(4, d, 192, 95, 5, 64, 27);
	/* replaces previous entry, and maskself is required */
	insert(4, c, 192, 95, 5, 65, 27);
	insert(6, d, 0x26075300, 0x60006b00, 0, 0xc05f0543, 128);
	insert(6, c, 0x26075300, 0x60006b00, 0, 0, 64);
	insert(4, e, 0, 0, 0, 0, 0);
	insert(6, e, 0, 0, 0, 0, 0);
	/* replaces previous entry */
	insert(6, f, 0, 0, 0, 0, 0);
	insert(6, g, 0x24046800, 0, 0, 0, 32);
	/* maskself is required */
	insert(6, h, 0x24046800, 0x40040800, 0xdeadbeef, 0xdeadbeef, 64);
	insert(6, a, 0x24046800, 0x40040800, 0xdeadbeef, 0xdeadbeef, 128);
	insert(6, c, 0x24446800, 0x40e40800, 0xdeaebeef, 0xdefbeef, 128);
	insert(6, b, 0x24446800, 0xf0e40800, 0xeeaebeef, 0, 98);
	insert(4, g, 64, 15, 112, 0, 20);
	/* maskself is required */
	insert(4, h, 64, 15, 123, 211, 25);
	insert(4, a, 10, 0, 0, 0, 25);
	insert(4, b, 10, 0, 0, 128, 25);
	insert(4, a, 10, 1, 0, 0, 30);
	insert(4, b, 10, 1, 0, 4, 30);
	insert(4, c, 10, 1, 0, 8, 29);
	insert(4, d, 10, 1, 0, 16, 29);

	success = true;

	test(4, a, 192, 168, 4, 20);
	test(4, a, 192, 168, 4, 0);
	test(4, b, 192, 168, 4, 4);
	test(4, c, 192, 168, 200, 182);
	test(4, c, 192, 95, 5, 68);
	test(4, e, 192, 95, 5, 96);
	test(6, d, 0x26075300, 0x60006b00, 0, 0xc05f0543);
	test(6, c, 0x26075300, 0x60006b00, 0, 0xc02e01ee);
	test(6, f, 0x26075300, 0x60006b01, 0, 0);
	test(6, g, 0x24046800, 0x40040806, 0, 0x1006);
	test(6, g, 0x24046800, 0x40040806, 0x1234, 0x5678);
	test(6, f, 0x240467ff, 0x40040806, 0x1234, 0x5678);
	test(6, f, 0x24046801, 0x40040806, 0x1234, 0x5678);
	test(6, h, 0x24046800, 0x40040800, 0x1234, 0x5678);
	test(6, h, 0x24046800, 0x40040800, 0, 0);
	test(6, h, 0x24046800, 0x40040800, 0x10101010, 0x10101010);
	test(6, a, 0x24046800, 0x40040800, 0xdeadbeef, 0xdeadbeef);
	test(4, g, 64, 15, 116, 26);
	test(4, g, 64, 15, 127, 3);
	test(4, g, 64, 15, 123, 1);
	test(4, h, 64, 15, 123, 128);
	test(4, h, 64, 15, 123, 129);
	test(4, a, 10, 0, 0, 52);
	test(4, b, 10, 0, 0, 220);
	test(4, a, 10, 1, 0, 2);
	test(4, b, 10, 1, 0, 6);
	test(4, c, 10, 1, 0, 10);
	test(4, d, 10, 1, 0, 20);

	insert(4, a, 1, 0, 0, 0, 32);
	insert(4, a, 64, 0, 0, 0, 32);
	insert(4, a, 128, 0, 0, 0, 32);
	insert(4, a, 192, 0, 0, 0, 32);
	insert(4, a, 255, 0, 0, 0, 32);
	wg_aip_remove_all(&sc, a);
	test_negative(4, a, 1, 0, 0, 0);
	test_negative(4, a, 64, 0, 0, 0);
	test_negative(4, a, 128, 0, 0, 0);
	test_negative(4, a, 192, 0, 0, 0);
	test_negative(4, a, 255, 0, 0, 0);

	free_all();
	insert(4, a, 192, 168, 0, 0, 16);
	insert(4, a, 192, 168, 0, 0, 24);
	wg_aip_remove_all(&sc, a);
	test_negative(4, a, 192, 168, 0, 1);

	for (i = 0; i < 128; ++i) {
		part = htobe64(~(1LLU << (i % 64)));
		memset(&ip, 0xff, 16);
		memcpy((uint8_t *)&ip + (i < 64) * 8, &part, 8);
		wg_aip_add(&sc, a, AF_INET6, &ip, 128);
	}

	free_all();
	insert(4, a, 192, 95, 5, 93, 27);
	insert(6, a, 0x26075300, 0x60006b00, 0, 0xc05f0543, 128);
	insert(4, a, 10, 1, 0, 20, 29);
	insert(6, a, 0x26075300, 0x6d8a6bf8, 0xdab1f1df, 0xc05f1523, 83);
	insert(6, a, 0x26075300, 0x6d8a6bf8, 0xdab1f1df, 0xc05f1523, 21);
	LIST_FOREACH(iter_node, &a->p_aips, a_entry) {
		uint8_t cidr, *ip = iter_node->a_addr.bytes;
		sa_family_t family = iter_node->a_af;
		if (family == AF_INET)
			cidr = bitcount32(iter_node->a_mask.ip);
		else if (family == AF_INET6)
			cidr = in6_mask2len(&iter_node->a_mask.in6, NULL);
		else
			continue;

		count++;

		if (cidr == 27 && family == AF_INET &&
		    !memcmp(ip, ip4(192, 95, 5, 64), sizeof(struct in_addr)))
			found_a = true;
		else if (cidr == 128 && family == AF_INET6 &&
			 !memcmp(ip, ip6(0x26075300, 0x60006b00, 0, 0xc05f0543),
				 sizeof(struct in6_addr)))
			found_b = true;
		else if (cidr == 29 && family == AF_INET &&
			 !memcmp(ip, ip4(10, 1, 0, 16), sizeof(struct in_addr)))
			found_c = true;
		else if (cidr == 83 && family == AF_INET6 &&
			 !memcmp(ip, ip6(0x26075300, 0x6d8a6bf8, 0xdab1e000, 0),
				 sizeof(struct in6_addr)))
			found_d = true;
		else if (cidr == 21 && family == AF_INET6 &&
			 !memcmp(ip, ip6(0x26075000, 0, 0, 0),
				 sizeof(struct in6_addr)))
			found_e = true;
		else
			found_other = true;
	}
	test_boolean(count == 5);
	test_boolean(found_a);
	test_boolean(found_b);
	test_boolean(found_c);
	test_boolean(found_d);
	test_boolean(found_e);
	test_boolean(!found_other);

#ifdef WG_ALLOWEDIPS_RANDOMIZED_TEST
	if (success)
		success = randomized_test();
#endif

	if (success)
		printf("allowedips self-tests: pass\n");

free:
	free_all();
	free(a, M_WG);
	free(b, M_WG);
	free(c, M_WG);
	free(d, M_WG);
	free(e, M_WG);
	free(f, M_WG);
	free(g, M_WG);
	free(h, M_WG);
	test_aip_deinit(&sc);

	return success;
}

#undef test_negative
#undef test
#undef remove
#undef insert
#undef init_peer
#undef free_all
