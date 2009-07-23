/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Simon 'corecode' Schubert <corecode@fs.ei.tum.de>.
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

#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <syslog.h>

#include "dma.h"

/*
 * Spool file format:
 *
 * 'Q'id files (queue):
 *   id envelope-to
 *
 * 'M'id files (data):
 *   envelope-from
 *   mail data
 *
 * Each queue file needs to have a corresponding data file.
 * One data file might be shared by linking it several times.
 *
 * Queue ids are unique, formed from the inode of the data file
 * and a unique identifier.
 */

int
newspoolf(struct queue *queue, const char *sender)
{
	char fn[PATH_MAX+1];
	struct stat st;
	struct stritem *t;
	struct qitem *it;
	off_t hdrlen;
	int fd;

	if (snprintf(fn, sizeof(fn), "%s/%s", config->spooldir, "tmp_XXXXXXXXXX") <= 0)
		return (-1);

	fd = mkstemp(fn);
	if (fd < 0)
		return (-1);
	if (flock(fd, LOCK_EX) == -1)
		goto fail;
	queue->tmpf = strdup(fn);
	if (queue->tmpf == NULL)
		goto fail;

	/*
	 * Assign queue id
	 */
	if (fstat(fd, &st) != 0)
		goto fail;
	if (asprintf(&queue->id, "%"PRIxMAX, st.st_ino) < 0)
		goto fail;

	queue->mailf = fdopen(fd, "r+");
	if (queue->mailf == NULL)
		goto fail;

	if (fprintf(queue->mailf, "%s\n", sender) < 0)
		goto fail;

	hdrlen = ftello(queue->mailf);

	LIST_FOREACH(it, &queue->queue, next) {
		it->mailf = queue->mailf;
		it->hdrlen = hdrlen;
	}

	t = malloc(sizeof(*t));
	if (t != NULL) {
		t->str = queue->tmpf;
		SLIST_INSERT_HEAD(&tmpfs, t, next);
	}
	return (0);

fail:
	if (queue->mailf != NULL)
		fclose(queue->mailf);
	close(fd);
	unlink(fn);
	return (-1);
}

int
linkspool(struct queue *queue, const char *sender)
{
	char line[1000];	/* by RFC2822 */
	struct stat st;
	size_t error;
	int queuefd;
	struct qitem *it;

	if (fflush(queue->mailf) != 0 || fsync(fileno(queue->mailf)) != 0)
		goto delfiles;

	syslog(LOG_INFO, "new mail from user=%s uid=%d envelope_from=<%s>",
	       username, getuid(), sender);

	LIST_FOREACH(it, &queue->queue, next) {
		if (asprintf(&it->queueid, "%s.%"PRIxPTR, queue->id, (uintptr_t)it) <= 0)
			goto delfiles;
		if (asprintf(&it->queuefn, "%s/Q%s", config->spooldir, it->queueid) <= 0)
			goto delfiles;
		if (asprintf(&it->mailfn, "%s/M%s", config->spooldir, it->queueid) <= 0)
			goto delfiles;

		/* Neither file may not exist yet */
		if (stat(it->queuefn, &st) == 0 || stat(it->mailfn, &st) == 0)
			goto delfiles;

		error = snprintf(line, sizeof(line), "%s %s\n", it->queueid, it->addr);
		if ((ssize_t)error < 0 || error >= sizeof(line))
			goto delfiles;

		queuefd = open_locked(it->queuefn, O_CREAT|O_EXCL|O_RDWR, 0600);
		if (queuefd == -1)
			goto delfiles;
		it->queuef = fdopen(queuefd, "w+");
		if (it->queuef == NULL)
			goto delfiles;

		if (fwrite(line, strlen(line), 1, it->queuef) != 1)
			goto delfiles;
		if (fflush(it->queuef) != 0 || fsync(fileno(it->queuef)) != 0)
			goto delfiles;

		if (link(queue->tmpf, it->mailfn) != 0)
			goto delfiles;
	}

	LIST_FOREACH(it, &queue->queue, next) {
		syslog(LOG_INFO, "mail to=<%s> queued as %s",
		       it->addr, it->queueid);
	}

	unlink(queue->tmpf);
	return (0);

delfiles:
	LIST_FOREACH(it, &queue->queue, next) {
		unlink(it->mailfn);
		unlink(it->queuefn);
	}
	return (-1);
}

int
load_queue(struct queue *queue)
{
	struct qitem *it;
	//struct queue queue, itmqueue;
	struct queue itmqueue;
	DIR *spooldir;
	struct dirent *de;
	char line[1000];
	FILE *queuef;
	FILE *mailf;
	char *sender;
	char *addr;
	char *queueid;
	char *queuefn;
	char *mailfn;
	off_t hdrlen;

	LIST_INIT(&queue->queue);

	spooldir = opendir(config->spooldir);
	if (spooldir == NULL)
		err(1, "reading queue");

	while ((de = readdir(spooldir)) != NULL) {
		sender = NULL;
		queuef = NULL;
		mailf = NULL;
		queueid = NULL;
		queuefn = NULL;
		LIST_INIT(&itmqueue.queue);

		/* ignore temp files */
		if (strncmp(de->d_name, "tmp_", 4) == 0 || de->d_type != DT_REG)
			continue;
		if (de->d_name[0] != 'Q')
			continue;
		if (asprintf(&queuefn, "%s/Q%s", config->spooldir, de->d_name + 1) < 0)
			goto fail;
		if (asprintf(&mailfn, "%s/M%s", config->spooldir, de->d_name + 1) < 0)
			goto fail;

		mailf = fopen(mailfn, "r");
		if (mailf == NULL)
			goto skip_item;
		if (fgets(line, sizeof(line), mailf) == NULL || line[0] == 0)
			goto skip_item;
		line[strlen(line) - 1] = 0;	/* chop newline */
		sender = strdup(line);
		if (sender == NULL)
			goto skip_item;

		hdrlen = ftell(mailf);

		queuef = fopen(queuefn, "r");
		if (queuef == NULL)
			goto skip_item;

		if (fgets(line, sizeof(line), queuef) == NULL || line[0] == 0)
			goto skip_item;
		line[strlen(line) - 1] = 0;
		queueid = strdup(line);
		if (queueid == NULL)
			goto skip_item;
		addr = strchr(queueid, ' ');
		if (addr == NULL)
			goto skip_item;
		*addr++ = 0;

		if (add_recp(&itmqueue, addr, sender, 0) != 0)
			goto skip_item;

		it = LIST_FIRST(&itmqueue.queue);
		it->queueid = queueid;
		it->queuefn = queuefn;
		it->mailfn = mailfn;
		it->hdrlen = hdrlen;
		LIST_INSERT_HEAD(&queue->queue, it, next);

		if (queuef != NULL)
			fclose(queuef);
		if (mailf != NULL)
			fclose(mailf);
		continue;

skip_item:
		syslog(LOG_INFO, "could not pick up queue file: `%s'/`%s': %m", queuefn, mailfn);
		if (sender != NULL)
			free(sender);
		if (queuefn != NULL)
			free(queuefn);
		if (mailfn != NULL)
			free(queuefn);
		if (queueid != NULL)
			free(queueid);
		if (queuef != NULL)
			fclose(queuef);
		if (mailf != NULL)
			fclose(mailf);
	}
	closedir(spooldir);
	return (0);

fail:
	return (-1);
}

void
delqueue(struct qitem *it)
{
	unlink(it->mailfn);
	unlink(it->queuefn);
	if (it->queuef != NULL)
		fclose(it->queuef);
	if (it->mailf != NULL)
		fclose(it->mailf);
	free(it);
}

int
aquirespool(struct qitem *it)
{
	int queuefd;

	if (it->queuef == NULL) {
		queuefd = open_locked(it->queuefn, O_RDWR|O_NONBLOCK);
		if (queuefd < 0)
			goto fail;
		it->queuef = fdopen(queuefd, "r+");
		if (it->queuef == NULL)
			goto fail;
	}

	if (it->mailf == NULL) {
		it->mailf = fopen(it->mailfn, "r");
		if (it->mailf == NULL)
			goto fail;
	}

	return (0);

fail:
	syslog(LOG_INFO, "could not aquire queue file: %m");
	return (-1);
}

void
dropspool(struct queue *queue, struct qitem *keep)
{
	struct qitem *it;

	LIST_FOREACH(it, &queue->queue, next) {
		if (it == keep)
			continue;

		if (it->queuef != NULL)
			fclose(it->queuef);
		if (it->mailf != NULL)
			fclose(it->mailf);
	}
}
