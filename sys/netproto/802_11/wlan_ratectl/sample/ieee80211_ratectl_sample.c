/*
 * Copyright (c) 2005 John Bicket
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD: src/sys/dev/ath/ath_rate/sample/sample.c,v 1.8.2.3 2006/03/14 23:22:27 sam Exp $
 * $DragonFly: src/sys/netproto/802_11/wlan_ratectl/sample/ieee80211_ratectl_sample.c,v 1.1 2007/04/01 13:59:41 sephe Exp $
 */

/*
 * John Bicket's SampleRate control algorithm.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>
 
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/wlan_ratectl/sample/ieee80211_ratectl_sample.h>

#define	SAMPLE_DEBUG

#ifdef SAMPLE_DEBUG
#define	DPRINTF(ssc, lv, fmt, ...) do {		\
	if (ssc->debug >= lv)			\
		kprintf(fmt, __VA_ARGS__);	\
} while (0)
#else
#define	DPRINTF(ssc, lv, fmt, ...)
#endif

/*
 * This file is an implementation of the SampleRate algorithm
 * in "Bit-rate Selection in Wireless Networks"
 * (http://www.pdos.lcs.mit.edu/papers/jbicket-ms.ps)
 *
 * SampleRate chooses the bit-rate it predicts will provide the most
 * throughput based on estimates of the expected per-packet
 * transmission time for each bit-rate.  SampleRate periodically sends
 * packets at bit-rates other than the current one to estimate when
 * another bit-rate will provide better performance. SampleRate
 * switches to another bit-rate when its estimated per-packet
 * transmission time becomes smaller than the current bit-rate's.
 * SampleRate reduces the number of bit-rates it must sample by
 * eliminating those that could not perform better than the one
 * currently being used.  SampleRate also stops probing at a bit-rate
 * if it experiences several successive losses.
 *
 * The difference between the algorithm in the thesis and the one in this
 * file is that the one in this file uses a ewma instead of a window.
 *
 * Also, this implementation tracks the average transmission time for
 * a few different packet sizes independently for each link.
 */

#define STALE_FAILURE_TIMEOUT_MS	10000
#define MIN_SWITCH_MS			1000

static void	*sample_attach(struct ieee80211com *);
static void	sample_detach(void *);
static void	sample_data_alloc(struct ieee80211_node *);
static void	sample_data_free(struct ieee80211_node *);
static void	sample_data_dup(const struct ieee80211_node *,
				struct ieee80211_node *);
static void	sample_newstate(void *, enum ieee80211_state);
static void	sample_tx_complete(void *, struct ieee80211_node *, int,
				   const struct ieee80211_ratectl_res[],
				   int, int, int, int);
static void	sample_newassoc(void *, struct ieee80211_node *, int);
static int	sample_findrate(void *, struct ieee80211_node *, int,
				int[], int);

static void	sample_sysctl_attach(struct sample_softc *);
static void	sample_start(struct sample_softc *, struct ieee80211_node *);
static void	sample_update_stats(struct sample_softc *,
				    struct ieee80211_node *, int, int,
				    const struct ieee80211_ratectl_res [], int,
				    int, int);

static const struct ieee80211_ratectl sample = {
	.rc_name	= "sample",
	.rc_ratectl	= IEEE80211_RATECTL_SAMPLE,
	.rc_attach	= sample_attach,
	.rc_detach	= sample_detach,
	.rc_data_alloc	= sample_data_alloc,
	.rc_data_free	= sample_data_free,
	.rc_data_dup	= sample_data_dup,
	.rc_newstate	= sample_newstate,
	.rc_tx_complete	= sample_tx_complete,
	.rc_newassoc	= sample_newassoc,
	.rc_findrate	= sample_findrate
};

static u_int	sample_nrefs;

/*
 * for now, we track performance for three different packet
 * size buckets
 */
static int	packet_size_bins[NUM_PACKET_SIZE_BINS] = { 250, 1600, 3000 };

MALLOC_DEFINE(M_SAMPLE_RATECTL_DATA, "sample_ratectl_data",
	      "sample rate control data");

static __inline int
size_to_bin(int size) 
{
	int x;

	for (x = 0; x < NUM_PACKET_SIZE_BINS; x++) {
		if (size <= packet_size_bins[x])
			return x;
	}
	return NUM_PACKET_SIZE_BINS - 1;
}

static __inline int
bin_to_size(int index)
{
	return packet_size_bins[index];
}

static __inline int
rate_to_ndx(struct ieee80211_node *ni, int rate)
{
	int x = 0;

	for (x = 0; x < ni->ni_rates.rs_nrates; x++) {
		if (IEEE80211_RS_RATE(&ni->ni_rates, x) == rate)
			return x;
	}
	return -1;
}

static __inline int
unicast_pkt_time(struct sample_softc *ssc, struct ieee80211_node *ni,
		 int rate, int len, int data_tries, int rts_tries,
		 int *cw0)
{
	struct ieee80211com *ic = ssc->ic;
	int sifs, difs, slot;
	int ack_dur, data_dur, cw;
	int tt = 0;
	int i;

	ack_dur = ieee80211_txtime(ni,
			sizeof(struct ieee80211_frame_ack) + IEEE80211_FCS_LEN,
			ieee80211_ack_rate(ni, rate), ic->ic_flags);
	data_dur = ieee80211_txtime(ni, len, rate, ic->ic_flags);

	if (IEEE80211_IS_CHAN_5GHZ(ni->ni_chan)) {
		cw = IEEE80211_CW_MIN_1;
		sifs = IEEE80211_DUR_OFDM_SIFS;
		slot = IEEE80211_DUR_OFDM_SLOT;
	} else {
		/* XXX should base on characteristic rate set */
		cw = IEEE80211_CW_MIN_0;
		sifs = IEEE80211_DUR_SIFS;
		slot = (ic->ic_flags & IEEE80211_F_SHSLOT)
			? IEEE80211_DUR_SHSLOT
			: IEEE80211_DUR_SLOT;
	}
	if (cw0 != NULL && *cw0 != 0)
		cw = *cw0;
	difs = IEEE80211_DUR_DIFS(sifs, slot);

	if (rts_tries > 0 && (ic->ic_flags & IEEE80211_F_USEPROT) &&
	    ieee80211_rate2modtype(rate) == IEEE80211_MODTYPE_OFDM) {
		if (ic->ic_protmode == IEEE80211_PROT_RTSCTS) {
			uint8_t rts_rate;
			int rts_dur, cts_dur;

			/* Assume RTS is sent at 2Mbits/s */
			rts_rate = 4;

			rts_dur = ieee80211_txtime(ni,
					sizeof(struct ieee80211_frame_rts) +
					IEEE80211_FCS_LEN,
					rts_rate, ic->ic_flags);
			cts_dur = ieee80211_txtime(ni,
					sizeof(struct ieee80211_frame_cts) +
					IEEE80211_FCS_LEN,
					ieee80211_ack_rate(ni, rts_rate),
					ic->ic_flags);

			tt += rts_tries * (rts_dur + sifs + cts_dur);

			/*
			 * Immediate data transmission does not perform backoff
			 * procedure.
			 *
			 * XXX not correct, if RTS retries (short retry count)
			 * reaches dot11ShortRetryLimit, which should be rare.
			 */
			tt += sifs;
			--rts_tries;
		} else if (ic->ic_protmode == IEEE80211_PROT_CTSONLY) {
			/* Assume CTS is sent at 2Mbits/s */
			tt += ieee80211_txtime(ni,
				sizeof(struct ieee80211_frame_cts) +
				IEEE80211_FCS_LEN,
				4, ic->ic_flags);
			tt += sifs;
			rts_tries = 0;
		}
	} else {
		rts_tries = 0;
	}

	tt += data_tries * (data_dur + sifs + ack_dur);

	/* Average time consumed by backoff procedure */
	for (i = 0; i < (data_tries + rts_tries); ++i) {
		tt += difs + (slot * cw / 2);
		cw = MIN(IEEE80211_CW_MAX + 1, (cw + 1) * 2) - 1;
	}
	if (cw0 != NULL)
		*cw0 = cw;
	return tt;
}

/*
 * returns the ndx with the lowest average_tx_time,
 * or -1 if all the average_tx_times are 0.
 */
static __inline int
best_rate_ndx(struct ieee80211_node *ni, int size_bin,
	      int require_acked_before)
{
	int x, best_rate_ndx = 0, best_rate_tt = 0;
	struct sample_data *sd = ni->ni_rate_data;

        for (x = 0; x < ni->ni_rates.rs_nrates; x++) {
		int tt = sd->stats[size_bin][x].average_tx_time;

		if (tt <= 0 ||
		    (require_acked_before &&
		     !sd->stats[size_bin][x].packets_acked))
			continue;

		/* 9 megabits never works better than 12 */
		if (IEEE80211_RS_RATE(&ni->ni_rates, x) == 18)
			continue;

		/* don't use a bit-rate that has been failing */
		if (sd->stats[size_bin][x].successive_failures > 3)
			continue;

		if (!best_rate_tt || best_rate_tt > tt) {
			best_rate_tt = tt;
			best_rate_ndx = x;
		}
        }
        return (best_rate_tt) ? best_rate_ndx : -1;
}

/*
 * pick a good "random" bit-rate to sample other than the current one
 */
static __inline int
pick_sample_ndx(struct ieee80211_node *ni, int size_bin) 
{
	int x = 0;
	int current_ndx = 0;
	unsigned current_tt = 0;
	struct sample_data *sd = ni->ni_rate_data;

	current_ndx = sd->current_rate[size_bin];
	if (current_ndx < 0) {
		/* no successes yet, send at the lowest bit-rate */
		return 0;
	}

	current_tt = sd->stats[size_bin][current_ndx].average_tx_time;

	for (x = 0; x < ni->ni_rates.rs_nrates; x++) {
		int ndx = (sd->last_sample_ndx[size_bin] + 1 + x) %
			  ni->ni_rates.rs_nrates;

	        /* don't sample the current bit-rate */
		if (ndx == current_ndx) 
			continue;

		/* this bit-rate is always worse than the current one */
		if (sd->stats[size_bin][ndx].perfect_tx_time > current_tt) 
			continue;

		/* rarely sample bit-rates that fail a lot */
		if (ticks - sd->stats[size_bin][ndx].last_tx < ((hz * STALE_FAILURE_TIMEOUT_MS) / 1000) &&
		    sd->stats[size_bin][ndx].successive_failures > 3)
			continue;

		/*
		 * don't sample more than 2 indexes higher
		 * for rates higher than 11 megabits
		 */
		if (IEEE80211_RS_RATE(&ni->ni_rates, ndx) > 22 &&
		    ndx > current_ndx + 2)
			continue;

		/* 9 megabits never works better than 12 */
		if (IEEE80211_RS_RATE(&ni->ni_rates, ndx) == 18)
			continue;

		/*
		 * if we're using 11 megabits, only sample up to 12 megabits
		 */
		if (IEEE80211_RS_RATE(&ni->ni_rates, current_ndx) == 22 &&
		    ndx > current_ndx + 1) 
			continue;

		sd->last_sample_ndx[size_bin] = ndx;
		return ndx;
	}
	return current_ndx;
}

static int
sample_findrate(void *arg, struct ieee80211_node *ni, int frame_len,
		int rateidx[], int rateidx_len)
{
	struct sample_softc *ssc = arg;
	struct sample_data *sd = ni->ni_rate_data;
	struct ieee80211com *ic = ssc->ic;
	struct ieee80211_ratectl_state *rc_st = &ic->ic_ratectl;
	int ndx, size_bin, best_ndx, change_rates, ack_before, cur_ndx, i;
	unsigned average_tx_time;

	for (i = 0; i < NUM_PACKET_SIZE_BINS; ++i) {
		if (sd->current_rate[i] >= ni->ni_rates.rs_nrates) {
			DPRINTF(ssc, 5, "%s: number of rates changed, "
				"restart\n", __func__);
			sample_start(ssc, ni);
			break;
		}
	}

	KKASSERT(frame_len > 0);
	size_bin = size_to_bin(frame_len);

	ack_before = (!(rc_st->rc_st_flags & IEEE80211_RATECTL_F_MRR) ||
		      (ic->ic_flags & IEEE80211_F_USEPROT));
	best_ndx = best_rate_ndx(ni, size_bin, ack_before);
	if (best_ndx >= 0)
		average_tx_time = sd->stats[size_bin][best_ndx].average_tx_time;
	else
		average_tx_time = 0;

	if (sd->static_rate_ndx != -1) {
		ndx = sd->static_rate_ndx;
	} else {
		if (sd->sample_tt[size_bin] <
		    average_tx_time * (sd->packets_since_sample[size_bin] * ssc->sample_rate / 100)) {
			/*
			 * We want to limit the time measuring the
			 * performance of other bit-rates to sample_rate%
			 * of the total transmission time.
			 */
			ndx = pick_sample_ndx(ni, size_bin);
			if (ndx != sd->current_rate[size_bin])
				sd->current_sample_ndx[size_bin] = ndx;
			else
				sd->current_sample_ndx[size_bin] = -1;
			sd->packets_since_sample[size_bin] = 0;
		} else {
			change_rates = 0;
			if (!sd->packets_sent[size_bin] || best_ndx == -1) {
				/* no packet has been sent successfully yet */
				for (ndx = ni->ni_rates.rs_nrates - 1; ndx > 0; ndx--) {
					/* 
					 * pick the highest rate <= 36 Mbps
					 * that hasn't failed.
					 */
					if (IEEE80211_RS_RATE(&ni->ni_rates, ndx) <= 72 && 
					    sd->stats[size_bin][ndx].successive_failures == 0)
						break;
				}
				change_rates = 1;
				best_ndx = ndx;
			} else if (sd->packets_sent[size_bin] < 20) {
				/* let the bit-rate switch quickly during the first few packets */
				change_rates = 1;
			} else if (ticks - ((hz * MIN_SWITCH_MS) / 1000) > sd->ticks_since_switch[size_bin]) {
				/* 2 seconds have gone by */
				change_rates = 1;
			} else if (average_tx_time * 2 < sd->stats[size_bin][sd->current_rate[size_bin]].average_tx_time) {
				/* the current bit-rate is twice as slow as the best one */
				change_rates = 1;
			}

			sd->packets_since_sample[size_bin]++;

			if (change_rates) {
				if (best_ndx != sd->current_rate[size_bin]) {
					DPRINTF(ssc, 5, "%s: %6D size %d "
						"switch rate "
						"%d (%d/%d) -> %d (%d/%d) "
						"after %d packets\n",
						__func__,
						ni->ni_macaddr, ":",
						packet_size_bins[size_bin],
						IEEE80211_RS_RATE(&ni->ni_rates, sd->current_rate[size_bin]),
						sd->stats[size_bin][sd->current_rate[size_bin]].average_tx_time,
						sd->stats[size_bin][sd->current_rate[size_bin]].perfect_tx_time,
						IEEE80211_RS_RATE(&ni->ni_rates, best_ndx),
						sd->stats[size_bin][best_ndx].average_tx_time,
						sd->stats[size_bin][best_ndx].perfect_tx_time,
						sd->packets_since_switch[size_bin]);
				}
				sd->packets_since_switch[size_bin] = 0;
				sd->current_rate[size_bin] = best_ndx;
				sd->ticks_since_switch[size_bin] = ticks;
			}
			ndx = sd->current_rate[size_bin];
			sd->packets_since_switch[size_bin]++;
			if (size_bin == 0) {
	    			/*
	    			 * set the visible txrate for this node
			         * to the rate of small packets
			         */
				ni->ni_txrate = ndx;
			}
		}
	}

	KASSERT(ndx >= 0 && ndx < ni->ni_rates.rs_nrates, ("ndx is %d", ndx));

	sd->packets_sent[size_bin]++;

	cur_ndx = sd->current_rate[size_bin];
	if (sd->stats[size_bin][cur_ndx].packets_acked == 0)
		cur_ndx = 0;

	rateidx[0] = ndx;
	i = 1;
	if (rateidx_len > 2) {
		if ((rc_st->rc_st_flags & IEEE80211_RATECTL_F_RSDESC) == 0 ||
		    cur_ndx < ndx)
			rateidx[i++] = cur_ndx;
		else if (ndx > 0)
			rateidx[i++] = ndx - 1;
	}
	if (i < rateidx_len && rateidx[i] != 0)
		rateidx[i++] = 0;
	return i;
}

static void
sample_update_stats(struct sample_softc *ssc, struct ieee80211_node *ni,
		    int size, int size_bin,
		    const struct ieee80211_ratectl_res res[], int res_len,
		    int rts_tries, int is_fail)
{
	struct sample_data *sd = ni->ni_rate_data;
	int tt, rate, i, ndx, cw = 0, data_tries;

	cw = 0;
	ndx = res[0].rc_res_rateidx;

	rate = IEEE80211_RS_RATE(&ni->ni_rates, ndx);
	tt = unicast_pkt_time(ssc, ni, rate, size,
			      res[0].rc_res_tries, rts_tries, &cw);
	data_tries = res[0].rc_res_tries;

	for (i = 1; i < res_len; ++i) {
		rate = IEEE80211_RS_RATE(&ni->ni_rates, res[i].rc_res_rateidx);
		tt += unicast_pkt_time(ssc, ni, rate, size,
				       res[i].rc_res_tries, 0, &cw);
		data_tries += res[i].rc_res_tries;
	}

	if (sd->stats[size_bin][ndx].total_packets < (100 / (100 - ssc->smoothing_rate))) {
		int avg_tx = sd->stats[size_bin][ndx].average_tx_time;
		int packets = sd->stats[size_bin][ndx].total_packets;

		/* Average the first few packets. */
		sd->stats[size_bin][ndx].average_tx_time =
			(tt + (avg_tx * packets)) / (packets + 1);
	} else {
		/* Use EWMA */
		sd->stats[size_bin][ndx].average_tx_time =
			((sd->stats[size_bin][ndx].average_tx_time * ssc->smoothing_rate) + 
			 (tt * (100 - ssc->smoothing_rate))) / 100;
	}

	if (is_fail) {
		int y;

		sd->stats[size_bin][ndx].successive_failures++;
		for (y = size_bin + 1; y < NUM_PACKET_SIZE_BINS; y++) {
			/*
			 * also say larger packets failed since we
			 * assume if a small packet fails at a lower
			 * bit-rate then a larger one will also.
			 */
			sd->stats[y][ndx].successive_failures++;
			sd->stats[y][ndx].last_tx = ticks;
			sd->stats[y][ndx].tries += data_tries;
			sd->stats[y][ndx].total_packets++;
		}
	} else {
		sd->stats[size_bin][ndx].packets_acked++;
		sd->stats[size_bin][ndx].successive_failures = 0;
	}
	sd->stats[size_bin][ndx].last_tx = ticks;
	sd->stats[size_bin][ndx].tries += data_tries;
	sd->stats[size_bin][ndx].total_packets++;

	if (ndx == sd->current_sample_ndx[size_bin]) {
		DPRINTF(ssc, 10, "%s: %6D size %d sample rate %d "
			"tries (d%d/r%d) tt %d avg_tt (%d/%d)%s\n", 
			__func__, ni->ni_macaddr, ":",
			size, IEEE80211_RS_RATE(&ni->ni_rates, ndx),
			data_tries, rts_tries, tt,
			sd->stats[size_bin][ndx].average_tx_time,
			sd->stats[size_bin][ndx].perfect_tx_time,
			is_fail ? " fail" : "");
		sd->sample_tt[size_bin] = tt;
		sd->current_sample_ndx[size_bin] = -1;
	}
}

static void
sample_tx_complete(void *arg, struct ieee80211_node *ni, int frame_len,
		   const struct ieee80211_ratectl_res res[], int res_len,
		   int data_retry, int rts_retry, int is_fail0)
{
	struct sample_softc *ssc = arg;
	struct sample_data *sd = ni->ni_rate_data;
	int i, size_bin, size;

	KKASSERT(frame_len > 0);
	size_bin = size_to_bin(frame_len);
	size = bin_to_size(size_bin);

	if (sd == NULL || !sd->started) {
		DPRINTF(ssc, 10, "%s: %6D size %d retries (d%d/r%d) "
			"no rates yet\n", __func__,
			ni->ni_macaddr, ":",
			size, data_retry, rts_retry);
		return;
	}

	for (i = 0; i < res_len; ++i) {
		if (res[i].rc_res_rateidx >= ni->ni_rates.rs_nrates) {
			DPRINTF(ssc, 5, "%s: number of rates changed, "
				"restart\n", __func__);
			sample_start(ssc, ni);
			return;
		}
	}

	DPRINTF(ssc, 20, "%s: %6D size %d retries (d%d/r%d)\n",
		__func__, ni->ni_macaddr, ":",
		size, data_retry, rts_retry);
	for (i = 0; i < res_len; ++i) {
		int is_fail = is_fail0;

		if (i == 0 && (data_retry + 1) > res[0].rc_res_tries &&
		    res[0].rc_res_rateidx == sd->current_sample_ndx[size_bin])
			is_fail = 1;

		sample_update_stats(ssc, ni, size, size_bin,
				    &res[i], res_len - i,
				    rts_retry + 1, is_fail);
	}
}

static void
sample_newassoc(void *arg, struct ieee80211_node *ni, int isnew)
{
	struct sample_softc *ssc = arg;

	DPRINTF(ssc, 5, "%s: %6D isnew %d\n", __func__,
		ni->ni_macaddr, ":", isnew);

	if (isnew)
		sample_start(ssc, ni);
}

/*
 * Initialize the tables for a node.
 */
static void
sample_start(struct sample_softc *ssc, struct ieee80211_node *ni)
{
#define	RATE(_ix)	IEEE80211_RS_RATE(&ni->ni_rates, (_ix))
	struct ieee80211com *ic = ssc->ic;
	struct sample_data *sd = ni->ni_rate_data;
	int x, y, srate;

	if (sd == NULL) {
		sample_data_alloc(ni);

		sd = ni->ni_rate_data;
		if (sd == NULL)
			return;
	}

        sd->static_rate_ndx = -1;
	if (ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE) {
		/*
		 * A fixed rate is to be used; ic_fixed_rate is an
		 * index into the supported rate set.  Convert this
		 * to the index into the negotiated rate set for
		 * the node.
		 */
		const struct ieee80211_rateset *rs =
			&ic->ic_sup_rates[ic->ic_curmode];
		int r = IEEE80211_RS_RATE(rs, ic->ic_fixed_rate);

		/* NB: the rate set is assumed sorted */
		srate = ni->ni_rates.rs_nrates - 1;
		for (; srate >= 0 && RATE(srate) != r; srate--)
			;
		KASSERT(srate >= 0,
			("fixed rate %d not in rate set", ic->ic_fixed_rate));
		sd->static_rate_ndx = srate;
	}

#ifdef SAMPLE_DEBUG
        DPRINTF(ssc, 1, "%s: %6D size 1600 rate/tt", __func__,
		ni->ni_macaddr, ":");
        for (x = 0; x < ni->ni_rates.rs_nrates; x++) {
		DPRINTF(ssc, 1, " %d/%d", RATE(x),
			unicast_pkt_time(ssc, ni, RATE(x), 1600, 1, 1, NULL));
	}
	DPRINTF(ssc, 1, "%s\n", "");
#endif

	/* Set the visible bit-rate to the lowest one available */
	ni->ni_txrate = 0;

	for (y = 0; y < NUM_PACKET_SIZE_BINS; y++) {
		int size = bin_to_size(y);
		int ndx = 0;

		sd->packets_sent[y] = 0;
		sd->current_sample_ndx[y] = -1;
		sd->last_sample_ndx[y] = 0;

		DPRINTF(ssc, 1, "%s: %6D size %d rate/tt", __func__,
			ni->ni_macaddr, ":", size);
		for (x = 0; x < ni->ni_rates.rs_nrates; x++) {
			sd->stats[y][x].successive_failures = 0;
			sd->stats[y][x].tries = 0;
			sd->stats[y][x].total_packets = 0;
			sd->stats[y][x].packets_acked = 0;
			sd->stats[y][x].last_tx = 0;

			sd->stats[y][x].perfect_tx_time =
				unicast_pkt_time(ssc, ni, RATE(x), size,
						 1, 1, NULL);

			DPRINTF(ssc, 1, " %d/%d", RATE(x),
				sd->stats[y][x].perfect_tx_time);

			sd->stats[y][x].average_tx_time =
				sd->stats[y][x].perfect_tx_time;
		}
		DPRINTF(ssc, 1, "%s\n", "");

		/* Set the initial rate */
		for (ndx = ni->ni_rates.rs_nrates - 1; ndx > 0; ndx--) {
			if (RATE(ndx) <= 72)
				break;
		}
		sd->current_rate[y] = ndx;
	}

	DPRINTF(ssc, 1, "%s: %6D %d rates %d%sMbps (%dus)- %d%sMbps (%dus)\n",
		__func__, ni->ni_macaddr, ":",
		ni->ni_rates.rs_nrates,
		RATE(0) / 2, RATE(0) % 2 ? ".5" : "",
		sd->stats[1][0].perfect_tx_time,
		RATE(ni->ni_rates.rs_nrates - 1) / 2,
		RATE(ni->ni_rates.rs_nrates - 1) % 2 ? ".5" : "",
		sd->stats[1][ni->ni_rates.rs_nrates - 1].perfect_tx_time);

        if (sd->static_rate_ndx != -1)
		ni->ni_txrate = sd->static_rate_ndx;
	else
		ni->ni_txrate = sd->current_rate[0];
#undef RATE

	sd->started = 1;
}

static void
sample_rate_cb(void *arg, struct ieee80211_node *ni)
{
	sample_newassoc(arg, ni, 1);
}

/*
 * Reset the rate control state for each 802.11 state transition.
 */
static void
sample_newstate(void *arg, enum ieee80211_state state)
{
	struct sample_softc *ssc = arg;

	if (state == IEEE80211_S_RUN) {
		struct ieee80211com *ic = ssc->ic;

		if (ic->ic_opmode != IEEE80211_M_STA) {
			/*
			 * Sync rates for associated stations and neighbors.
			 */
			ieee80211_iterate_nodes(&ic->ic_sta, sample_rate_cb,
						ssc);
		}
		sample_newassoc(ssc, ic->ic_bss, 1);
	}
}

static void
sample_sysctl_attach(struct sample_softc *ssc)
{
	ssc->smoothing_rate = 95;
	ssc->sample_rate = 10;
	ssc->debug = 0;

	sysctl_ctx_init(&ssc->sysctl_ctx);
	ssc->sysctl_oid = SYSCTL_ADD_NODE(&ssc->sysctl_ctx,
		SYSCTL_CHILDREN(ssc->ic->ic_sysctl_oid),
		OID_AUTO, "sample_ratectl", CTLFLAG_RD, 0, "");
	if (ssc->sysctl_oid == NULL) {
		kprintf("wlan_ratectl_sample: create sysctl tree failed\n");
		return;
	}

	/* XXX bounds check [0..100] */
	SYSCTL_ADD_INT(&ssc->sysctl_ctx, SYSCTL_CHILDREN(ssc->sysctl_oid),
		       OID_AUTO, "smoothing_rate", CTLFLAG_RW,
		       &ssc->smoothing_rate, 0,
		       "rate control: "
		       "retry threshold to credit rate raise (%%)");

	/* XXX bounds check [2..100] */
	SYSCTL_ADD_INT(&ssc->sysctl_ctx, SYSCTL_CHILDREN(ssc->sysctl_oid),
		       OID_AUTO, "sample_rate", CTLFLAG_RW,
		       &ssc->sample_rate, 0,
		       "rate control: "
		       "# good periods before raising rate");

	SYSCTL_ADD_INT(&ssc->sysctl_ctx, SYSCTL_CHILDREN(ssc->sysctl_oid),
		       OID_AUTO, "debug", CTLFLAG_RW, &ssc->debug, 0,
		       "rate control: debug level");
}

static void *
sample_attach(struct ieee80211com *ic)
{
	struct sample_softc *ssc;

	sample_nrefs++;

	ssc = kmalloc(sizeof(struct sample_softc), M_DEVBUF, M_WAITOK | M_ZERO);
	ssc->ic = ic;
	sample_sysctl_attach(ssc);

	sample_newstate(ssc, ic->ic_state);

	return ssc;
}

static void
_sample_data_free(void *arg __unused, struct ieee80211_node *ni)
{
	sample_data_free(ni);
}

static void
sample_detach(void *arg)
{
	struct sample_softc *ssc = arg;
	struct ieee80211com *ic = ssc->ic;

	sample_newstate(ssc, IEEE80211_S_INIT);

	ieee80211_iterate_nodes(&ic->ic_sta, _sample_data_free, NULL);
	ieee80211_iterate_nodes(&ic->ic_scan, _sample_data_free, NULL);

	if (ssc->sysctl_oid != NULL)
		sysctl_ctx_free(&ssc->sysctl_ctx);
	kfree(ssc, M_DEVBUF);

	sample_nrefs--;
}

static void
sample_data_alloc(struct ieee80211_node *ni)
{
	KKASSERT(ni->ni_rate_data == NULL);
	ni->ni_rate_data = kmalloc(sizeof(struct sample_data),
				   M_SAMPLE_RATECTL_DATA, M_NOWAIT | M_ZERO);
}

static void
sample_data_free(struct ieee80211_node *ni)
{
	if (ni->ni_rate_data != NULL) {
		kfree(ni->ni_rate_data, M_SAMPLE_RATECTL_DATA);
		ni->ni_rate_data = NULL;
	}
}

static void
sample_data_dup(const struct ieee80211_node *oni, struct ieee80211_node *nni)
{
	if (oni->ni_rate_data == NULL || nni->ni_rate_data == NULL)
		return;

	bcopy(oni->ni_rate_data, nni->ni_rate_data,
	      sizeof(struct sample_data));
}

/*
 * Module glue.
 */
static int
sample_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		ieee80211_ratectl_register(&sample);
		return 0;
	case MOD_UNLOAD:
		if (sample_nrefs) {
			kprintf("wlan_ratectl_sample: still in use "
				"(%u dynamic refs)\n", sample_nrefs);
			return EBUSY;
		}
		ieee80211_ratectl_unregister(&sample);
		return 0;
	}
	return EINVAL;
}

static moduledata_t sample_mod = {
	"wlan_ratectl_sample",
	sample_modevent,
	0
};
DECLARE_MODULE(wlan_ratectl_sample, sample_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(wlan_ratectl_sample, 1);
MODULE_DEPEND(wlan_ratectl_sample, wlan, 1, 1, 1);
