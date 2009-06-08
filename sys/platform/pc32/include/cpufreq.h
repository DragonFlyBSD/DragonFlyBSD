#ifndef _MACHINE_CPUFREQ_H_
#define _MACHINE_CPUFREQ_H_

struct amd0f_fidvid {
	uint32_t	fid;
	uint32_t	vid;
};

struct amd0f_xsit {
	uint32_t	rvo;
	uint32_t	mvs;
	uint32_t	vst;
	uint32_t	pll_time;
	uint32_t	irt;
};

void	amd0f_fidvid_limit(struct amd0f_fidvid *, struct amd0f_fidvid *);
int	amd0f_set_fidvid(const struct amd0f_fidvid *,
	    const struct amd0f_xsit *);
int	amd0f_get_fidvid(struct amd0f_fidvid *);

#endif	/* !_MACHINE_CPUFREQ_H_ */
