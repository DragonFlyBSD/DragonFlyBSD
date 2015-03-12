/*
 * (MPSAFE)
 *
 * Copyright (c) 1994, David Greenman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/tty_subr.c,v 1.32 1999/08/28 00:46:21 peter Exp $
 */

/*
 * MPSAFE NOTE: 
 * Most functions here could use a separate lock to deal with concurrent
 * access to the cblocks and cblock_*_list.
 *
 * Right now the tty_token must be held for all this.
 */

/*
 * clist support routines
 *
 * NOTE on cblock->c_cf:	This pointer may point at the base of a cblock,
 *				which is &cblock->c_info[0], but will never
 *				point at the end of a cblock (char *)(cblk + 1)
 *				
 * NOTE on cblock->c_cl:	This pointer will never point at the base of
 *				a block but may point at the end of one.
 *
 * These routines may be used by more then just ttys, so a critical section
 * must be used to access the free list, and for general safety.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/clist.h>
#include <sys/thread2.h>

static void clist_init (void *);
SYSINIT(clist, SI_SUB_CLIST, SI_ORDER_FIRST, clist_init, NULL);

static struct cblock *cfreelist = NULL;
int cfreecount = 0;
static int cslushcount;
static int ctotcount;

#ifndef INITIAL_CBLOCKS
#define	INITIAL_CBLOCKS 50
#endif

static struct cblock *cblock_alloc (void);
static void cblock_alloc_cblocks (int number);
static void cblock_free (struct cblock *cblockp);
static void cblock_free_cblocks (int number);

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>

DB_SHOW_COMMAND(cbstat, cbstat)
{
	int cbsize = CBSIZE;

	kprintf(
	"tot = %d (active = %d, free = %d (reserved = %d, slush = %d))\n",
	       ctotcount * cbsize, ctotcount * cbsize - cfreecount, cfreecount,
	       cfreecount - cslushcount * cbsize, cslushcount * cbsize);
}
#endif /* DDB */

/*
 * Called from init_main.c
 */
/* ARGSUSED*/
static void
clist_init(void *dummy)
{
	/*
	 * Allocate an initial base set of cblocks as a 'slush'.
	 * We allocate non-slush cblocks with each initial ttyopen() and
	 * deallocate them with each ttyclose().
	 * We should adjust the slush allocation.  This can't be done in
	 * the i/o routines because they are sometimes called from
	 * interrupt handlers when it may be unsafe to call kmalloc().
	 */
	lwkt_gettoken(&tty_token);
	cblock_alloc_cblocks(cslushcount = INITIAL_CBLOCKS);
	lwkt_reltoken(&tty_token);
	KKASSERT(sizeof(struct cblock) == CBLOCK);
}

/*
 * Remove a cblock from the cfreelist queue and return a pointer
 * to it.
 *
 * May not block.
 *
 * NOTE: Must be called with tty_token held
 */
static struct cblock *
cblock_alloc(void)
{
	struct cblock *cblockp;

	ASSERT_LWKT_TOKEN_HELD(&tty_token);

	cblockp = cfreelist;
	if (cblockp == NULL)
		panic("clist reservation botch");
	KKASSERT(cblockp->c_head.ch_magic == CLIST_MAGIC_FREE);
	cfreelist = cblockp->c_head.ch_next;
	cblockp->c_head.ch_next = NULL;
	cblockp->c_head.ch_magic = CLIST_MAGIC_USED;
	cfreecount -= CBSIZE;
	return (cblockp);
}

/*
 * Add a cblock to the cfreelist queue.
 *
 * May not block, must be called in a critical section
 *
 * NOTE: Must be called with tty_token held
 */
static void
cblock_free(struct cblock *cblockp)
{
	ASSERT_LWKT_TOKEN_HELD(&tty_token);

	if (isset(cblockp->c_quote, CBQSIZE * NBBY - 1))
		bzero(cblockp->c_quote, sizeof cblockp->c_quote);
	KKASSERT(cblockp->c_head.ch_magic == CLIST_MAGIC_USED);
	cblockp->c_head.ch_next = cfreelist;
	cblockp->c_head.ch_magic = CLIST_MAGIC_FREE;
	cfreelist = cblockp;
	cfreecount += CBSIZE;
}

/*
 * Allocate some cblocks for the cfreelist queue.
 *
 * This routine may block, but still must be called in a critical section
 *
 * NOTE: Must be called with tty_token held
 */
static void
cblock_alloc_cblocks(int number)
{
	int i;
	struct cblock *cbp;

	ASSERT_LWKT_TOKEN_HELD(&tty_token);

	for (i = 0; i < number; ++i) {
		cbp = kmalloc(sizeof *cbp, M_TTYS, M_NOWAIT);
		if (cbp == NULL) {
			kprintf(
"clist_alloc_cblocks: M_NOWAIT kmalloc failed, trying M_WAITOK\n");
			cbp = kmalloc(sizeof *cbp, M_TTYS, M_WAITOK);
		}
		KKASSERT(((intptr_t)cbp & CROUND) == 0);
		/*
		 * Freed cblocks have zero quotes and garbage elsewhere.
		 * Set the may-have-quote bit to force zeroing the quotes.
		 */
		setbit(cbp->c_quote, CBQSIZE * NBBY - 1);
		cbp->c_head.ch_magic = CLIST_MAGIC_USED;
		cblock_free(cbp);
	}
	ctotcount += number;
}

/*
 * Set the cblock allocation policy for a clist.
 */
void
clist_alloc_cblocks(struct clist *clistp, int ccmax, int ccreserved)
{
	int dcbr;

	/*
	 * Allow for wasted space at the head.
	 */
	if (ccmax != 0)
		ccmax += CBSIZE - 1;
	if (ccreserved != 0)
		ccreserved += CBSIZE - 1;

	crit_enter();
	lwkt_gettoken(&tty_token);
	clistp->c_cbmax = roundup(ccmax, CBSIZE) / CBSIZE;
	dcbr = roundup(ccreserved, CBSIZE) / CBSIZE - clistp->c_cbreserved;
	if (dcbr >= 0) {
		clistp->c_cbreserved += dcbr;	/* atomic w/c_cbmax */
		cblock_alloc_cblocks(dcbr);	/* may block */
	} else {
		KKASSERT(clistp->c_cbcount <= clistp->c_cbreserved);
		if (clistp->c_cbreserved + dcbr < clistp->c_cbcount)
			dcbr = clistp->c_cbcount - clistp->c_cbreserved;
		clistp->c_cbreserved += dcbr;	/* atomic w/c_cbmax */
		cblock_free_cblocks(-dcbr);	/* may block */
	}
	KKASSERT(clistp->c_cbreserved >= 0);
	lwkt_reltoken(&tty_token);
	crit_exit();
}

/*
 * Free some cblocks from the cfreelist queue back to the
 * system malloc pool.
 *
 * Must be called from within a critical section.  May block.
 */
static void
cblock_free_cblocks(int number)
{
	int i;

	lwkt_gettoken(&tty_token);
	for (i = 0; i < number; ++i)
		kfree(cblock_alloc(), M_TTYS);
	ctotcount -= number;
	lwkt_reltoken(&tty_token);
}

/*
 * Free the cblocks reserved for a clist.
 */
void
clist_free_cblocks(struct clist *clistp)
{
	int cbreserved;

	crit_enter();
	lwkt_gettoken(&tty_token);
	if (clistp->c_cbcount != 0)
		panic("freeing active clist cblocks");
	cbreserved = clistp->c_cbreserved;
	clistp->c_cbmax = 0;
	clistp->c_cbreserved = 0;
	cblock_free_cblocks(cbreserved); /* may block */
	lwkt_reltoken(&tty_token);
	crit_exit();
}

/*
 * Get a character from the head of a clist.
 */
int
clist_getc(struct clist *clistp)
{
	int chr = -1;
	struct cblock *cblockp;

	crit_enter();
	lwkt_gettoken(&tty_token);
	if (clistp->c_cc) {
		KKASSERT(((intptr_t)clistp->c_cf & CROUND) != 0);
		cblockp = (struct cblock *)((intptr_t)clistp->c_cf & ~CROUND);
		chr = (u_char)*clistp->c_cf;

		/*
		 * If this char is quoted, set the flag.
		 */
		if (isset(cblockp->c_quote, clistp->c_cf - (char *)cblockp->c_info))
			chr |= TTY_QUOTE;

		/*
		 * Advance to next character.
		 */
		clistp->c_cf++;
		clistp->c_cc--;
		/*
		 * If we have advanced the 'first' character pointer
		 * past the end of this cblock, advance to the next one.
		 * If there are no more characters, set the first and
		 * last pointers to NULL. In either case, free the
		 * current cblock.
		 */
		KKASSERT(clistp->c_cf <= (char *)(cblockp + 1));
		if ((clistp->c_cf == (char *)(cblockp + 1)) ||
		    (clistp->c_cc == 0)) {
			if (clistp->c_cc > 0) {
				clistp->c_cf = cblockp->c_head.ch_next->c_info;
			} else {
				clistp->c_cf = clistp->c_cl = NULL;
			}
			cblock_free(cblockp);
			if (--clistp->c_cbcount >= clistp->c_cbreserved)
				++cslushcount;
		}
	}
	lwkt_reltoken(&tty_token);
	crit_exit();
	return (chr);
}

/*
 * Copy 'amount' of chars, beginning at head of clist 'clistp' to
 * destination linear buffer 'dest'. Return number of characters
 * actually copied.
 */
int
q_to_b(struct clist *clistp, char *dest, int amount)
{
	struct cblock *cblockp;
	struct cblock *cblockn;
	char *dest_orig = dest;
	int numc;

	crit_enter();
	lwkt_gettoken(&tty_token);
	while (clistp && amount && (clistp->c_cc > 0)) {
		KKASSERT(((intptr_t)clistp->c_cf & CROUND) != 0);
		cblockp = (struct cblock *)((intptr_t)clistp->c_cf & ~CROUND);
		cblockn = cblockp + 1; /* pointer arithmetic! */
		numc = min(amount, (char *)cblockn - clistp->c_cf);
		numc = min(numc, clistp->c_cc);
		bcopy(clistp->c_cf, dest, numc);
		amount -= numc;
		clistp->c_cf += numc;
		clistp->c_cc -= numc;
		dest += numc;
		/*
		 * If this cblock has been emptied, advance to the next
		 * one. If there are no more characters, set the first
		 * and last pointer to NULL. In either case, free the
		 * current cblock.
		 */
		KKASSERT(clistp->c_cf <= (char *)cblockn);
		if ((clistp->c_cf == (char *)cblockn) || (clistp->c_cc == 0)) {
			if (clistp->c_cc > 0) {
				KKASSERT(cblockp->c_head.ch_next != NULL);
				clistp->c_cf = cblockp->c_head.ch_next->c_info;
			} else {
				clistp->c_cf = clistp->c_cl = NULL;
			}
			cblock_free(cblockp);
			if (--clistp->c_cbcount >= clistp->c_cbreserved)
				++cslushcount;
		}
	}
	lwkt_reltoken(&tty_token);
	crit_exit();
	return (dest - dest_orig);
}

/*
 * Flush 'amount' of chars, beginning at head of clist 'clistp'.
 */
void
ndflush(struct clist *clistp, int amount)
{
	struct cblock *cblockp;
	struct cblock *cblockn;
	int numc;

	crit_enter();
	lwkt_gettoken(&tty_token);
	while (amount && (clistp->c_cc > 0)) {
		KKASSERT(((intptr_t)clistp->c_cf & CROUND) != 0);
		cblockp = (struct cblock *)((intptr_t)clistp->c_cf & ~CROUND);
		cblockn = cblockp + 1; /* pointer arithmetic! */
		numc = min(amount, (char *)cblockn - clistp->c_cf);
		numc = min(numc, clistp->c_cc);
		amount -= numc;
		clistp->c_cf += numc;
		clistp->c_cc -= numc;
		/*
		 * If this cblock has been emptied, advance to the next
		 * one. If there are no more characters, set the first
		 * and last pointer to NULL. In either case, free the
		 * current cblock.
		 */
		KKASSERT(clistp->c_cf <= (char *)cblockn);
		if (clistp->c_cf == (char *)cblockn || clistp->c_cc == 0) {
			if (clistp->c_cc > 0) {
				KKASSERT(cblockp->c_head.ch_next != NULL);
				clistp->c_cf = cblockp->c_head.ch_next->c_info;
			} else {
				clistp->c_cf = clistp->c_cl = NULL;
			}
			cblock_free(cblockp);
			if (--clistp->c_cbcount >= clistp->c_cbreserved)
				++cslushcount;
		}
	}
	lwkt_reltoken(&tty_token);
	crit_exit();
}

/*
 * Add a character to the end of a clist. Return -1 is no
 * more clists, or 0 for success.
 */
int
clist_putc(int chr, struct clist *clistp)
{
	struct cblock *cblockp;

	crit_enter();
	lwkt_gettoken(&tty_token);

	/*
	 * Note: this section may point c_cl at the base of a cblock.  This
	 * is a temporary violation of the requirements for c_cl, we
	 * increment it before returning.
	 */
	if (clistp->c_cl == NULL) {
		if (clistp->c_cbreserved < 1) {
			lwkt_reltoken(&tty_token);
			crit_exit();
			return (-1);		/* nothing done */
		}
		cblockp = cblock_alloc();
		clistp->c_cbcount = 1;
		clistp->c_cf = clistp->c_cl = cblockp->c_info;
		clistp->c_cc = 0;
	} else {
		cblockp = (struct cblock *)((intptr_t)clistp->c_cl & ~CROUND);
		if (((intptr_t)clistp->c_cl & CROUND) == 0) {
			struct cblock *prev = (cblockp - 1);

			if (clistp->c_cbcount >= clistp->c_cbreserved) {
				if (clistp->c_cbcount >= clistp->c_cbmax
				    || cslushcount <= 0) {
					lwkt_reltoken(&tty_token);
					crit_exit();
					return (-1);
				}
				--cslushcount;
			}
			cblockp = cblock_alloc();
			clistp->c_cbcount++;
			prev->c_head.ch_next = cblockp;
			clistp->c_cl = cblockp->c_info;
		}
	}

	/*
	 * If this character is quoted, set the quote bit, if not, clear it.
	 */
	if (chr & TTY_QUOTE) {
		setbit(cblockp->c_quote, clistp->c_cl - (char *)cblockp->c_info);
		/*
		 * Use one of the spare quote bits to record that something
		 * may be quoted.
		 */
		setbit(cblockp->c_quote, CBQSIZE * NBBY - 1);
	} else {
		clrbit(cblockp->c_quote, clistp->c_cl - (char *)cblockp->c_info);
	}

	*clistp->c_cl++ = chr;
	clistp->c_cc++;

	lwkt_reltoken(&tty_token);
	crit_exit();
	return (0);
}

/*
 * Copy data from linear buffer to clist chain. Return the
 * number of characters not copied.
 */
int
b_to_q(char *src, int amount, struct clist *clistp)
{
	struct cblock *cblockp;
	char *firstbyte, *lastbyte;
	u_char startmask, endmask;
	int startbit, endbit, num_between, numc;

	/*
	 * Avoid allocating an initial cblock and then not using it.
	 * c_cc == 0 must imply c_cbount == 0.
	 */
	if (amount <= 0)
		return (amount);

	crit_enter();
	lwkt_gettoken(&tty_token);

	/*
	 * Note: this section may point c_cl at the base of a cblock.  This
	 * is a temporary violation of the requirements for c_cl.  Since
	 * amount is non-zero we will not return with it in that state.
	 */
	if (clistp->c_cl == NULL) {
		if (clistp->c_cbreserved < 1) {
			lwkt_reltoken(&tty_token);
			crit_exit();
			kprintf("b_to_q to a clist with no reserved cblocks.\n");
			return (amount);	/* nothing done */
		}
		cblockp = cblock_alloc();
		clistp->c_cbcount = 1;
		clistp->c_cf = clistp->c_cl = cblockp->c_info;
		clistp->c_cc = 0;
	} else {
		/*
		 * c_cl may legally point past the end of the block, which
		 * falls through to the 'get another cblock' code below.
		 */
		cblockp = (struct cblock *)((intptr_t)clistp->c_cl & ~CROUND);
	}

	while (amount) {
		/*
		 * Get another cblock if needed.
		 */
		if (((intptr_t)clistp->c_cl & CROUND) == 0) {
			struct cblock *prev = cblockp - 1;

			if (clistp->c_cbcount >= clistp->c_cbreserved) {
				if (clistp->c_cbcount >= clistp->c_cbmax
				    || cslushcount <= 0) {
					lwkt_reltoken(&tty_token);
					crit_exit();
					return (amount);
				}
				--cslushcount;
			}
			cblockp = cblock_alloc();
			clistp->c_cbcount++;
			prev->c_head.ch_next = cblockp;
			clistp->c_cl = cblockp->c_info;
		}

		/*
		 * Copy a chunk of the linear buffer up to the end
		 * of this cblock.
		 */
		numc = min(amount, (char *)(cblockp + 1) - clistp->c_cl);
		bcopy(src, clistp->c_cl, numc);

		/*
		 * Clear quote bits if they aren't known to be clear.
		 * The following could probably be made into a separate
		 * "bitzero()" routine, but why bother?
		 */
		if (isset(cblockp->c_quote, CBQSIZE * NBBY - 1)) {
			startbit = clistp->c_cl - (char *)cblockp->c_info;
			endbit = startbit + numc - 1;

			firstbyte = (u_char *)cblockp->c_quote + (startbit / NBBY);
			lastbyte = (u_char *)cblockp->c_quote + (endbit / NBBY);

			/*
			 * Calculate mask of bits to preserve in first and
			 * last bytes.
			 */
			startmask = NBBY - (startbit % NBBY);
			startmask = 0xff >> startmask;
			endmask = (endbit % NBBY);
			endmask = 0xff << (endmask + 1);

			if (firstbyte != lastbyte) {
				*firstbyte &= startmask;
				*lastbyte &= endmask;

				num_between = lastbyte - firstbyte - 1;
				if (num_between)
					bzero(firstbyte + 1, num_between);
			} else {
				*firstbyte &= (startmask | endmask);
			}
		}

		/*
		 * ...and update pointer for the next chunk.
		 */
		src += numc;
		clistp->c_cl += numc;
		clistp->c_cc += numc;
		amount -= numc;
		/*
		 * If we go through the loop again, it's always
		 * for data in the next cblock, so by adding one (cblock),
		 * (which makes the pointer 1 beyond the end of this
		 * cblock) we prepare for the assignment of 'prev'
		 * above.
		 */
		++cblockp;
	}
	lwkt_reltoken(&tty_token);
	crit_exit();
	return (amount);
}

/*
 * Get the next character in the clist. Store it at dst. Don't
 * advance any clist pointers, but return a pointer to the next
 * character position.
 *
 * Must be called at spltty().  This routine may not run in a critical
 * section and so may not call the cblock allocator/deallocator.
 */
char *
nextc(struct clist *clistp, char *cp, int *dst)
{
	struct cblock *cblockp;

	++cp;
	/*
	 * See if the next character is beyond the end of
	 * the clist.
	 */
	lwkt_gettoken(&tty_token);
	if (clistp->c_cc && (cp != clistp->c_cl)) {
		/*
		 * If the next character is beyond the end of this
		 * cblock, advance to the next cblock.
		 */
		if (((intptr_t)cp & CROUND) == 0)
			cp = ((struct cblock *)cp - 1)->c_head.ch_next->c_info;
		cblockp = (struct cblock *)((intptr_t)cp & ~CROUND);

		/*
		 * Get the character. Set the quote flag if this character
		 * is quoted.
		 */
		*dst = (u_char)*cp | (isset(cblockp->c_quote, cp - (char *)cblockp->c_info) ? TTY_QUOTE : 0);

		lwkt_reltoken(&tty_token);
		return (cp);
	}

	lwkt_reltoken(&tty_token);
	return (NULL);
}

/*
 * "Unput" a character from a clist.
 */
int
clist_unputc(struct clist *clistp)
{
	struct cblock *cblockp = NULL, *cbp = NULL;
	int chr = -1;

	crit_enter();
	lwkt_gettoken(&tty_token);

	if (clistp->c_cc) {
		/*
		 * note that clistp->c_cl will never point at the base
		 * of a cblock (cblock->c_info) (see assert this later on),
		 * but it may point past the end of one.  We temporarily
		 * violate this in the decrement below but then we fix it up.
		 */
		--clistp->c_cc;
		--clistp->c_cl;

		chr = (u_char)*clistp->c_cl;

		cblockp = (struct cblock *)((intptr_t)clistp->c_cl & ~CROUND);

		/*
		 * Set quote flag if this character was quoted.
		 */
		if (isset(cblockp->c_quote, (u_char *)clistp->c_cl - cblockp->c_info))
			chr |= TTY_QUOTE;

		/*
		 * If all of the characters have been unput in this
		 * cblock, then find the previous one and free this
		 * one.
		 *
		 * if c_cc is 0 clistp->c_cl may end up pointing at
		 * cblockp->c_info, which is illegal, but the case will be 
		 * taken care of near the end of the routine.  Otherwise
		 * there *MUST* be another cblock, find it.
		 */
		KKASSERT(clistp->c_cl >= (char *)cblockp->c_info);
		if (clistp->c_cc && (clistp->c_cl == (char *)cblockp->c_info)) {
			cbp = (struct cblock *)((intptr_t)clistp->c_cf & ~CROUND);

			while (cbp->c_head.ch_next != cblockp)
				cbp = cbp->c_head.ch_next;
			cbp->c_head.ch_next = NULL;

			/*
			 * When the previous cblock is at the end, the 'last'
			 * pointer always points (invalidly) one past.
			 */
			clistp->c_cl = (char *)(cbp + 1);
			cblock_free(cblockp);
			if (--clistp->c_cbcount >= clistp->c_cbreserved)
				++cslushcount;
		}
	}

	/*
	 * If there are no more characters on the list, then
	 * free the last cblock.   It should not be possible for c->cl
	 * to be pointing past the end of a block due to our decrement
	 * of it way above.
	 */
	if (clistp->c_cc == 0 && clistp->c_cl) {
		KKASSERT(((intptr_t)clistp->c_cl & CROUND) != 0);
		cblockp = (struct cblock *)((intptr_t)clistp->c_cl & ~CROUND);
		cblock_free(cblockp);
		if (--clistp->c_cbcount >= clistp->c_cbreserved)
			++cslushcount;
		clistp->c_cf = clistp->c_cl = NULL;
	}

	lwkt_reltoken(&tty_token);
	crit_exit();
	return (chr);
}

/*
 * Move characters in source clist to destination clist,
 * preserving quote bits.
 */
void
catq(struct clist *src_clistp, struct clist *dest_clistp)
{
	int chr;

	lwkt_gettoken(&tty_token);
	crit_enter();
	/*
	 * If the destination clist is empty (has no cblocks atttached),
	 * and there are no possible complications with the resource counters,
	 * then we simply assign the current clist to the destination.
	 */
	if (!dest_clistp->c_cf
	    && src_clistp->c_cbcount <= src_clistp->c_cbmax
	    && src_clistp->c_cbcount <= dest_clistp->c_cbmax) {
		dest_clistp->c_cf = src_clistp->c_cf;
		dest_clistp->c_cl = src_clistp->c_cl;
		src_clistp->c_cf = src_clistp->c_cl = NULL;

		dest_clistp->c_cc = src_clistp->c_cc;
		src_clistp->c_cc = 0;
		dest_clistp->c_cbcount = src_clistp->c_cbcount;
		src_clistp->c_cbcount = 0;

		crit_exit();
		lwkt_reltoken(&tty_token);
		return;
	}
	crit_exit();

	/*
	 * XXX  This should probably be optimized to more than one
	 * character at a time.
	 */
	while ((chr = clist_getc(src_clistp)) != -1)
		clist_putc(chr, dest_clistp);
	lwkt_reltoken(&tty_token);
}
