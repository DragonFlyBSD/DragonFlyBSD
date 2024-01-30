/*-
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2015-2021 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2021 Matt Dunwoodie <ncon@noconroy.net>
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

#define T_LIM (COUNTER_WINDOW_SIZE + 1)
#define T_INIT do {							\
	bzero(&kp, sizeof(kp));						\
	lockinit(&kp.kp_counter_lock, "noise_counter", 0, 0);		\
} while (0)
#define T(num, v, e) do {						\
	if (noise_keypair_counter_check(&kp, v) != (e)) {		\
		kprintf("%s: self-test %u: FAIL\n", __func__, num);	\
		success = false;					\
	}								\
} while (0)

bool
noise_counter_selftest(void)
{
	struct noise_keypair kp;
	int i;
	bool success = true;

	T_INIT;
	/* T(test_number, counter, expected_response) */
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
	T(15, 3, ESTALE);
	T(16, T_LIM + 16, EEXIST);
	T(17, T_LIM * 4, 0);
	T(18, T_LIM * 4 - (T_LIM - 1), 0);
	T(19, 10, ESTALE);
	T(20, T_LIM * 4 - T_LIM, ESTALE);
	T(21, T_LIM * 4 - (T_LIM + 1), ESTALE);
	T(22, T_LIM * 4 - (T_LIM - 2), 0);
	T(23, T_LIM * 4 - (T_LIM - 1), EEXIST);
	T(24, 0, ESTALE);
	T(25, REJECT_AFTER_MESSAGES, EINVAL);
	T(26, REJECT_AFTER_MESSAGES - 1, 0);
	T(27, REJECT_AFTER_MESSAGES, EINVAL);
	T(28, REJECT_AFTER_MESSAGES - 1, EEXIST);
	T(29, REJECT_AFTER_MESSAGES - 2, 0);
	T(30, REJECT_AFTER_MESSAGES + 1, EINVAL);
	T(31, REJECT_AFTER_MESSAGES + 2, EINVAL);
	T(32, REJECT_AFTER_MESSAGES - 2, EEXIST);
	T(33, REJECT_AFTER_MESSAGES - 3, 0);
	T(34, 0, ESTALE);

	T_INIT;
	for (i = 1; i <= COUNTER_WINDOW_SIZE; ++i)
		T(35, i, 0);
	T(36, 0, 0);
	T(37, 0, EEXIST);

	T_INIT;
	for (i = 2; i <= COUNTER_WINDOW_SIZE + 1; ++i)
		T(38, i, 0);
	T(39, 1, 0);
	T(40, 0, ESTALE);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE; i >= 0; --i)
		T(41, i, 0);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i >= 1; --i)
		T(42, i, 0);
	T(43, 0, ESTALE);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE; i >= 1; --i)
		T(44, i, 0);
	T(45, COUNTER_WINDOW_SIZE + 1, 0);
	T(46, 0, ESTALE);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE; i >= 1; --i)
		T(47, i, 0);
	T(48, 0, 0);
	T(49, COUNTER_WINDOW_SIZE + 1, 0);

	kprintf("%s: %s\n", __func__, success ? "pass" : "FAIL");
	return (success);
}

#undef T
#undef T_INIT
#undef T_LIM
