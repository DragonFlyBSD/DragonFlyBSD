/*
 * Copyright 1994, 1995 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * at_rmx.c,v 1.13 1995/05/30 08:09:31 rgrimes Exp
 * $DragonFly: src/sys/netproto/atalk/at_rmx.c,v 1.4 2006/12/22 23:57:53 swildner Exp $
 */

/* This code generates debugging traces to the radix code */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>

#include <net/route.h>

int at_inithead(void **head, int off);

static char hexbuf[256];

static char *
prsockaddr(void *v)
{
	char *bp = &hexbuf[0];
	u_char *cp = v;

	if (v) {
		int len = *cp;
		u_char *cplim = cp + len;

		/* return: "(len) hexdump" */

		bp += ksprintf(bp, "(%d)", len);
		for (cp++; cp < cplim && bp < hexbuf+252; cp++) {
			*bp++ = "0123456789abcdef"[*cp / 16];
			*bp++ = "0123456789abcdef"[*cp % 16];
		}
	} else {
		bp+= ksprintf(bp, "null");
	}
	*bp = '\0';
	
	return &hexbuf[0];
}

static struct radix_node *
at_addroute(char *key, char *mask, struct radix_node_head *head,
	    struct radix_node *treenodes)
{
	struct radix_node *rn;

	kprintf("at_addroute: v=%s\n", prsockaddr(key));
	kprintf("at_addroute: n=%s\n", prsockaddr(mask));
	kprintf("at_addroute: head=%p treenodes=%p\n",
	    (void *)head, (void *)treenodes);

	rn = rn_addroute(key, mask, head, treenodes);

	kprintf("at_addroute: returns rn=%p\n", (void *)rn);

	return rn;
}

static struct radix_node *
at_matroute(char *key, struct radix_node_head *head)
{
	struct radix_node *rn;

	kprintf("at_matroute: v=%s\n", prsockaddr(key));
	kprintf("at_matroute: head=%p\n", (void *)head);

	rn = rn_match(key, head);

	kprintf("at_matroute: returnr rn=%p\n", (void *)rn);

	return rn;
}

static struct radix_node *
at_lookup(char *key, char *mask, struct radix_node_head *head)
{
	struct radix_node *rn;

	kprintf("at_lookup: v=%s\n", prsockaddr(key));
	kprintf("at_lookup: n=%s\n", prsockaddr(mask));
	kprintf("at_lookup: head=%p\n", (void *)head);

	rn = rn_lookup(key, mask, head);

	kprintf("at_lookup: returns rn=%p\n", (void *)rn);

	return rn;
}

static struct radix_node *  
at_delroute(char *key, char *netmask, struct radix_node_head *head)
{
	struct radix_node *rn;

	kprintf("at_delroute: v=%s\n", prsockaddr(key));
	kprintf("at_delroute: n=%s\n", prsockaddr(netmask));
	kprintf("at_delroute: head=%p\n", (void *)head);

	rn = rn_delete(key, netmask, head);

	kprintf("at_delroute: returns rn=%p\n", (void *)rn);

	return rn;
}

/*
 * Initialize our routing tree with debugging hooks.
 */
int
at_inithead(void **head, int off)
{
	struct radix_node_head *rnh;

	if(!rn_inithead(head, rn_cpumaskhead(mycpuid), off))
		return 0;

	rnh = *head;
	rnh->rnh_addaddr = at_addroute;
	rnh->rnh_deladdr = at_delroute;
	rnh->rnh_matchaddr = at_matroute;
	rnh->rnh_lookup = at_lookup;
	return 1;
}
