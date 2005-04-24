/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/usr.sbin/dntpd/client.h,v 1.1 2005/04/24 02:36:50 dillon Exp $
 */

struct server_info {
	int fd;
	char *target;

	/*
	 * A second linear regression playing hopskip with the first.
	 */
	struct server_info *altinfo;

	/*
	 * Linear regression accumulator
	 *
	 * note: the starting base time is where all corrections get applied
	 * to eventually.  The linear regression makes a relative microseconds
	 * calculation between the current base time and the starting base
	 * time to figure out what corrections the system has made to the
	 * clock.
	 */
	struct timeval lin_tv;	/* starting real time */
	struct timeval lin_btv;	/* starting base time */
	double lin_count;	/* samples	*/
	double lin_sumx;	/* sum(x)	*/
	double lin_sumy;	/* sum(y)	*/
	double lin_sumxy;	/* sum(x*y)	*/
	double lin_sumx2;	/* sum(x^2) 	*/
	double lin_sumy2;	/* sum(y^2) 	*/
	double lin_sumoffset;	/* sum of uncompensated offsets */

	/*
	 * Cached results
	 */
	double lin_cache_slope;
	double lin_cache_yint;
	double lin_cache_corr;

	double lin_cache_offset; /* last offset correction (s) */
	double lin_cache_freq;	/* last frequency correction (s/s) */
};

/*
 * We start a second linear regression a LIN_RESTART / 2 and it
 * replaces the first one (and we start a third) at LIN_RESTART.
 */
#define LIN_RESTART	30

typedef struct server_info *server_info_t;

void client_init(void);
int client_main(struct server_info **info_ary, int count);
void client_poll(server_info_t info);
struct server_info *client_check(struct server_info **check, 
				struct server_info *best);
void lin_regress(server_info_t info, 
		 struct timeval *ltv, struct timeval *lbtv, 
		 double offset, int isalt);
void lin_reset(server_info_t info);


