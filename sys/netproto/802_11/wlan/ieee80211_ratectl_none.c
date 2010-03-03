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
 * $DragonFly: src/sys/netproto/802_11/wlan/ieee80211_ratectl_none.c,v 1.3 2008/01/15 09:01:13 sephe Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
 
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>

#include <netproto/802_11/ieee80211_var.h>

static void	*none_ratectl_attach(struct ieee80211vap *);
static void	none_ratectl_detach(void *);
static void	none_ratectl_data_alloc(struct ieee80211_node *);
static void	none_ratectl_data_free(struct ieee80211_node *);
static void	none_ratectl_data_dup(const struct ieee80211_node *,
				      struct ieee80211_node *);
static void	none_ratectl_newstate(void *, enum ieee80211_state);
static void	none_ratectl_tx_complete(void *, struct ieee80211_node *,
			int, const struct ieee80211_ratectl_res[],
			int, int, int, int);
static void	none_ratectl_newassoc(void *, struct ieee80211_node *, int);
static int	none_ratectl_findrate(void *, struct ieee80211_node *, int,
				      int[], int);

const struct ieee80211_ratectl ieee80211_ratectl_none = {
	.rc_name	= "none",
	.rc_ratectl	= IEEE80211_RATECTL_NONE,
	.rc_attach	= none_ratectl_attach,
	.rc_detach	= none_ratectl_detach,
	.rc_data_alloc	= none_ratectl_data_alloc,
	.rc_data_free	= none_ratectl_data_free,
	.rc_data_dup	= none_ratectl_data_dup,
	.rc_newstate	= none_ratectl_newstate,
	.rc_tx_complete	= none_ratectl_tx_complete,
	.rc_newassoc	= none_ratectl_newassoc,
	.rc_findrate	= none_ratectl_findrate
};

static void *
none_ratectl_attach(struct ieee80211vap *vap)
{
	struct ieee80211_ratectl_state *rc_st = &vap->iv_ratectl;

	rc_st->rc_st_attach(vap, IEEE80211_RATECTL_NONE);
	return NULL;
}

static void
none_ratectl_detach(void *arg __unused)
{
}

static void
none_ratectl_data_alloc(struct ieee80211_node *ni __unused)
{
}

static void
none_ratectl_data_free(struct ieee80211_node *ni __unused)
{
}

static void
none_ratectl_data_dup(const struct ieee80211_node *oni __unused,
		      struct ieee80211_node *nni __unused)
{
}

static void
none_ratectl_newstate(void *arg __unused, enum ieee80211_state state __unused)
{
}

static void
none_ratectl_tx_complete(void *arg __unused, struct ieee80211_node *ni __unused,
			 int frame_len __unused,
			 const struct ieee80211_ratectl_res res[] __unused,
			 int res_len __unused,
			 int data_retries __unused, int rts_retries __unused,
			 int is_fail __unused)
{
}

static void
none_ratectl_newassoc(void *arg __unused, struct ieee80211_node *ni __unused,
		      int is_new __unused)
{
}

static int
none_ratectl_findrate(void * arg __unused, struct ieee80211_node *ni,
		      int frame_len __unused, int rateidx[], int rateidx_len)
{
	int i;

	for (i = 0; i < rateidx_len; ++i)
		rateidx[i] = ni->ni_txrate;
	return rateidx_len;
}
