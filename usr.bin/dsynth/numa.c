/*
 * Copyright (c) 2022 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * This code uses concepts and configuration based on 'synth', by
 * John R. Marino <draco@marino.st>, which was written in ada.
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
 */
/*
 * Put builders into (numa) cpu domains to improve cache interactions
 * (This is totally optional)
 */
#include "dsynth.h"

/*
 * Number of NUMA domains or integral multiple of same.  Recommend a
 * value of 0 (disabled), 2 or 4.  Set with Numa_setsize parameter.
 */
int NumaSetSize = 0;

/*
 * Set the domain for the given slot number.  -1 == reset to default.
 */
void
setNumaDomain(int slot)
{
	static int numa_initialized;
	static cpu_set_t defset;

	/*
	 * Auto-initialization (0) -> Enabled (1) or Disabled (-1)
	 */
	if (numa_initialized == 0) {
		if (NumaSetSize <= 1) {
			/*
			 * Disabled
			 */
			numa_initialized = -1;
		} else if (pthread_getaffinity_np(pthread_self(),
					          sizeof(defset), &defset) != 0)
	        {
			/*
			 * Missing pthreads support
			 */
			dlog(DLOG_ALL,
			     "Warning: pthread_*affinity*() "
			     "not supported\n");
			numa_initialized = -1;
		} else {
			/*
			 * Operational, default set saved
			 */
			numa_initialized = 1;
		}
	}

	/*
	 * Do nothing if disabled
	 */
	if (numa_initialized < 0)
		return;

	/*
	 * Set domain for requested slot number or -1 to return to default
	 */
	if (slot < 0) {
		/*
		 * Return to default
		 */
		pthread_setaffinity_np(pthread_self(), sizeof(defset), &defset);
	} else if (NumCores <= NumaSetSize * 2) {
		/*
		 * Not enough cores to partition
		 */
		/* do nothing */
	} else {
		/*
		 * Set cpumask to N-way partitioned mask of cpu threads,
		 * hopefully in the same NUMA domain.
		 *
		 * XXX hacked for the moment to assume a simple partitioning
		 * in linear blocks of cores, with the second half of the
		 * cpu space being the related sibling hyperthreads.
		 */
		cpu_set_t cpuset;
		int hcores = NumCores / 2;
		int count = (hcores + NumaSetSize - 1) / NumaSetSize;
		int i;

		slot = slot % NumaSetSize;
		slot *= count;
		slot %= hcores;
		CPU_ZERO(&cpuset);
		for (i = 0; i < count; ++i) {
			CPU_SET(slot + i, &cpuset);
			CPU_SET(slot + hcores + i, &cpuset);
		}
		pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
	}
}
