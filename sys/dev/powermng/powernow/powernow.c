/*
 * Copyright (c) 2004 Martin V\xe9giard. Copyright (c) 2004-2005 Bruno Ducrot
 * Copyright (c) 2004 FUKUDA Nobuhiko <nfukuda@spa.is.uec.ac.jp> Copyright
 * (c) 2004, 2006 The NetBSD Foundation, Inc. All rights reserved.
 * 
 * This code is derived from software contributed to The NetBSD Foundation by
 * Juan Romero Pardines and Martin Vegiard.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution. THIS SOFTWARE IS
 * PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/* AMD POWERNOW K8 driver */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <bus/isa/isa.h>
#include <machine/cpu.h>
#include <machine/pmap.h>
#include <machine/pc/bios.h>
#include <machine/cpufunc.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/vmparam.h>

#define PN8_STA_MFID(x)                 (((x) >> 16) & 0x3f)
#define PN8_STA_MVID(x)                 (((x) >> 48) & 0x1f)
#define PN8_STA_SFID(x)                 (((x) >> 8) & 0x3f)

/*
 * MSRs and bits used by PowerNow! technology
 */
#define MSR_AMDK7_FIDVID_CTL            0xc0010041
#define MSR_AMDK7_FIDVID_STATUS         0xc0010042
#define AMD_PN_FID_VID                  0x06

#define BIOS_START                      0xe0000
#define BIOS_LEN                        0x20000
#define BIOS_STEP                       16

#define PN8_PSB_VERSION                 0x14
#define PN8_PSB_TO_RVO(x)               ((x) & 0x03)
#define PN8_PSB_TO_IRT(x)               (((x) >> 2) & 0x03)
#define PN8_PSB_TO_MVS(x)               (((x) >> 4) & 0x03)
#define PN8_PSB_TO_BATT(x)              (((x) >> 6) & 0x03)
/* Bitfields used by K8 */
#define PN8_CTR_FID(x)                  ((x) & 0x3f)
#define PN8_CTR_VID(x)                  (((x) & 0x1f) << 8)
#define PN8_CTR_PENDING(x)              (((x) & 1) << 32)
#define PN8_STA_CFID(x)                 ((x) & 0x3f)
#define PN8_STA_SFID(x)                 (((x) >> 8) & 0x3f)
#define PN8_STA_MFID(x)                 (((x) >> 16) & 0x3f)
#define PN8_STA_PENDING(x)              (((x) >> 31) & 0x01)
#define PN8_STA_CVID(x)                 (((x) >> 32) & 0x1f)
#define PN8_STA_SVID(x)                 (((x) >> 40) & 0x1f)
#define PN8_STA_MVID(x)                 (((x) >> 48) & 0x1f)
#define PN8_PLL_LOCK(x)                 ((x) * 1000/5)
#define WRITE_FIDVID(fid, vid, ctrl)    \
        wrmsr(MSR_AMDK7_FIDVID_CTL,     \
            (((ctrl) << 32) | (1ULL << 16) | ((vid) << 8) | (fid)))
#define COUNT_OFF_IRT(irt)              DELAY(10 * (1 << (irt)))
#define COUNT_OFF_VST(vst)              DELAY(20 * (vst))
#define FID_TO_VCO_FID(fid)             \
        (((fid) < 8) ? (8 + ((fid) << 1)) : (fid))

#define READ_PENDING_WAIT(status)                               \
        do {                                                    \
                (status) = rdmsr(MSR_AMDK7_FIDVID_STATUS);      \
        } while (PN8_STA_PENDING(status))
#define abs(x) ( x < 0 ? -x : x )

#define POWERNOW_MAX_STATES             16

struct k8pnow_state {
	int		freq;
	uint8_t		fid;
	uint8_t		vid;
};

struct k8pnow_cpu_state {
	struct k8pnow_state state_table[POWERNOW_MAX_STATES];
	unsigned int	n_states;
	unsigned int	vst;
	unsigned int	mvs;
	unsigned int	pll;
	unsigned int	rvo;
	unsigned int	irt;
	int		low;
};

struct psb_s {
	char		signature [10];	/* AMDK7PNOW! */
	uint8_t		version;
	uint8_t		flags;
	uint16_t	ttime;	/* Min Settling time */
	uint8_t		reserved;
	uint8_t		n_pst;
};
struct pst_s {
	uint32_t	cpuid;
	uint8_t		pll;
	uint8_t		fid;
	uint8_t		vid;
	uint8_t		n_states;
};

static struct k8pnow_cpu_state *k8pnow_current_state = NULL;
int		cpuspeed;

int
k8pnow_states(struct k8pnow_cpu_state *cstate, uint32_t cpusig,
	      unsigned int fid, unsigned int vid);
int
		k8pnow_decode_pst(struct k8pnow_cpu_state *cstate, uint8_t * p);

/*
 * Given a set of pair of fid/vid, and number of performance states, compute
 * state_table via an insertion sort.
 */
int
k8pnow_decode_pst(struct k8pnow_cpu_state *cstate, uint8_t * p)
{
	int		i         , j, n;
	struct k8pnow_state state;
	for (n = 0, i = 0; i < cstate->n_states; i++) {
		state.fid = *p++;
		state.vid = *p++;

		/*
		 * The minimum supported frequency per the data sheet is
		 * 800MHz The maximum supported frequency is 5000MHz.
		 */
		state.freq = 800 + state.fid * 100;
		j = n;
		while (j > 0 && cstate->state_table[j - 1].freq > state.freq) {
			memcpy(&cstate->state_table[j],
			       &cstate->state_table[j - 1],
			       sizeof(struct k8pnow_state));
			--j;
		}
		memcpy(&cstate->state_table[j], &state,
		       sizeof(struct k8pnow_state));
		n++;
	}
	return 1;
}

int
k8pnow_states(struct k8pnow_cpu_state *cstate, uint32_t cpusig,
	      unsigned int fid, unsigned int vid)
{
	struct psb_s   *psb;
	struct pst_s   *pst;
	uint8_t        *p;
	int		i;
	for (p = (u_int8_t *) BIOS_PADDRTOVADDR(BIOS_START);
	     p < (u_int8_t *) BIOS_PADDRTOVADDR(BIOS_START + BIOS_LEN); p +=
	     BIOS_STEP) {
		if (memcmp(p, "AMDK7PNOW!", 10) == 0) {
			psb = (struct psb_s *)p;
			if (psb->version != PN8_PSB_VERSION)
				return 0;
			cstate->vst = psb->ttime;
			cstate->rvo = PN8_PSB_TO_RVO(psb->reserved);
			cstate->irt = PN8_PSB_TO_IRT(psb->reserved);
			cstate->mvs = PN8_PSB_TO_MVS(psb->reserved);
			cstate->low = PN8_PSB_TO_BATT(psb->reserved);
			p += sizeof(struct psb_s);
			for (i = 0; i < psb->n_pst; ++i) {
				pst = (struct pst_s *)p;
				cstate->pll = pst->pll;
				cstate->n_states = pst->n_states;
				if (cpusig == pst->cpuid &&
				    pst->fid == fid && pst->vid == vid) {
					return (k8pnow_decode_pst(cstate,
						p += sizeof(struct pst_s)));
				}
				p += sizeof(struct pst_s) + 2
					* cstate->n_states;
			}
		}
	}
	return 0;
}

static int
k8_get_curfreq(void)
{
	unsigned int	i;
	uint64_t	status;
	int		cfid      , cvid, fid = 0, vid = 0;
	struct k8pnow_cpu_state *cstate;
	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	if (PN8_STA_PENDING(status))
		return 1;
	cfid = PN8_STA_CFID(status);
	cvid = PN8_STA_CVID(status);
	cstate = k8pnow_current_state;
	for (i = 0; i < cstate->n_states; i++) {
		if (cstate->state_table[i].fid == cfid &&
		    cstate->state_table[i].vid == cvid) {
			fid = cstate->state_table[i].fid;
			vid = cstate->state_table[i].vid;
			return (cstate->state_table[i].freq);
		}
	}
	/* Not reached */
	return -1;
}

static int
k8_powernow_setperf(unsigned int freq)
{
	unsigned int	i;
	uint64_t	status;
	uint32_t	val;
	int		cfid      , cvid, fid = 0, vid = 0;
	int		rvo;
	struct k8pnow_cpu_state *cstate;
	/*
	 * We dont do a k8pnow_read_pending_wait here, need to ensure that
	 * the change pending bit isn't stuck,
	 */
	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	if (PN8_STA_PENDING(status))
		return 1;
	cfid = PN8_STA_CFID(status);
	cvid = PN8_STA_CVID(status);
	cstate = k8pnow_current_state;
	for (i = 0; i < cstate->n_states; i++) {
		if (cstate->state_table[i].freq >= freq) {
			fid = cstate->state_table[i].fid;
			vid = cstate->state_table[i].vid;
			break;
		}
	}
	if (fid == cfid && vid == cvid) {
		cpuspeed = freq;
		return 0;
	}
	/*
	 * Phase 1: Raise core voltage to requested VID if frequency is going
	 * up.
	 */
	while (cvid > vid) {
		val = cvid - (1 << cstate->mvs);
		WRITE_FIDVID(cfid, (val > 0) ? val : 0, 1ULL);
		READ_PENDING_WAIT(status);
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(cstate->vst);
	}

	/* ... then raise to voltage + RVO (if required) */
	for (rvo = cstate->rvo; rvo > 0 && cvid > 0; --rvo) {
		/*
		 * XXX It's not clear from spec if we have to do that in 0.25
		 * step or in MVS.  Therefore do it as it's done under Linux
		 */
		WRITE_FIDVID(cfid, cvid - 1, 1ULL);
		READ_PENDING_WAIT(status);
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(cstate->vst);
	}
	/* Phase 2: change to requested core frequency */
	if (cfid != fid) {
		uint32_t	vco_fid, vco_cfid;
		vco_fid = FID_TO_VCO_FID(fid);
		vco_cfid = FID_TO_VCO_FID(cfid);
		while (abs(vco_fid - vco_cfid) > 2) {
			if (fid > cfid) {
				if (cfid > 6)
					val = cfid + 2;
				else
					val = FID_TO_VCO_FID(cfid) + 2;
			} else
				val = cfid - 2;
			WRITE_FIDVID(val, cvid, (uint64_t) cstate->pll * 1000 / 5);
			READ_PENDING_WAIT(status);
			cfid = PN8_STA_CFID(status);
			COUNT_OFF_IRT(cstate->irt);
			vco_cfid = FID_TO_VCO_FID(cfid);
		}
		WRITE_FIDVID(fid, cvid, (uint64_t) cstate->pll * 1000 / 5);
		READ_PENDING_WAIT(status);
		cfid = PN8_STA_CFID(status);
		COUNT_OFF_IRT(cstate->irt);
	}
	/* Phase 3: change to requested voltage */
	if (cvid != vid) {
		WRITE_FIDVID(cfid, vid, 1ULL);
		READ_PENDING_WAIT(status);
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(cstate->vst);
	}
	if (cfid == fid || cvid == vid)
		cpuspeed = cstate->state_table[i].freq;
	return 0;
}

static int
powernow_sysctl_helper(SYSCTL_HANDLER_ARGS)
{
	int		fq        , err = 0;
	int		i;
	struct k8pnow_cpu_state *cstate;
	struct k8pnow_state *state;
	cstate = k8pnow_current_state;
	if (req->newptr != NULL) {
		err = SYSCTL_IN(req, &fq, sizeof(fq));
		if (err)
			return err;
		if (fq != cpuspeed) {
			for (i = cstate->n_states; i > 0; i--) {
				state = &cstate->state_table[i - 1];
				if (fq == state->freq) {
					k8_powernow_setperf(fq);
					break;
				}
			}
		}
	} else {
		err = SYSCTL_OUT(req, &cpuspeed, sizeof(cpuspeed));
	}
	return err;
}

static struct sysctl_ctx_list machdep_powernow_ctx;
static char	freqs_available[80];

static int
powernow_init(void)
{
	uint64_t	status;
	size_t		len    , freq_len;
	uint32_t	maxfid, maxvid, i;
	struct k8pnow_cpu_state *cstate;
	struct k8pnow_state *state;
	const char     *techname;
	u_int32_t	regs  [4];
	cpuspeed = 0;
	struct sysctl_oid *oid, *leaf;

	do_cpuid(0x80000000, regs);
	if (regs[0] < 0x80000007)
		return 1;
	do_cpuid(0x80000007, regs);
	if (!(regs[3] & AMD_PN_FID_VID))
		return 2;
	/* Extended CPUID signature value */
	do_cpuid(0x80000001, regs);
	cstate = kmalloc(sizeof(struct k8pnow_cpu_state), M_DEVBUF, M_WAITOK);
	cstate->n_states = 0;

	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	maxfid = PN8_STA_MFID(status);
	maxvid = PN8_STA_MVID(status);

	if (PN8_STA_SFID(status) != PN8_STA_MFID(status))
		techname = "PowerNow!";
	else
		techname = "Cool`n'Quiet";
	k8pnow_states(cstate, regs[0], maxfid, maxvid);
	len = 0;
	if (cstate->n_states) {
		freq_len = cstate->n_states * (sizeof("9999 ") - 1) + 1;
		kprintf("%s speeds:",
			techname);
		for (i = cstate->n_states; i > 0; i--) {
			state = &cstate->state_table[i - 1];
			kprintf(" %d", state->freq);
			len += ksnprintf(freqs_available + len, freq_len - len, "%d%s",
					 state->freq,
					 i > 1 ? " " : "");
		}
		kprintf(" MHz\n");
		k8pnow_current_state = cstate;
		k8_powernow_setperf(k8_get_curfreq());
	} else {
		kfree(cstate, M_DEVBUF);
		kprintf("powernow: no power states found\n");
		return 3;
	}

	/*
	 * Setup the sysctl sub-tree machdep.powernow.*
	 */
	oid = SYSCTL_ADD_NODE(&machdep_powernow_ctx,
		     SYSCTL_STATIC_CHILDREN(_machdep), OID_AUTO, "powernow",
			      CTLFLAG_RD, NULL, "");
	if (oid == NULL)
		return (EOPNOTSUPP);
	oid = SYSCTL_ADD_NODE(&machdep_powernow_ctx, SYSCTL_CHILDREN(oid),
			      OID_AUTO, "frequency", CTLFLAG_RD, NULL, "");
	if (oid == NULL)
		return (EOPNOTSUPP);
	leaf = SYSCTL_ADD_PROC(&machdep_powernow_ctx, SYSCTL_CHILDREN(oid),
		      OID_AUTO, "target", CTLTYPE_INT | CTLFLAG_RW, NULL, 0,
			       powernow_sysctl_helper, "I",
			       "Target CPU frequency for AMD PowerNow!");
	if (leaf == NULL)
		return (EOPNOTSUPP);
	leaf = SYSCTL_ADD_PROC(&machdep_powernow_ctx, SYSCTL_CHILDREN(oid),
		     OID_AUTO, "current", CTLTYPE_INT | CTLFLAG_RD, NULL, 0,
			       powernow_sysctl_helper, "I",
			       "Current CPU frequency for AMD PowerNow!");
	if (leaf == NULL)
		return (EOPNOTSUPP);
	leaf = SYSCTL_ADD_STRING(&machdep_powernow_ctx, SYSCTL_CHILDREN(oid),
			 OID_AUTO, "available", CTLFLAG_RD, freqs_available,
				 sizeof(freqs_available),
			      "CPU frequencies supported by AMD PowerNow!");
	if (leaf == NULL)
		return (EOPNOTSUPP);
	return (0);
}

static int
powernow_modevh(struct module *m, int what, void *arg __unused)
{
	int		error;

	switch (what) {
	case MOD_LOAD:
		error = sysctl_ctx_init(&machdep_powernow_ctx);
		if (error != 0)
			break;
		error = powernow_init();
		break;
	case MOD_UNLOAD:
		if (k8pnow_current_state)
			kfree(k8pnow_current_state, M_DEVBUF);
		error = sysctl_ctx_free(&machdep_powernow_ctx);
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}
static moduledata_t powernow_mod = {
	"powernow",
	powernow_modevh,
	NULL,
};

DECLARE_MODULE(powernow, powernow_mod, SI_BOOT2_KLD, SI_ORDER_ANY);
