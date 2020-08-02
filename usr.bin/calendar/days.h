/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
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

#ifndef DAYS_H_
#define DAYS_H_

/* IDs of special days */
enum {
	SD_NONE,
	SD_EASTER,
	SD_PASKHA,
	SD_ADVENT,
	SD_CNY,
	SD_CQINGMING,
	SD_CJIEQI,
	SD_MAREQUINOX,
	SD_SEPEQUINOX,
	SD_JUNSOLSTICE,
	SD_DECSOLSTICE,
	SD_NEWMOON,
	SD_FULLMOON,
};

struct cal_day;

struct specialday {
	int		 id;		/* enum ID of the special day */
	const char	*name;		/* name of the special day */
	size_t		 len;		/* length of the name */
	char		*n_name;	/* national name of the special day */
	size_t		 n_len;		/* length of the national name */

	/* function to find days of the special day in [rd1, rd2] */
	int	(*find_days)(int offset, struct cal_day **dayp, char **edp);
};

extern struct specialday specialdays[];

int	find_days_ymd(int year, int month, int day,
		      struct cal_day **dayp, char **edp);
int	find_days_dom(int dom, struct cal_day **dayp, char **edp);
int	find_days_month(int month, struct cal_day **dayp, char **edp);
int	find_days_mdow(int month, int dow, int index,
		       struct cal_day **dayp, char **edp);

#endif
