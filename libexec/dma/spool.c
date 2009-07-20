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
 *
 * $DragonFly$
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
	char line[1000];	/* by RFC2822 */
	char fn[PATH_MAX+1];
	struct stat st;
	struct stritem *t;
	struct qitem *it;
	FILE *mailf;
	off_t hdrlen;
	int error;

	if (snprintf(fn, sizeof(fn), "%s/%s", config->spooldir, "tmp_XXXXXXXXXX") <= 0)
		return (-1);

	queue->mailfd = mkstemp(fn);
	if (queue->mailfd < 0)
		return (-1);
	if (flock(queue->mailfd, LOCK_EX) == -1)
		return (-1);

	queue->tmpf = strdup(fn);
	if (queue->tmpf == NULL)
		goto fail;

	error = snprintf(line, sizeof(line), "%s\n", sender);
	if (error < 0 || (size_t)error >= sizeof(line)) {
		errno = E2BIG;
		goto fail;
	}
	if (write(queue->mailfd, line, error) != error)
		goto fail;

	hdrlen = lseek(queue->mailfd, 0, SEEK_CUR);

	mailf = fdopen(queue->mailfd, "r+");
	if (mailf == NULL)
		goto fail;
	LIST_FOREACH(it, &queue->queue, next) {
		it->mailf = mailf;
		it->hdrlen = hdrlen;
	}

	/*
	 * Assign queue id
	 */
	if (fstat(queue->mailfd, &st) != 0)
		return (-1);
	if (asprintf(&queue->id, "%"PRIxMAX, st.st_ino) < 0)
		return (-1);

	t = malloc(sizeof(*t));
	if (t != NULL) {
		t->str = queue->tmpf;
		SLIST_INSERT_HEAD(&tmpfs, t, next);
	}
	return (0);

fail:
	close(queue->mailfd);
	unlink(fn);
	return (-1);
}

int
linkspool(struct queue *queue)
{
	char line[1000];	/* by RFC2822 */
	struct stat st;
	int error;
	int queuefd;
	struct qitem *it;

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
		if (error < 0 || (size_t)error >= sizeof(line))
			goto delfiles;
		queuefd = open_locked(it->queuefn, O_CREAT|O_EXCL|O_RDWR, 0600);
		if (queuefd == -1)
			goto delfiles;
		if (write(queuefd, line, error) != error) {
			close(queuefd);
			goto delfiles;
		}
		it->queuefd = queuefd;

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
		unlink(it->queuefn);
		unlink(it->mailfn);
	}
	return (-1);
}

void
load_queue(struct queue *queue, int ignorelock)
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
	int fd;
	int locked;

	LIST_INIT(&queue->queue);

	spooldir = opendir(config->spooldir);
	if (spooldir == NULL)
		err(1, "reading queue");

	while ((de = readdir(spooldir)) != NULL) {
		fd = -1;
		sender = NULL;
		queuef = NULL;
		mailf = NULL;
		queueid = NULL;
		queuefn = NULL;
		locked = 1;
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

		fd = open_locked(queuefn, O_RDONLY|O_NONBLOCK);
		if (ignorelock) {
			if (fd < 0)
				fd = open(queuefn, O_RDONLY);
			else
				locked = 0;
		}
		if (fd < 0) {
			/* Ignore locked files */
			if (errno == EWOULDBLOCK)
				continue;
			goto skip_item;
		}

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

		queuef = fdopen(fd, "r");
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
		it->queuefd = fd;
		it->mailf = mailf;
		it->queueid = queueid;
		it->queuefn = queuefn;
		it->mailfn = mailfn;
		it->hdrlen = hdrlen;
		it->locked = locked;
		LIST_INSERT_HEAD(&queue->queue, it, next);

		continue;

skip_item:
		warn("reading queue: `%s'", queuefn);
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
		close(fd);
	}
	closedir(spooldir);
	return;

fail:
	err(1, "reading queue");
}

void
delqueue(struct qitem *it)
{
	unlink(it->queuefn);
	close(it->queuefd);
	unlink(it->mailfn);
	fclose(it->mailf);
	free(it);
}
