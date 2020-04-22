/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
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
#include "dsynth.h"

typedef struct job {
	pthread_t td;
	pthread_cond_t cond;
	bulk_t	*active;
	int terminate : 1;
} job_t;

/*
 * Most of these globals are locked with BulkMutex
 */
static int BulkScanJob;
static int BulkCurJobs;
static int BulkMaxJobs;
static job_t JobsAry[MAXBULK];
static void (*BulkFunc)(bulk_t *bulk);
static bulk_t *BulkSubmit;
static bulk_t **BulkSubmitTail = &BulkSubmit;
static bulk_t *BulkResponse;
static bulk_t **BulkResponseTail = &BulkResponse;
static pthread_cond_t BulkResponseCond;
static pthread_mutex_t BulkMutex;

static void bulkstart(void);
#if 0
static int readall(int fd, void *buf, size_t bytes);
static int writeall(int fd, const void *buf, size_t bytes);
#endif
static void *bulkthread(void *arg);

/*
 * Initialize for bulk scan operations.  Always paired with donebulk()
 */
void
initbulk(void (*func)(bulk_t *bulk), int jobs)
{
	int i;

	if (jobs > MAXBULK)
		jobs = MAXBULK;

	ddassert(BulkSubmit == NULL);
	BulkCurJobs = 0;
	BulkMaxJobs = jobs;
	BulkFunc = func;
	BulkScanJob = 0;

	addbuildenv("__MAKE_CONF", "/dev/null",
		    BENV_ENVIRONMENT | BENV_PKGLIST);

	/*
	 * CCache is a horrible unreliable hack but... leave the
	 * mechanism in-place in case someone has a death wish.
	 */
	if (UseCCache) {
		addbuildenv("WITH_CCACHE_BUILD", "yes", BENV_MAKECONF);
		addbuildenv("CCACHE_DIR", CCachePath, BENV_MAKECONF);
	}

	pthread_mutex_init(&BulkMutex, NULL);
	pthread_cond_init(&BulkResponseCond, NULL);

	pthread_mutex_lock(&BulkMutex);
	for (i = 0; i < jobs; ++i) {
		pthread_cond_init(&JobsAry[i].cond, NULL);
		pthread_create(&JobsAry[i].td, NULL, bulkthread, &JobsAry[i]);
	}
	pthread_mutex_unlock(&BulkMutex);
}

void
donebulk(void)
{
	bulk_t *bulk;
	int i;

	pthread_mutex_lock(&BulkMutex);
	while ((bulk = BulkSubmit) != NULL) {
		BulkSubmit = bulk->next;
		freebulk(bulk);
	}
	BulkSubmitTail = &BulkSubmit;

	for (i = 0; i < BulkMaxJobs; ++i) {
		JobsAry[i].terminate = 1;
		pthread_cond_signal(&JobsAry[i].cond);
	}
	pthread_mutex_unlock(&BulkMutex);
	for (i = 0; i < BulkMaxJobs; ++i) {
		pthread_join(JobsAry[i].td, NULL);
		pthread_cond_destroy(&JobsAry[i].cond);
		if (JobsAry[i].active) {
			freebulk(JobsAry[i].active);
			JobsAry[i].active = NULL;
			pthread_mutex_lock(&BulkMutex);
			--BulkCurJobs;
			pthread_mutex_unlock(&BulkMutex);
		}
		JobsAry[i].terminate = 0;
	}
	ddassert(BulkCurJobs == 0);

	while ((bulk = BulkResponse) != NULL) {
		BulkResponse = bulk->next;
		freebulk(bulk);
	}
	BulkResponseTail = &BulkResponse;

	BulkFunc = NULL;

	bzero(JobsAry, sizeof(JobsAry));

	if (UseCCache) {
		delbuildenv("WITH_CCACHE_BUILD");
		delbuildenv("CCACHE_DIR");
	}
	delbuildenv("__MAKE_CONF");
}

void
queuebulk(const char *s1, const char *s2, const char *s3, const char *s4)
{
	bulk_t *bulk;

	bulk = calloc(1, sizeof(*bulk));
	if (s1)
		bulk->s1 = strdup(s1);
	if (s2)
		bulk->s2 = strdup(s2);
	if (s3)
		bulk->s3 = strdup(s3);
	if (s4)
		bulk->s4 = strdup(s4);
	bulk->state = ONSUBMIT;

	pthread_mutex_lock(&BulkMutex);
	*BulkSubmitTail = bulk;
	BulkSubmitTail = &bulk->next;
	if (BulkCurJobs < BulkMaxJobs) {
		pthread_mutex_unlock(&BulkMutex);
		bulkstart();
	} else {
		pthread_mutex_unlock(&BulkMutex);
	}
}

/*
 * Fill any idle job slots with new jobs as available.
 */
static
void
bulkstart(void)
{
	bulk_t *bulk;
	int i;

	pthread_mutex_lock(&BulkMutex);
	while ((bulk = BulkSubmit) != NULL && BulkCurJobs < BulkMaxJobs) {
		i = BulkScanJob + 1;
		for (;;) {
			i = i % BulkMaxJobs;
			if (JobsAry[i].active == NULL)
				break;
			++i;
		}
		BulkScanJob = i;
		BulkSubmit = bulk->next;
		if (BulkSubmit == NULL)
			BulkSubmitTail = &BulkSubmit;

		bulk->state = ONRUN;
		JobsAry[i].active = bulk;
		pthread_cond_signal(&JobsAry[i].cond);
		++BulkCurJobs;
	}
	pthread_mutex_unlock(&BulkMutex);
}

/*
 * Retrieve completed job or job with activity
 */
bulk_t *
getbulk(void)
{
	bulk_t *bulk;

	pthread_mutex_lock(&BulkMutex);
	while (BulkCurJobs && BulkResponse == NULL) {
		pthread_cond_wait(&BulkResponseCond, &BulkMutex);
	}
	if (BulkResponse) {
		bulk = BulkResponse;
		ddassert(bulk->state == ONRESPONSE);
		BulkResponse = bulk->next;
		if (BulkResponse == NULL)
			BulkResponseTail = &BulkResponse;
		bulk->state = UNLISTED;
	} else {
		bulk = NULL;
	}
	pthread_mutex_unlock(&BulkMutex);
	bulkstart();

	return bulk;
}

void
freebulk(bulk_t *bulk)
{
	ddassert(bulk->state == UNLISTED);

	if (bulk->s1) {
		free(bulk->s1);
		bulk->s1 = NULL;
	}
	if (bulk->s2) {
		free(bulk->s2);
		bulk->s2 = NULL;
	}
	if (bulk->s3) {
		free(bulk->s3);
		bulk->s3 = NULL;
	}
	if (bulk->s4) {
		free(bulk->s4);
		bulk->s4 = NULL;
	}
	if (bulk->r1) {
		free(bulk->r1);
		bulk->r1 = NULL;
	}
	if (bulk->r2) {
		free(bulk->r2);
		bulk->r2 = NULL;
	}
	if (bulk->r3) {
		free(bulk->r3);
		bulk->r3 = NULL;
	}
	if (bulk->r4) {
		free(bulk->r4);
		bulk->r4 = NULL;
	}
	free(bulk);
}

#if 0

/*
 * Returns non-zero if unable to read specified number of bytes
 */
static
int
readall(int fd, void *buf, size_t bytes)
{
	ssize_t r;

	for (;;) {
		r = read(fd, buf, bytes);
		if (r == (ssize_t)bytes)
			break;
		if (r > 0) {
			buf = (char *)buf + r;
			bytes -= r;
			continue;
		}
		if (r < 0 && errno == EINTR)
			continue;
		return 1;
	}
	return 0;
}

static
int
writeall(int fd, const void *buf, size_t bytes)
{
	ssize_t r;

	for (;;) {
		r = write(fd, buf, bytes);
		if (r == (ssize_t)bytes)
			break;
		if (r > 0) {
			buf = (const char *)buf + r;
			bytes -= r;
			continue;
		}
		if (r < 0 && errno == EINTR)
			continue;
		return 1;
	}
	return 0;
}

#endif

static void *
bulkthread(void *arg)
{
	job_t *job = arg;
	bulk_t *bulk;

	pthread_mutex_lock(&BulkMutex);
	for (;;) {
		if (job->terminate)
			break;
		if (job->active == NULL)
			pthread_cond_wait(&job->cond, &BulkMutex);
		bulk = job->active;
		if (bulk) {
			bulk->state = ISRUNNING;

			pthread_mutex_unlock(&BulkMutex);
			BulkFunc(bulk);
			pthread_mutex_lock(&BulkMutex);

			bulk->state = ONRESPONSE;
			bulk->next = NULL;
			*BulkResponseTail = bulk;
			BulkResponseTail = &bulk->next;
			--BulkCurJobs;
			pthread_cond_signal(&BulkResponseCond);
		}

		/*
		 * Optimization - automatically fetch the next job
		 */
		if ((bulk = BulkSubmit) != NULL && job->terminate == 0) {
			BulkSubmit = bulk->next;
			if (BulkSubmit == NULL)
				BulkSubmitTail = &BulkSubmit;
			bulk->state = ONRUN;
			job->active = bulk;
			++BulkCurJobs;
		} else {
			job->active = NULL;
		}
	}
	pthread_mutex_unlock(&BulkMutex);

	return NULL;
}
