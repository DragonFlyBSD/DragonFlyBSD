/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2019-2020 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Aaron LI <aly@aaronly.me>
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
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
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

#ifndef CHINESE_H_
#define CHINESE_H_

#include <stdbool.h>

struct chinese_date {
	int	cycle;
	int	year;	/* year in the $cycle: [1, 60] */
	int	month;	/* [1, 12] */
	bool	leap;	/* whether $month is a leap month */
	int	day;
};

struct chinese_jieqi {
	const char	*name;
	const char	*zhname;
	bool		is_major;  /* whether a major solar term (Zhōngqì) */
	int		longitude;  /* longitude of Sun */
};

enum { C_JIEQI_ALL, C_JIEQI_MAJOR, C_JIEQI_MINOR };

struct cal_day;

int	chinese_new_year(int year);

void	chinese_from_fixed(int rd, struct chinese_date *date);
int	fixed_from_chinese(const struct chinese_date *date);

int	chinese_qingming(int g_year);
int	chinese_jieqi_onafter(int rd, int type, const struct chinese_jieqi **jieqi);

int	chinese_format_date(char *buf, size_t size, int rd);
int	chinese_find_days_ymd(int year, int month, int day, struct cal_day **dayp,
			      char **edp);
int	chinese_find_days_dom(int dom, struct cal_day **dayp, char **edp);
void	show_chinese_calendar(int rd);

#endif
