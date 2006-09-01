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
 * $DragonFly: src/sys/netproto/802_11/ieee80211_ratectl.h,v 1.1 2006/09/01 15:12:11 sephe Exp $
 */

#ifndef _NET80211_IEEE80211_RATECTL_H
#define _NET80211_IEEE80211_RATECTL_H

struct ieee80211_ratectl_state {
	void		*rc_st_ctx;
	u_int		rc_st_ratectl;		/* see IEEE80211_RATECTL_ */
	uint32_t	rc_st_ratectl_cap;	/* see IEEE80211_RATECTL_CAP_ */
	void		(*rc_st_change)(struct ieee80211com *, u_int, u_int);
};

struct ieee80211_ratectl_res {
	int		rc_res_rateidx;
	int		rc_res_tries;
};

struct ieee80211_ratectl {
	const char	*rc_name;
	u_int		rc_ratectl;	/* see IEEE80211_RATECTL_ */

	void		*(*rc_attach)(struct ieee80211com *);
	void		(*rc_detach)(void *);

	void		(*rc_data_alloc)(struct ieee80211_node *);
	void		(*rc_data_free)(struct ieee80211_node *);
	void		(*rc_data_dup)(const struct ieee80211_node *,
				       struct ieee80211_node *);

	void		(*rc_newstate)(void *, enum ieee80211_state);
	void		(*rc_tx_complete)(void *, struct ieee80211_node *, int,
					  const struct ieee80211_ratectl_res[],
					  int, int, int, int);
	void		(*rc_newassoc)(void *, struct ieee80211_node *, int);
	int		(*rc_findrate)(void *, struct ieee80211_node *, int,
				       int[], int);
};

#define IEEE80211_RATECTL_NONE		0
#define IEEE80211_RATECTL_ONOE		1
#define IEEE80211_RATECTL_AMRR		2
#define IEEE80211_RATECTL_MAX		3

#define IEEE80211_AMRR_RATEIDX_MAX	4
#define IEEE80211_RATEIDX_MAX		5

#define IEEE80211_RATECTL_CAP(v)	(1 << (v))

#define _IEEE80211_RATECTL_CAP(n)	\
	IEEE80211_RATECTL_CAP(IEEE80211_RATECTL_##n)

#define IEEE80211_RATECTL_CAP_NONE	_IEEE80211_RATECTL_CAP(NONE)
#define IEEE80211_RATECTL_CAP_ONOE	_IEEE80211_RATECTL_CAP(ONOE)
#define IEEE80211_RATECTL_CAP_AMRR	_IEEE80211_RATECTL_CAP(AMRR)

extern const struct ieee80211_ratectl	ieee80211_ratectl_none;

void	ieee80211_ratectl_attach(struct ieee80211com *);
void	ieee80211_ratectl_detach(struct ieee80211com *);

void	ieee80211_ratectl_register(const struct ieee80211_ratectl *);
void	ieee80211_ratectl_unregister(const struct ieee80211_ratectl *);

int	ieee80211_ratectl_change(struct ieee80211com *, u_int);

void	ieee80211_ratectl_data_alloc(struct ieee80211_node *);
void	ieee80211_ratectl_data_dup(const struct ieee80211_node *,
				   struct ieee80211_node *);
void	ieee80211_ratectl_data_free(struct ieee80211_node *);

void	ieee80211_ratectl_newstate(struct ieee80211com *,
				   enum ieee80211_state);
void	ieee80211_ratectl_tx_complete(struct ieee80211_node *, int,
				      const struct ieee80211_ratectl_res[],
				      int, int, int, int);
void	ieee80211_ratectl_newassoc(struct ieee80211_node *, int);
int	ieee80211_ratectl_findrate(struct ieee80211_node *, int, int[], int);

#endif	/* !_NET80211_IEEE80211_RATECTL_H */
