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
static void none_ratectl_init(struct ieee80211vap *);
static void none_ratectl_deinit(struct ieee80211vap *);
static void none_ratectl_node_init(struct ieee80211_node *);
static void none_ratectl_node_deinit(struct ieee80211_node *);
static void none_ratectl_rate(struct ieee80211vap *, void *, uint32_t);
static void	none_ratectl_tx_complete(const struct ieee80211vap *,
			struct ieee80211_node *, int, void *, void *);
static void none_ratectl_tx_update(const struct ieee80211vap *,
			const struct ieee80211_node *, void *, void *, void *);
static void none_ratectl_setinterval(const struct ieee80211vap *, int);

const struct ieee80211_ratectl ieee80211_ratectl_none = {
	.rc_name	= "none",
	.rc_attach	= none_ratectl_attach,
	.rc_detach	= none_ratectl_detach,
	.rc_init	= none_ratectl_init,
	.rc_deinit	= none_ratectl_deinit,
	.rc_node_init	= none_ratectl_node_init,
	.rc_node_deinit	= none_ratectl_node_deinit,
	.rc_rate = none_ratectl_rate,
	.rc_tx_complete	= none_ratectl_tx_complete,
	.rc_tx_update = none_ratectl_tx_update,
	.rc_setinterval = none_ratectl_setinterval,
};

IEEE80211_RATECTL_MODULE(ieee80211_ratectl_none, 1);
IEEE80211_RATECTL_ALG(ieee80211_ratectl_none, IEEE80211_RATECTL_NONE,
	ieee80211_ratectl_none);

static void
none_ratectl_setinterval(const struct ieee80211vap *vap __unused,
	int msecs __unused)
{
}

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
none_ratectl_init(struct ieee80211vap *vap __unused)
{
}

static void
none_ratectl_deinit(struct ieee80211vap *vap __unused)
{
}

static void
none_ratectl_node_init(struct ieee80211_node *ni __unused)
{
}

static void
none_ratectl_node_deinit(struct ieee80211_node *ni __unused)
{
}

static void
none_ratectl_tx_complete(const struct ieee80211vap *vap __unused,
			 struct ieee80211_node *ni __unused, int ok __unused,
			 void *arg1 __unused, void *arg2 __unused)
{
}

static void
none_ratectl_tx_update(const struct ieee80211vap *vap __unused,
			 const struct ieee80211_node *ni __unused,
			 void *arg1 __unused, void *arg2 __unused, void *arg3 __unused)
{
}
