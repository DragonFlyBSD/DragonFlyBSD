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
 */

struct server_info {
	int fd;			/* udp descriptor */
	int server_state;	/* -1 (no dns), 0 (dns good), or 1 (pkt good) */
	int server_insane;	/* insanity check */
	int poll_sleep;		/* countdown for poll (in seconds) */
	int poll_mode;		/* mode of operation */
	int poll_count;		/* number of polls in current mode */
	int poll_failed;	/* count of NTP failures */
	struct sockaddr *sam; 	/* udp connection info */
	struct sockaddr_storage sam_st; 	/* udp connection info (stor) */
	char *target;		/* target hostname or IP (string) */
	char *ipstr;		/* IP string */

	/*
	 * A second linear regression playing hopskip with the first.  This
	 * is maintained by the check function.
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

	/*
	 * Offsets are accumulated for a straight average.  When a
	 * correction is made we have to reset the averaging code
	 * or follow-up corrections will oscillate wildly because
	 * the new offsets simply cannot compete with the dozens
	 * of previously polls in the sum.
	 */
	double lin_sumoffset;	/* sum of compensated offsets */
	double lin_sumoffset2;	/* sum of compensated offsets^2 */
	double lin_countoffset;	/* count is reset after a correction is made */

	/*
	 * Cached results
	 */
	double lin_cache_slope;	/* (freq calculations) */
	double lin_cache_yint;	/* (freq calculations) */
	double lin_cache_corr;	/* (freq calculations) */
	double lin_cache_stddev; /* (offset calculations) */

	double lin_cache_offset; /* last sampled offset (NOT an average) */
	double lin_cache_freq;	/* last frequency correction (s/s) */
};

/*
 * Polling modes and max polls for specific modes.  Note that the polling
 * mode basically just effects the polling rate.  It does not effect the
 * linear regression.
 */
#define POLL_FIXED	0	/* fixed internal (nom_sleep_opt seconds) */
#define POLL_STARTUP	1	/* startup poll for offset adjust (min) */
#define POLL_ACQUIRE	2	/* acquisition for frequency adjust (nom) */
#define POLL_MAINTAIN	3	/* maintainance mode (max) */
#define POLL_FAILED	4	/* failure state (nom) */

#define POLL_STARTUP_MAX	6	/* max polls in this mode */
#define POLL_ACQUIRE_MAX	16	/* max polls in this mode */
#define POLL_FAIL_RESET		3	/* reset the regression after 3 fails */
#define POLL_RECOVERY_RESTART	10	/* ->ACQ vs ->STARTUP after recovery */

/*
 * We start a second linear regression a LIN_RESTART / 2 and it
 * replaces the first one (and we start a third) at LIN_RESTART.
 */
#define LIN_RESTART	30

/*
 * A course correction is made if the time gets more then 2 minutes
 * off.
 */
#define COURSE_OFFSET_CORRECTION_LIMIT	120.0

typedef struct server_info *server_info_t;

void client_init(void);
void client_main(struct server_info **info_ary, int count) __dead2;
void client_poll(server_info_t info, int poll_interval, 
		  int calc_offset_correction);
void client_check(struct server_info **check, 
		  struct server_info **best_off,
		  struct server_info **best_freq);
void client_check_duplicate_ips(struct server_info **info_ary, int count);
void client_manage_polling_mode(struct server_info *info, int *didreconnect);
void client_setserverstate(server_info_t info, int state, const char *str);

void lin_regress(server_info_t info, 
		 struct timeval *ltv, struct timeval *lbtv,
		 double offset, int calc_offset_correction);
void lin_reset(server_info_t info);
void lin_resetalloffsets(struct server_info **info_ary, int count);
void lin_resetoffsets(server_info_t info);

