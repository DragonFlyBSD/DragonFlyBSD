/*	$KAME: altq_var.h,v 1.17 2004/04/20 05:09:08 kjc Exp $	*/
/*	$DragonFly: src/sys/net/altq/altq_var.h,v 1.1 2005/02/11 22:25:57 joerg Exp $ */

/*
 * Copyright (C) 1998-2003
 *	Sony Computer Science Laboratories Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY SONY CSL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL SONY CSL OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _ALTQ_ALTQ_VAR_H_
#define	_ALTQ_ALTQ_VAR_H_

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>

MALLOC_DECLARE(M_ALTQ);

/*
 * machine dependent clock
 * a 64bit high resolution time counter.
 */
extern int	machclk_usepcc;
extern uint32_t	machclk_freq;
extern uint32_t	machclk_per_tick;

void		init_machclk(void);
uint64_t	read_machclk(void);

#define	m_pktlen(m)		((m)->m_pkthdr.len)

extern int pfaltq_running;

struct ifnet;
struct mbuf;
struct pf_altq;

void	*altq_lookup(const char *, int);
uint8_t	read_dsfield(struct mbuf *, struct altq_pktattr *);
void	write_dsfield(struct mbuf *, struct altq_pktattr *, uint8_t);
int	tbr_set(struct ifaltq *, struct tb_profile *);
int	tbr_get(struct ifaltq *, struct tb_profile *);

int	altq_pfattach(struct pf_altq *);
int	altq_pfdetach(struct pf_altq *);
int	altq_add(struct pf_altq *);
int	altq_remove(struct pf_altq *);
int	altq_add_queue(struct pf_altq *);
int	altq_remove_queue(struct pf_altq *);
int	altq_getqstats(struct pf_altq *, void *, int *);

int	cbq_pfattach(struct pf_altq *);
int	cbq_add_altq(struct pf_altq *);
int	cbq_remove_altq(struct pf_altq *);
int	cbq_add_queue(struct pf_altq *);
int	cbq_remove_queue(struct pf_altq *);
int	cbq_getqstats(struct pf_altq *, void *, int *);

int	priq_pfattach(struct pf_altq *);
int	priq_add_altq(struct pf_altq *);
int	priq_remove_altq(struct pf_altq *);
int	priq_add_queue(struct pf_altq *);
int	priq_remove_queue(struct pf_altq *);
int	priq_getqstats(struct pf_altq *, void *, int *);

int	hfsc_pfattach(struct pf_altq *);
int	hfsc_add_altq(struct pf_altq *);
int	hfsc_remove_altq(struct pf_altq *);
int	hfsc_add_queue(struct pf_altq *);
int	hfsc_remove_queue(struct pf_altq *);
int	hfsc_getqstats(struct pf_altq *, void *, int *);

#endif /* _KERNEL */
#endif /* _ALTQ_ALTQ_VAR_H_ */
