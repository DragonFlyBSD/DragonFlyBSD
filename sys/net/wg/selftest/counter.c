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
	T( 1, 0, true);
	T( 2, 1, true);
	T( 3, 1, false);
	T( 4, 9, true);
	T( 5, 8, true);
	T( 6, 7, true);
	T( 7, 7, false);
	T( 8, T_LIM, true);
	T( 9, T_LIM - 1, true);
	T(10, T_LIM - 1, false);
	T(11, T_LIM - 2, true);
	T(12, 2, true);
	T(13, 2, false);
	T(14, T_LIM + 16, true);
	T(15, 3, false);
	T(16, T_LIM + 16, false);
	T(17, T_LIM * 4, true);
	T(18, T_LIM * 4 - (T_LIM - 1), true);
	T(19, 10, false);
	T(20, T_LIM * 4 - T_LIM, false);
	T(21, T_LIM * 4 - (T_LIM + 1), false);
	T(22, T_LIM * 4 - (T_LIM - 2), true);
	T(23, T_LIM * 4 + 1 - T_LIM, false);
	T(24, 0, false);
	T(25, REJECT_AFTER_MESSAGES, false);
	T(26, REJECT_AFTER_MESSAGES - 1, true);
	T(27, REJECT_AFTER_MESSAGES, false);
	T(28, REJECT_AFTER_MESSAGES - 1, false);
	T(29, REJECT_AFTER_MESSAGES - 2, true);
	T(30, REJECT_AFTER_MESSAGES + 1, false);
	T(31, REJECT_AFTER_MESSAGES + 2, false);
	T(32, REJECT_AFTER_MESSAGES - 2, false);
	T(33, REJECT_AFTER_MESSAGES - 3, true);
	T(34, 0, false);

	T_INIT;
	for (i = 1; i <= COUNTER_WINDOW_SIZE; ++i)
		T(35, i, true);
	T(36, 0, true);
	T(37, 0, false);

	T_INIT;
	for (i = 2; i <= COUNTER_WINDOW_SIZE + 1; ++i)
		T(38, i, true);
	T(39, 1, true);
	T(40, 0, false);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE; i >= 0; --i)
		T(41, i, true);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i >= 1; --i)
		T(42, i, true);
	T(43, 0, false);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE; i >= 1; --i)
		T(44, i, true);
	T(45, COUNTER_WINDOW_SIZE + 1, true);
	T(46, 0, false);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE; i >= 1; --i)
		T(47, i, true);
	T(48, 0, true);
	T(49, COUNTER_WINDOW_SIZE + 1, true);

	kprintf("%s: %s\n", __func__, success ? "pass" : "FAIL");
	return (success);
}

#undef T
#undef T_INIT
#undef T_LIM
