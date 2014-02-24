/*
 * Copyright (c) 2004 Martin V\xe9giard.
 * Copyright (c) 2004-2005 Bruno Ducrot
 * Copyright (c) 2004 FUKUDA Nobuhiko <nfukuda@spa.is.uec.ac.jp>
 * Copyright (c) 2004, 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Juan Romero Pardines and Martin Vegiard.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/cpufunc.h>
#include <machine/cpufreq.h>

#define AMD0F_MSR_FIDVID_CTL		0xc0010041
#define AMD0F_MSR_FIDVID_STATUS		0xc0010042

/* AMD0F_MSR_FIDVID_STATUS */
#define AMD0F_STA_CFID(x)		((x) & 0x3f)
#define AMD0F_STA_SFID(x)		(((x) >> 8) & 0x3f)
#define AMD0F_STA_MFID(x)		(((x) >> 16) & 0x3f)
#define AMD0F_STA_PENDING(x)		(((x) >> 31) & 0x01)
#define AMD0F_STA_CVID(x)		(((x) >> 32) & 0x1f)
#define AMD0F_STA_SVID(x)		(((x) >> 40) & 0x1f)
#define AMD0F_STA_MVID(x)		(((x) >> 48) & 0x1f)

#define AMD0F_WRITE_FIDVID(fid, vid, ctrl) \
	wrmsr(AMD0F_MSR_FIDVID_CTL, \
	(((ctrl) << 32) | (1ULL << 16) | ((vid) << 8) | (fid)))

#define AMD0F_WAIT_FIDVID_CHG(status) \
do { \
	(status) = rdmsr(AMD0F_MSR_FIDVID_STATUS); \
} while (AMD0F_STA_PENDING(status))

#define AMD0F_FID2VCO(fid) \
	(((fid) < 8) ? (8 + ((fid) << 1)) : (fid))

#define AMD0F_DELAY_VST(vst)		DELAY(20 * (vst))
#define AMD0F_DELAY_IRT(irt)		DELAY(10 * (1 << (irt)))

/* XXX */
#define abs(x) ((x) < 0 ? -(x) : (x))

int
amd0f_set_fidvid(const struct amd0f_fidvid *fv, const struct amd0f_xsit *xsit)
{
	uint32_t val, cfid, cvid;
	int rvo;
	uint64_t status;

	/*
	 * We don't wait change pending bit here, need to ensure
	 * that it isn't stuck.
	 */
	status = rdmsr(AMD0F_MSR_FIDVID_STATUS);
	if (AMD0F_STA_PENDING(status))
		return EBUSY;

	cfid = AMD0F_STA_CFID(status);
	cvid = AMD0F_STA_CVID(status);
	if (fv->fid == cfid && fv->vid == cvid)
		return 0;

	/*
	 * Phase 1: Raise core voltage to requested VID if frequency is
	 * going up.
	 */
	if ((fv->fid & ~0x1) > (cfid & ~0x1) || cvid > fv->vid) {
		KKASSERT(fv->vid >= xsit->rvo);
	} else {
		KKASSERT(cvid >= xsit->rvo);
	}
	while (cvid > fv->vid) {
		if (cvid > (1 << xsit->mvs))
			val = cvid - (1 << xsit->mvs);
		else
			val = 0;
		AMD0F_WRITE_FIDVID(cfid, val, 0ULL);
		AMD0F_WAIT_FIDVID_CHG(status);
		cvid = AMD0F_STA_CVID(status);
		AMD0F_DELAY_VST(xsit->vst);
	}
	/* ... then raise to voltage + RVO (if required) */
	for (rvo = xsit->rvo; rvo > 0 && cvid > 0; --rvo) {
		/* XXX It's not clear from spec if we have to do that
		 * in 0.25 step or in MVS.  Therefore do it as it's done
		 * under Linux */
		AMD0F_WRITE_FIDVID(cfid, cvid - 1, 0ULL);
		AMD0F_WAIT_FIDVID_CHG(status);
		cvid = AMD0F_STA_CVID(status);
		AMD0F_DELAY_VST(xsit->vst);
	}

	/*
	 * Phase 2: Change to requested core frequency
	 */
	if (cfid != fv->fid) {
		/* NOTE: Keep type as int, else following 'abs' will break */
		int vco_fid, vco_cfid;

		vco_fid = AMD0F_FID2VCO(fv->fid);
		vco_cfid = AMD0F_FID2VCO(cfid);
		while (abs(vco_fid - vco_cfid) > 2) {
			if (fv->fid > cfid) {
				if (cfid > 6)
					val = cfid + 2;
				else
					val = AMD0F_FID2VCO(cfid) + 2;
			} else {
				KKASSERT(cfid >= 2);
				val = cfid - 2;
			}
			AMD0F_WRITE_FIDVID(val, cvid,
				(uint64_t)xsit->pll_time * 1000 / 5);
			AMD0F_WAIT_FIDVID_CHG(status);
			cfid = AMD0F_STA_CFID(status);
			AMD0F_DELAY_IRT(xsit->irt);
			vco_cfid = AMD0F_FID2VCO(cfid);
		}
		if (cfid != fv->fid) {
			AMD0F_WRITE_FIDVID(fv->fid, cvid,
				(uint64_t)xsit->pll_time * 1000 / 5);
			AMD0F_WAIT_FIDVID_CHG(status);
			cfid = AMD0F_STA_CFID(status);
			AMD0F_DELAY_IRT(xsit->irt);
		}
	}

	/*
	 * Phase 3: Change to requested voltage
	 */
	if (cvid != fv->vid) {
		AMD0F_WRITE_FIDVID(cfid, fv->vid, 0ULL);
		AMD0F_WAIT_FIDVID_CHG(status);
		cvid = AMD0F_STA_CVID(status);
		AMD0F_DELAY_VST(xsit->vst);
	}
	return 0;
}

int
amd0f_get_fidvid(struct amd0f_fidvid *fv)
{
	uint64_t status;

	status = rdmsr(AMD0F_MSR_FIDVID_STATUS);
	if (AMD0F_STA_PENDING(status))
		return EBUSY;

	fv->fid = AMD0F_STA_CFID(status);
	fv->vid = AMD0F_STA_CVID(status);
	return 0;
}

void
amd0f_fidvid_limit(struct amd0f_fidvid *fv_min, struct amd0f_fidvid *fv_max)
{
	uint32_t max_fid, max_vid, start_fid, start_vid;
	uint64_t status;

	status = rdmsr(AMD0F_MSR_FIDVID_STATUS);

	start_fid = AMD0F_STA_SFID(status);
	max_fid = AMD0F_STA_MFID(status);
	start_vid = AMD0F_STA_SVID(status);
	max_vid = AMD0F_STA_MVID(status);

	if (max_fid == 0x2a && max_vid != 0) {
		fv_max->fid = start_fid + 0xa;
		fv_max->vid = max_vid + 0x2;
		fv_min->fid = 0x2;
		fv_min->vid = start_vid;
	} else {
		fv_max->fid = max_fid;
		fv_max->vid = max_vid + 0x2;
		fv_min->fid = start_fid;
		fv_min->vid = start_vid;
	}
}
