/*
 * Copyright (c) 2004-2005 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/net80211/ieee80211_acl.c,v 1.3.2.1 2005/09/03 22:40:02 sam Exp $
 * $DragonFly: src/sys/netproto/802_11/wlan_acl/ieee80211_acl.c,v 1.1 2006/05/18 13:51:46 sephe Exp $
 */

/*
 * IEEE 802.11 MAC ACL support.
 *
 * When this module is loaded the sender address of each received
 * frame is passed to the iac_check method and the module indicates
 * if the frame should be accepted or rejected.  If the policy is
 * set to ACL_POLICY_OPEN then all frames are accepted w/o checking
 * the address.  Otherwise, the address is looked up in the database
 * and if found the frame is either accepted (ACL_POLICY_ALLOW)
 * or rejected (ACL_POLICY_DENT).
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h> 
#include <sys/mbuf.h>   
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/serialize.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>
#include <net/ethernet.h>
#include <net/route.h>

#include <netproto/802_11/ieee80211_var.h>

enum {
	ACL_POLICY_OPEN		= 0,	/* open, don't check ACL's */
	ACL_POLICY_ALLOW	= 1,	/* allow traffic from MAC */
	ACL_POLICY_DENY		= 2,	/* deny traffic from MAC */
};

#define	ACL_HASHSIZE	32

struct acl {
	TAILQ_ENTRY(acl)	acl_list;
	LIST_ENTRY(acl)		acl_hash;
	uint8_t			acl_macaddr[IEEE80211_ADDR_LEN];
};
struct aclstate {
#if 0
	acl_lock_t		as_lock;
#endif
	int			as_policy;
	int			as_nacls;
	TAILQ_HEAD(, acl)	as_list;	/* list of all ACL's */
	LIST_HEAD(, acl)	as_hash[ACL_HASHSIZE];
	struct ieee80211com	*as_ic;
};

/* simple hash is enough for variation of macaddr */
#define	ACL_HASH(addr)	\
	(((const uint8_t *)(addr))[IEEE80211_ADDR_LEN - 1] % ACL_HASHSIZE)

MALLOC_DEFINE(M_80211_ACL, "acl", "802.11 station acl");

static	int acl_free_all(struct ieee80211com *);

static int
acl_attach(struct ieee80211com *ic)
{
	struct aclstate *as;

	as = malloc(sizeof(struct aclstate), M_80211_ACL, M_NOWAIT | M_ZERO);
	if (as == NULL)
		return 0;
	TAILQ_INIT(&as->as_list);
	as->as_policy = ACL_POLICY_OPEN;
	as->as_ic = ic;
	ic->ic_as = as;
	return 1;
}

static void
acl_detach(struct ieee80211com *ic)
{
	struct aclstate *as = ic->ic_as;

	acl_free_all(ic);
	ic->ic_as = NULL;
	free(as, M_DEVBUF);
}

static __inline struct acl *
_find_acl(struct aclstate *as, const uint8_t *macaddr)
{
	struct acl *acl;
	int hash;

	hash = ACL_HASH(macaddr);
	LIST_FOREACH(acl, &as->as_hash[hash], acl_hash) {
		if (IEEE80211_ADDR_EQ(acl->acl_macaddr, macaddr))
			return acl;
	}
	return NULL;
}

static void
_acl_free(struct aclstate *as, struct acl *acl)
{
	ASSERT_SERIALIZED(as->as_ic->ic_ifp->if_serializer);

	TAILQ_REMOVE(&as->as_list, acl, acl_list);
	LIST_REMOVE(acl, acl_hash);
	free(acl, M_80211_ACL);
	as->as_nacls--;
}

static int
acl_check(struct ieee80211com *ic, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct aclstate *as = ic->ic_as;

	switch (as->as_policy) {
	case ACL_POLICY_OPEN:
		return 1;
	case ACL_POLICY_ALLOW:
		return _find_acl(as, mac) != NULL;
	case ACL_POLICY_DENY:
		return _find_acl(as, mac) == NULL;
	}
	return 0;		/* should not happen */
}

static int
acl_add(struct ieee80211com *ic, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct aclstate *as = ic->ic_as;
	struct acl *acl, *new;
	int hash;

	ASSERT_SERIALIZED(ic->ic_ifp->if_serializer);

	new = malloc(sizeof(struct acl), M_80211_ACL, M_NOWAIT | M_ZERO);
	if (new == NULL) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ACL,
			"ACL: add %6D failed, no memory\n", mac, ":");
		/* XXX statistic */
		return ENOMEM;
	}

	hash = ACL_HASH(mac);
	LIST_FOREACH(acl, &as->as_hash[hash], acl_hash) {
		if (IEEE80211_ADDR_EQ(acl->acl_macaddr, mac)) {
			FREE(new, M_80211_ACL);
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ACL,
				"ACL: add %6D failed, already present\n",
				mac, ":");
			return EEXIST;
		}
	}
	IEEE80211_ADDR_COPY(new->acl_macaddr, mac);
	TAILQ_INSERT_TAIL(&as->as_list, new, acl_list);
	LIST_INSERT_HEAD(&as->as_hash[hash], new, acl_hash);
	as->as_nacls++;

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_ACL,
		"ACL: add %6D\n", mac, ":");
	return 0;
}

static int
acl_remove(struct ieee80211com *ic, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct aclstate *as = ic->ic_as;
	struct acl *acl;

	ASSERT_SERIALIZED(ic->ic_ifp->if_serializer);

	acl = _find_acl(as, mac);
	if (acl != NULL)
		_acl_free(as, acl);

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_ACL,
		"ACL: remove %6D%s\n", mac, ":",
		acl == NULL ? ", not present" : "");

	return (acl == NULL ? ENOENT : 0);
}

static int
acl_free_all(struct ieee80211com *ic)
{
	struct aclstate *as = ic->ic_as;
	struct acl *acl;

	ASSERT_SERIALIZED(ic->ic_ifp->if_serializer);

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_ACL, "ACL: %s\n", "free all");

	while ((acl = TAILQ_FIRST(&as->as_list)) != NULL)
		_acl_free(as, acl);

	return 0;
}

static int
acl_setpolicy(struct ieee80211com *ic, int policy)
{
	struct aclstate *as = ic->ic_as;

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_ACL,
		"ACL: set policy to %u\n", policy);

	switch (policy) {
	case IEEE80211_MACCMD_POLICY_OPEN:
		as->as_policy = ACL_POLICY_OPEN;
		break;
	case IEEE80211_MACCMD_POLICY_ALLOW:
		as->as_policy = ACL_POLICY_ALLOW;
		break;
	case IEEE80211_MACCMD_POLICY_DENY:
		as->as_policy = ACL_POLICY_DENY;
		break;
	default:
		return EINVAL;
	}
	return 0;
}

static int
acl_getpolicy(struct ieee80211com *ic)
{
	return ((struct aclstate *)(ic->ic_as))->as_policy;
}

static int
acl_setioctl(struct ieee80211com *ic, struct ieee80211req *ireq)
{
	return EINVAL;
}

static int
acl_getioctl(struct ieee80211com *ic, struct ieee80211req *ireq)
{
	struct aclstate *as = ic->ic_as;
	struct acl *acl;
	struct ieee80211req_maclist *ap;
	int error, space, i;

	ASSERT_SERIALIZED(ic->ic_ifp->if_serializer);

	switch (ireq->i_val) {
	case IEEE80211_MACCMD_POLICY:
		ireq->i_val = as->as_policy;
		return 0;
	case IEEE80211_MACCMD_LIST:
		space = as->as_nacls * IEEE80211_ADDR_LEN;
		if (ireq->i_len == 0) {
			ireq->i_len = space;	/* return required space */
			return 0;		/* NB: must not error */
		}
		ap = malloc(space, M_TEMP, M_NOWAIT);
		if (ap == NULL)
			return ENOMEM;
		i = 0;
		TAILQ_FOREACH(acl, &as->as_list, acl_list) {
			IEEE80211_ADDR_COPY(ap[i].ml_macaddr, acl->acl_macaddr);
			i++;
		}
		if (ireq->i_len >= space) {
			error = copyout(ap, ireq->i_data, space);
			ireq->i_len = space;
		} else
			error = copyout(ap, ireq->i_data, ireq->i_len);
		free(ap, M_TEMP);
		return error;
	}
	return EINVAL;
}

static const struct ieee80211_aclator mac = {
	.iac_name	= "mac",
	.iac_attach	= acl_attach,
	.iac_detach	= acl_detach,
	.iac_check	= acl_check,
	.iac_add	= acl_add,
	.iac_remove	= acl_remove,
	.iac_flush	= acl_free_all,
	.iac_setpolicy	= acl_setpolicy,
	.iac_getpolicy	= acl_getpolicy,
	.iac_setioctl	= acl_setioctl,
	.iac_getioctl	= acl_getioctl,
};

/*
 * Module glue.
 */
static int
wlan_acl_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		if (bootverbose)
			printf("wlan: <802.11 MAC ACL support>\n");
		ieee80211_aclator_register(&mac);
		return 0;
	case MOD_UNLOAD:
		ieee80211_aclator_unregister(&mac);
		return 0;
	}
	return EINVAL;
}

static moduledata_t wlan_acl_mod = {
	"wlan_acl",
	wlan_acl_modevent,
	0
};
DECLARE_MODULE(wlan_acl, wlan_acl_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(wlan_acl, 1);
MODULE_DEPEND(wlan_acl, wlan, 1, 1, 1);
