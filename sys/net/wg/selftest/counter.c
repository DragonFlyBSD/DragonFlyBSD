/* SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2015-2021 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2021 Matt Dunwoodie <ncon@noconroy.net>
 */

#define T_LIM (COUNTER_WINDOW_SIZE + 1)
#define T_INIT do {				\
	bzero(&kp, sizeof(kp));			\
	rw_init(&kp.kp_nonce_lock, "counter");	\
} while (0)
#define T(num, v, e) do {						\
	if (noise_keypair_nonce_check(&kp, v) != (e)) {			\
		printf("nonce counter self-test %u: FAIL\n", num);	\
		success = false;					\
	}								\
} while (0)

bool
noise_counter_selftest(void)
{
	struct noise_keypair kp;
	unsigned int i;
	bool success = true;

	T_INIT;
	/* T(test number, nonce, expected_response) */
	T( 1, 0, 0);
	T( 2, 1, 0);
	T( 3, 1, EEXIST);
	T( 4, 9, 0);
	T( 5, 8, 0);
	T( 6, 7, 0);
	T( 7, 7, EEXIST);
	T( 8, T_LIM, 0);
	T( 9, T_LIM - 1, 0);
	T(10, T_LIM - 1, EEXIST);
	T(11, T_LIM - 2, 0);
	T(12, 2, 0);
	T(13, 2, EEXIST);
	T(14, T_LIM + 16, 0);
	T(15, 3, EEXIST);
	T(16, T_LIM + 16, EEXIST);
	T(17, T_LIM * 4, 0);
	T(18, T_LIM * 4 - (T_LIM - 1), 0);
	T(19, 10, EEXIST);
	T(20, T_LIM * 4 - T_LIM, EEXIST);
	T(21, T_LIM * 4 - (T_LIM + 1), EEXIST);
	T(22, T_LIM * 4 - (T_LIM - 2), 0);
	T(23, T_LIM * 4 + 1 - T_LIM, EEXIST);
	T(24, 0, EEXIST);
	T(25, REJECT_AFTER_MESSAGES, EEXIST);
	T(26, REJECT_AFTER_MESSAGES - 1, 0);
	T(27, REJECT_AFTER_MESSAGES, EEXIST);
	T(28, REJECT_AFTER_MESSAGES - 1, EEXIST);
	T(29, REJECT_AFTER_MESSAGES - 2, 0);
	T(30, REJECT_AFTER_MESSAGES + 1, EEXIST);
	T(31, REJECT_AFTER_MESSAGES + 2, EEXIST);
	T(32, REJECT_AFTER_MESSAGES - 2, EEXIST);
	T(33, REJECT_AFTER_MESSAGES - 3, 0);
	T(34, 0, EEXIST);

	T_INIT;
	for (i = 1; i <= COUNTER_WINDOW_SIZE; ++i)
		T(35, i, 0);
	T(36, 0, 0);
	T(37, 0, EEXIST);

	T_INIT;
	for (i = 2; i <= COUNTER_WINDOW_SIZE + 1; ++i)
		T(38, i, 0);
	T(39, 1, 0);
	T(40, 0, EEXIST);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i-- > 0;)
		T(41, i, 0);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 2; i-- > 1;)
		T(42, i, 0);
	T(43, 0, EEXIST);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i-- > 1;)
		T(44, i, 0);
	T(45, COUNTER_WINDOW_SIZE + 1, 0);
	T(46, 0, EEXIST);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i-- > 1;)
		T(47, i, 0);
	T(48, 0, 0);
	T(49, COUNTER_WINDOW_SIZE + 1, 0);

	if (success)
		printf("nonce counter self-test: pass\n");
	return success;
}
