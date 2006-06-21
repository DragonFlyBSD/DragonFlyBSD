/*-
 * Copyright 2004 Colin Percival.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Colin Percival.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $DragonFly: src/sys/i386/i386/Attic/est.c,v 1.1 2006/06/21 22:25:43 y0netan1 Exp $
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/proc.h>

typedef struct {
	int mhz;
	int mv;
} freq_info;

typedef struct {
	freq_info *freqtab;
	size_t tabsize;
	uint32_t VID;
} cpu_info;

/* Obtained from Linux patch by Jeremy Fitzhardinge */
#define mhzmv2msr(mhz, mv)	((((mhz) / 100) << 8) + (((mv) - 700) >> 4))
#define msr2mhz(msr)		((((msr) >> 8) & 0xff) * 100)
#define msr2mv(msr)		((((msr) & 0xff) << 4) + 700)

/* Obtained by observation of MSR contents */
#define ID(mhz_hi, mv_hi, mhz_lo, mv_lo)	\
	((mhzmv2msr(mhz_lo, mv_lo) << 16) + mhzmv2msr(mhz_hi, mv_hi))
#define CPUINFO(t, zhi, vhi, zlo, vlo)	\
	{ t, sizeof(t) / sizeof(t[0]), ID(zhi, vhi, zlo, vlo) }

/* Names and numbers from IA-32 System Programming Guide */
#define MSR_PERF_STATUS		0x198
#define MSR_PERF_CTL		0x199

/*
 * Data from
 * Intel Pentium M Processor Datasheet (Order Number 252612), Table 5
 */
static freq_info PM17_130[] = {
/* 130nm 1.70GHz Pentium M */
	{ 1700, 1484 },
	{ 1400, 1308 },
	{ 1200, 1228 },
	{ 1000, 1116 },
	{  800, 1004 },
	{  600,  956 },
};
static freq_info PM16_130[] = {
/* 130nm 1.60GHz Pentium M */
	{ 1600, 1484 },
	{ 1400, 1420 },
	{ 1200, 1276 },
	{ 1000, 1164 },
	{  800, 1036 },
	{  600,  956 },
};
static freq_info PM15_130[] = {
/* 130nm 1.50GHz Pentium M */
	{ 1500, 1484 },
	{ 1400, 1452 },
	{ 1200, 1356 },
	{ 1000, 1228 },
	{  800, 1116 },
	{  600,  956 },
};
static freq_info PM14_130[] = {
/* 130nm 1.40GHz Pentium M */
	{ 1400, 1484 },
	{ 1200, 1436 },
	{ 1000, 1308 },
	{  800, 1180 },
	{  600,  956 },
};
static freq_info PM13_130[] = {
/* 130nm 1.30GHz Pentium M */
	{ 1300, 1388 },
	{ 1200, 1356 },
	{ 1000, 1292 },
	{  800, 1260 },
	{  600,  956 },
};
static freq_info PM13_LV_130[] = {
/* 130nm 1.30GHz Low Voltage Pentium M */
	{ 1300, 1180 },
	{ 1200, 1164 },
	{ 1100, 1100 },
	{ 1000, 1020 },
	{  900, 1004 },
	{  800,  988 },
	{  600,  956 },
};
static freq_info PM12_LV_130[] = {
/* 130 nm 1.20GHz Low Voltage Pentium M */
	{ 1200, 1180 },
	{ 1100, 1164 },
	{ 1000, 1100 },
	{  900, 1020 },
	{  800, 1004 },
	{  600,  956 },
};
static freq_info PM11_LV_130[] = {
/* 130 nm 1.10GHz Low Voltage Pentium M */
	{ 1100, 1180 },
	{ 1000, 1164 },
	{  900, 1100 },
	{  800, 1020 },
	{  600,  956 },
};
static freq_info PM11_ULV_130[] = {
/* 130 nm 1.10GHz Ultra Low Voltage Pentium M */
	{ 1100, 1004 },
	{ 1000,  988 },
	{  900,  972 },
	{  800,  956 },
	{  600,  844 },
};
static freq_info PM10_ULV_130[] = {
/* 130 nm 1.00GHz Ultra Low Voltage Pentium M */
	{ 1000, 1004 },
	{  900,  988 },
	{  800,  972 },
	{  600,  844 },
};

/*
 * Data from
 * Intel Pentium M Processor on 90nm Process with 2-MB L2 Cache
 * Datasheet (Order Number 302189), Table 5
 */
static freq_info PM_755A_90[] = {
/* 90 nm 2.00GHz Pentium M, VID #A */
	{ 2000, 1340 },
	{ 1800, 1292 },
	{ 1600, 1244 },
	{ 1400, 1196 },
	{ 1200, 1148 },
	{ 1000, 1100 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_755B_90[] = {
/* 90 nm 2.00GHz Pentium M, VID #B */
	{ 2000, 1324 },
	{ 1800, 1276 },
	{ 1600, 1228 },
	{ 1400, 1180 },
	{ 1200, 1132 },
	{ 1000, 1084 },
	{  800, 1036 },
	{  600,  988 },
};
static freq_info PM_755C_90[] = {
/* 90 nm 2.00GHz Pentium M, VID #C */
	{ 2000, 1308 },
	{ 1800, 1276 },
	{ 1600, 1228 },
	{ 1400, 1180 },
	{ 1200, 1132 },
	{ 1000, 1084 },
	{  800, 1036 },
	{  600,  988 },
};
static freq_info PM_755D_90[] = {
/* 90 nm 2.00GHz Pentium M, VID #D */
	{ 2000, 1276 },
	{ 1800, 1244 },
	{ 1600, 1196 },
	{ 1400, 1164 },
	{ 1200, 1116 },
	{ 1000, 1084 },
	{  800, 1036 },
	{  600,  988 },
};
static freq_info PM_745A_90[] = {
/* 90 nm 1.80GHz Pentium M, VID #A */
	{ 1800, 1340 },
	{ 1600, 1292 },
	{ 1400, 1228 },
	{ 1200, 1164 },
	{ 1000, 1116 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_745B_90[] = {
/* 90 nm 1.80GHz Pentium M, VID #B */
	{ 1800, 1324 },
	{ 1600, 1276 },
	{ 1400, 1212 },
	{ 1200, 1164 },
	{ 1000, 1116 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_745C_90[] = {
/* 90 nm 1.80GHz Pentium M, VID #C */
	{ 1800, 1308 },
	{ 1600, 1260 },
	{ 1400, 1212 },
	{ 1200, 1148 },
	{ 1000, 1100 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_745D_90[] = {
/* 90 nm 1.80GHz Pentium M, VID #D */
	{ 1800, 1276 },
	{ 1600, 1228 },
	{ 1400, 1180 },
	{ 1200, 1132 },
	{ 1000, 1084 },
	{  800, 1036 },
	{  600,  988 },
};
static freq_info PM_735A_90[] = {
/* 90 nm 1.70GHz Pentium M, VID #A */
	{ 1700, 1340 },
	{ 1400, 1244 },
	{ 1200, 1180 },
	{ 1000, 1116 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_735B_90[] = {
/* 90 nm 1.70GHz Pentium M, VID #B */
	{ 1700, 1324 },
	{ 1400, 1244 },
	{ 1200, 1180 },
	{ 1000, 1116 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_735C_90[] = {
/* 90 nm 1.70GHz Pentium M, VID #C */
	{ 1700, 1308 },
	{ 1400, 1228 },
	{ 1200, 1164 },
	{ 1000, 1116 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_735D_90[] = {
/* 90 nm 1.70GHz Pentium M, VID #D */
	{ 1700, 1276 },
	{ 1400, 1212 },
	{ 1200, 1148 },
	{ 1000, 1100 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_725A_90[] = {
/* 90 nm 1.60GHz Pentium M, VID #A */
	{ 1600, 1340 },
	{ 1400, 1276 },
	{ 1200, 1212 },
	{ 1000, 1132 },
	{  800, 1068 },
	{  600,  988 },
};
static freq_info PM_725B_90[] = {
/* 90 nm 1.60GHz Pentium M, VID #B */
	{ 1600, 1324 },
	{ 1400, 1260 },
	{ 1200, 1196 },
	{ 1000, 1132 },
	{  800, 1068 },
	{  600,  988 },
};
static freq_info PM_725C_90[] = {
/* 90 nm 1.60GHz Pentium M, VID #C */
	{ 1600, 1308 },
	{ 1400, 1244 },
	{ 1200, 1180 },
	{ 1000, 1116 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_725D_90[] = {
/* 90 nm 1.60GHz Pentium M, VID #D */
	{ 1600, 1276 },
	{ 1400, 1228 },
	{ 1200, 1164 },
	{ 1000, 1116 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_715A_90[] = {
/* 90 nm 1.50GHz Pentium M, VID #A */
	{ 1500, 1340 },
	{ 1200, 1228 },
	{ 1000, 1148 },
	{  800, 1068 },
	{  600,  988 },
};
static freq_info PM_715B_90[] = {
/* 90 nm 1.50GHz Pentium M, VID #B */
	{ 1500, 1324 },
	{ 1200, 1212 },
	{ 1000, 1148 },
	{  800, 1068 },
	{  600,  988 },
};
static freq_info PM_715C_90[] = {
/* 90 nm 1.50GHz Pentium M, VID #C */
	{ 1500, 1308 },
	{ 1200, 1212 },
	{ 1000, 1132 },
	{  800, 1068 },
	{  600,  988 },
};
static freq_info PM_715D_90[] = {
/* 90 nm 1.50GHz Pentium M, VID #D */
	{ 1500, 1276 },
	{ 1200, 1180 },
	{ 1000, 1116 },
	{  800, 1052 },
	{  600,  988 },
};
static freq_info PM_710_90[] = {                                               
/* 90 nm 1.40GHz Pentium M */                                          
	{ 1400, 1340 },                                                         
	{ 1200, 1228 },                                                         
	{ 1000, 1148 },                                                         
	{  800, 1068 },                                                         
	{  600,  988 },                                                         
};
static freq_info PM_738_90[] = {
/* 90 nm 1.40GHz Low Voltage Pentium M */
	{ 1400, 1116 },
	{ 1300, 1116 },
	{ 1200, 1100 },
	{ 1100, 1068 },
	{ 1000, 1052 },
	{  900, 1036 },
	{  800, 1020 },
	{  600,  988 },
};
static freq_info PM_733_90[] = {
/* 90 nm 1.10GHz Ultra Low Voltage Pentium M */
	{ 1100,  940 },
	{ 1000,  924 },
	{  900,  892 },
	{  800,  876 },
	{  600,  812 },
};
static freq_info PM_723_90[] = {
/* 90 nm 1.00GHz Ultra Low Voltage Pentium M */
	{ 1000,  940 },
	{  900,  908 },
	{  800,  876 },
	{  600,  812 },
};

static cpu_info ESTprocs[] = {
	CPUINFO(PM17_130, 1700, 1484, 600, 956),
	CPUINFO(PM16_130, 1600, 1484, 600, 956),
	CPUINFO(PM15_130, 1500, 1484, 600, 956),
	CPUINFO(PM14_130, 1400, 1484, 600, 956),
	CPUINFO(PM13_130, 1300, 1388, 600, 956),
	CPUINFO(PM13_LV_130, 1300, 1180, 600, 956),
	CPUINFO(PM12_LV_130, 1200, 1180, 600, 956),
	CPUINFO(PM11_LV_130, 1100, 1180, 600, 956),
	CPUINFO(PM11_ULV_130, 1100, 1004, 600, 844),
	CPUINFO(PM10_ULV_130, 1000, 1004, 600, 844),
	CPUINFO(PM_755A_90, 2000, 1340, 600, 988),
	CPUINFO(PM_755B_90, 2000, 1324, 600, 988),
	CPUINFO(PM_755C_90, 2000, 1308, 600, 988),
	CPUINFO(PM_755D_90, 2000, 1276, 600, 988),
	CPUINFO(PM_745A_90, 1800, 1340, 600, 988),
	CPUINFO(PM_745B_90, 1800, 1324, 600, 988),
	CPUINFO(PM_745C_90, 1800, 1308, 600, 988),
	CPUINFO(PM_745D_90, 1800, 1276, 600, 988),
	CPUINFO(PM_735A_90, 1700, 1340, 600, 988),
	CPUINFO(PM_735B_90, 1700, 1324, 600, 988),
	CPUINFO(PM_735C_90, 1700, 1308, 600, 988),
	CPUINFO(PM_735D_90, 1700, 1276, 600, 988),
	CPUINFO(PM_725A_90, 1600, 1340, 600, 988),
	CPUINFO(PM_725B_90, 1600, 1324, 600, 988),
	CPUINFO(PM_725C_90, 1600, 1308, 600, 988),
	CPUINFO(PM_725D_90, 1600, 1276, 600, 988),
	CPUINFO(PM_715A_90, 1500, 1340, 600, 988),
	CPUINFO(PM_715B_90, 1500, 1324, 600, 988),
	CPUINFO(PM_715C_90, 1500, 1308, 600, 988),
	CPUINFO(PM_715D_90, 1500, 1276, 600, 988),
	CPUINFO(PM_710_90, 1400, 1340, 600, 988),
	CPUINFO(PM_738_90, 1400, 1116, 600, 988),
	CPUINFO(PM_733_90, 1100, 940, 600, 812),
	CPUINFO(PM_723_90, 1000, 940, 600, 812),
};

static cpu_info *freq_list = NULL;	/* NULL if EST is disabled */

static int
est_sysctl_mhz(SYSCTL_HANDLER_ARGS)
{
	uint64_t msr;
	int mhz, mv;
	int mhz_wanted;
	freq_info *f, *f_end;
	int err = 0;

	if (freq_list == NULL)
		return(EOPNOTSUPP);

	msr = rdmsr(MSR_PERF_STATUS);
	mhz = msr2mhz(msr);
	mv = msr2mv(msr);

	if (req->newptr) {
		err = SYSCTL_IN(req, &mhz_wanted, sizeof(int));
		if (err)
			return(err);
		if (mhz == mhz_wanted)
			return(0);
		f_end = freq_list->freqtab + freq_list->tabsize;
		for (f = freq_list->freqtab; f < f_end; f++) {
			if (f->mhz == mhz_wanted)
				break;
		}
		if (f->mhz == 0)
			return(EOPNOTSUPP);
		printf("Changing CPU frequency from %d MHz to %d MHz\n",
			mhz, mhz_wanted);
		msr = rdmsr(MSR_PERF_CTL);
		msr = (msr & ~(uint64_t)(0xffff)) | mhzmv2msr(f->mhz, f->mv);
		wrmsr(MSR_PERF_CTL, msr);
		/*
		 * Sleep for a short time, to let the cpu find
		 * its new frequency before we return to the user
		 */
		tsleep(&freq_list, 0, "EST", 1);
	} else
		err = SYSCTL_OUT(req, &mhz, sizeof(int));
	return(err);
}

SYSCTL_NODE(_machdep, OID_AUTO, est, CTLFLAG_RD, 0, "");
SYSCTL_NODE(_machdep_est, OID_AUTO, frequency, CTLFLAG_RD, 0,
	    "Enhanced SpeedStep driver parameters");
SYSCTL_PROC(_machdep_est_frequency, OID_AUTO, current,
	    CTLTYPE_INT | CTLFLAG_RD, 0, 0, &est_sysctl_mhz, "I",
	    "Current CPU frequency for Enhanced SpeedStep");
SYSCTL_PROC(_machdep_est_frequency, OID_AUTO, target,
	    CTLTYPE_INT | CTLFLAG_RW, 0, 0, &est_sysctl_mhz, "I",
	    "Target CPU frequency for Enhanced SpeedStep");

static char est_frequencies[80] = "";
SYSCTL_STRING(_machdep_est_frequency, OID_AUTO, available, CTLFLAG_RD,
	      est_frequencies, 0,
	      "CPU frequencies supported by Enhanced SpeedStep");

static int
findcpu(void)
{
	uint64_t	msr;
	uint32_t	VID;
	cpu_info	*cpinfo;
	size_t		N = sizeof(ESTprocs) / sizeof(ESTprocs[0]);

	msr = rdmsr(MSR_PERF_STATUS);
	VID = (msr >> 32);
	for (cpinfo = ESTprocs; cpinfo < ESTprocs + N; cpinfo++) {
		if (VID == cpinfo->VID)
			break;
	}
	if (cpinfo == ESTprocs + N)
		return(EOPNOTSUPP);
	freq_list = cpinfo;
	return(0);
}

static int
est_loader(struct module *m __unused, int what, void *arg __unused)
{
	char		hwmodel[128];
	int		mib[] = { CTL_HW, HW_MODEL };
	size_t		modellen;
	size_t		freqlen, l;
	uint64_t	msr;
	int		mhz, mv;
	freq_info	*fq;
	int		err = 0;

	switch (what) {
	case MOD_LOAD:
		modellen = sizeof(hwmodel);
		err = kernel_sysctl(mib, 2, hwmodel, &modellen, NULL, 0, NULL);
		if (err) {
			printf("kernel_sysctl hw.model failed\n");
			return(err);
		}
		err = EOPNOTSUPP;
		if (ncpus != 1) {
			printf("Enhanced SpeedStep not supported"
			       " with more than one processor\n");
			break;
		}
		if (strncmp(hwmodel, "Intel(R) Pentium(R) M processor", 31) ||
		    (findcpu() != 0)) {
			printf("%s: Enhanced Speedstep not supported"
			       " on this processor\n", hwmodel);
			break;
		}

		freqlen = freq_list->tabsize * (sizeof(" 9999") - 1) + 1;
		if (sizeof(est_frequencies) <= freqlen) {
			printf("please enlarge est_frequencies[]\n");
			err = ENOMEM;
			break;
		}
		l = 0;
		for (fq = freq_list->freqtab + freq_list->tabsize;
		     --fq >= freq_list->freqtab;) {
			l += snprintf(est_frequencies + l, freqlen - l, "%s%d",
			    l > 0 ? " " : "", fq->mhz);
		}

		msr = rdmsr(MSR_PERF_STATUS);
		mhz = msr2mhz(msr);
		mv = msr2mv(msr);
		printf("%s: Enhanced SpeedStep running at %d MHz (%d mV)\n",
		       hwmodel, mhz, mv);
		err = 0;
		break;
	case MOD_UNLOAD:
		break;
	default:
		err = EINVAL;
		break;
	}
	return(err);
}

static moduledata_t est_mod = {
	"est",
	est_loader,
	NULL
};

DECLARE_MODULE(est, est_mod, SI_SUB_KLD, SI_ORDER_ANY);
