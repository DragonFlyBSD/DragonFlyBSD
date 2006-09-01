/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
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
 * $DragonFly: src/sys/netproto/802_11/wlan/ieee80211_ratectl.c,v 1.1 2006/09/01 15:12:11 sephe Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
 
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>

#include <netproto/802_11/ieee80211_var.h>

static const struct ieee80211_ratectl *ratectls[IEEE80211_RATECTL_MAX] = {
	[IEEE80211_RATECTL_NONE]	= &ieee80211_ratectl_none
};

static const char *ratectl_modname[IEEE80211_RATECTL_MAX] = {
	[IEEE80211_RATECTL_ONOE]	= "wlan_ratectl_onoe",
	[IEEE80211_RATECTL_AMRR]	= "wlan_ratectl_amrr"
};

void
ieee80211_ratectl_attach(struct ieee80211com *ic)
{
	struct ieee80211_ratectl_state *rc_st = &ic->ic_ratectl;
	u_int cur_ratectl = rc_st->rc_st_ratectl;

	rc_st->rc_st_ratectl_cap |= IEEE80211_RATECTL_CAP_NONE;
	rc_st->rc_st_ratectl = IEEE80211_RATECTL_NONE;

	ieee80211_ratectl_change(ic, cur_ratectl);
}

void
ieee80211_ratectl_detach(struct ieee80211com *ic)
{
	ieee80211_ratectl_change(ic, IEEE80211_RATECTL_NONE);
}

void
ieee80211_ratectl_register(const struct ieee80211_ratectl *rc)
{
	/*
	 * Sanity checks
	 */
	if (rc->rc_ratectl >= IEEE80211_RATECTL_MAX) {
		printf("%s: rate control %s has an invalid index %d\n",
		       __func__, rc->rc_name, rc->rc_ratectl);
		return;
	}
	if (ratectls[rc->rc_ratectl] != NULL &&
	    ratectls[rc->rc_ratectl] != rc) {
		printf("%s: rate control index %d is registered by %s\n",
		       __func__, rc->rc_ratectl,
		       ratectls[rc->rc_ratectl]->rc_name);
		return;
	}

	ratectls[rc->rc_ratectl] = rc;
}

void
ieee80211_ratectl_unregister(const struct ieee80211_ratectl *rc)
{
	/*
	 * Sanity checks
	 */
	if (rc->rc_ratectl >= IEEE80211_RATECTL_MAX) {
		printf("%s: rate control %s has an invalid index %d\n",
		       __func__, rc->rc_name, rc->rc_ratectl);
		return;
	}
	if (ratectls[rc->rc_ratectl] != NULL &&
	    ratectls[rc->rc_ratectl] != rc) {
		printf("%s: rate control index %d is registered by %s\n",
		       __func__, rc->rc_ratectl,
		       ratectls[rc->rc_ratectl]->rc_name);
		return;
	}

	/*
	 * Indiviual rate control module MUST maintain reference count itself.
	 */
	ratectls[rc->rc_ratectl] = NULL;
}

int
ieee80211_ratectl_change(struct ieee80211com *ic, u_int rc_idx)
{
	struct ieee80211_ratectl_state *rc_st = &ic->ic_ratectl;
	const struct ieee80211_ratectl *rc, *rc_old;

	if (rc_idx == rc_st->rc_st_ratectl) {
		/* Nothing need to be changed */
		return 0;
	}

	if ((IEEE80211_RATECTL_CAP(rc_idx) & rc_st->rc_st_ratectl_cap) == 0) {
		/* We are not capable to do requested rate control */
		return EOPNOTSUPP;
	}

	rc = ratectls[rc_idx];
	if (rc == NULL) {
		/* Try load the rate control module */
		ieee80211_load_module(ratectl_modname[rc_idx]);

		/*
		 * If rate control module loaded it should immediately
		 * call ieee80211_ratectl_register() which will fill in
		 * the entry in the 'ratectls' array.
		 */
		rc = ratectls[rc_idx];
		if (rc == NULL) {
			printf("%s: can't load requested rate control module",
			       __func__);
			return EOPNOTSUPP;
		}
	}

	/* Detach old rate control */
	rc_old = ratectls[rc_st->rc_st_ratectl];
	rc_old->rc_detach(rc_st->rc_st_ctx);

	if (rc_st->rc_st_change != NULL)
		rc_st->rc_st_change(ic, rc_st->rc_st_ratectl, rc_idx);

	/* Attach new rate control */
	rc_st->rc_st_ratectl = rc_idx;
	rc_st->rc_st_ctx = rc->rc_attach(ic);

	return 0;
}

void
ieee80211_ratectl_data_alloc(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_ratectl_state *rc_st = &ic->ic_ratectl;
	const struct ieee80211_ratectl *rc = ratectls[rc_st->rc_st_ratectl];

	rc->rc_data_alloc(ni);
}

void
ieee80211_ratectl_data_dup(const struct ieee80211_node *oni,
			   struct ieee80211_node *nni)
{
	struct ieee80211com *ic = oni->ni_ic;
	struct ieee80211_ratectl_state *rc_st = &ic->ic_ratectl;
	const struct ieee80211_ratectl *rc = ratectls[rc_st->rc_st_ratectl];

	rc->rc_data_dup(oni, nni);
}

void
ieee80211_ratectl_data_free(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_ratectl_state *rc_st = &ic->ic_ratectl;
	const struct ieee80211_ratectl *rc = ratectls[rc_st->rc_st_ratectl];

	rc->rc_data_free(ni);
}

void
ieee80211_ratectl_newstate(struct ieee80211com *ic, enum ieee80211_state state)
{
	struct ieee80211_ratectl_state *rc_st = &ic->ic_ratectl;
	const struct ieee80211_ratectl *rc = ratectls[rc_st->rc_st_ratectl];

	rc->rc_newstate(rc_st->rc_st_ctx, state);
}

void
ieee80211_ratectl_tx_complete(struct ieee80211_node *ni, int frame_len,
			      const struct ieee80211_ratectl_res res[],
			      int res_len, int short_retries, int long_retries,
			      int is_fail)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_ratectl_state *rc_st = &ic->ic_ratectl;
	const struct ieee80211_ratectl *rc = ratectls[rc_st->rc_st_ratectl];

	rc->rc_tx_complete(rc_st->rc_st_ctx, ni, frame_len, res, res_len,
			   long_retries, short_retries, is_fail);
}

void
ieee80211_ratectl_newassoc(struct ieee80211_node *ni, int is_new)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_ratectl_state *rc_st = &ic->ic_ratectl;
	const struct ieee80211_ratectl *rc = ratectls[rc_st->rc_st_ratectl];

	rc->rc_newassoc(rc_st->rc_st_ctx, ni, is_new);
}

int
ieee80211_ratectl_findrate(struct ieee80211_node *ni, int frame_len,
			   int rateidx[], int rateidx_len)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_ratectl_state *rc_st = &ic->ic_ratectl;
	const struct ieee80211_ratectl *rc = ratectls[rc_st->rc_st_ratectl];

	KKASSERT(rateidx_len > 0);

	return rc->rc_findrate(rc_st->rc_st_ctx, ni, frame_len,
			       rateidx, rateidx_len);
}
