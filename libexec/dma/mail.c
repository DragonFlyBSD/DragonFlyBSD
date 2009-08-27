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

#include <errno.h>
#include <syslog.h>
#include <unistd.h>

#include "dma.h"

void
bounce(struct qitem *it, const char *reason)
{
	struct queue bounceq;
	char line[1000];
	size_t pos;
	int error;

	/* Don't bounce bounced mails */
	if (it->sender[0] == 0) {
		syslog(LOG_INFO, "can not bounce a bounce message, discarding");
		exit(1);
	}

	LIST_INIT(&bounceq.queue);
	if (add_recp(&bounceq, it->sender, "", 1) != 0)
		goto fail;

	if (newspoolf(&bounceq, "") != 0)
		goto fail;

	syslog(LOG_ERR, "delivery failed, bouncing as %s", bounceq.id);
	setlogident("%s", bounceq.id);

	error = fprintf(bounceq.mailf,
		"Received: from MAILER-DAEMON\n"
		"\tid %s\n"
		"\tby %s (%s)\n"
		"\t%s\n"
		"X-Original-To: <%s>\n"
		"From: MAILER-DAEMON <>\n"
		"To: %s\n"
		"Subject: Mail delivery failed\n"
		"Message-Id: <%s@%s>\n"
		"Date: %s\n"
		"\n"
		"This is the %s at %s.\n"
		"\n"
		"There was an error delivering your mail to <%s>.\n"
		"\n"
		"%s\n"
		"\n"
		"%s\n"
		"\n",
		bounceq.id,
		hostname(), VERSION,
		rfc822date(),
		it->addr,
		it->sender,
		bounceq.id, hostname(),
		rfc822date(),
		VERSION, hostname(),
		it->addr,
		reason,
		config->features & FULLBOUNCE ?
		    "Original message follows." :
		    "Message headers follow.");
	if (error < 0)
		goto fail;

	if (fseek(it->mailf, it->hdrlen, SEEK_SET) != 0)
		goto fail;
	if (config->features & FULLBOUNCE) {
		while ((pos = fread(line, 1, sizeof(line), it->mailf)) > 0) {
			if (fwrite(line, 1, pos, bounceq.mailf) != pos)
				goto fail;
		}
	} else {
		while (!feof(it->mailf)) {
			if (fgets(line, sizeof(line), it->mailf) == NULL)
				break;
			if (line[0] == '\n')
				break;
			if (fwrite(line, strlen(line), 1, bounceq.mailf) != 1)
				goto fail;
		}
	}

	if (linkspool(&bounceq, "") != 0)
		goto fail;
	/* bounce is safe */

	delqueue(it);

	run_queue(&bounceq);
	/* NOTREACHED */

fail:
	syslog(LOG_CRIT, "error creating bounce: %m");
	delqueue(it);
	exit(1);
}

int
readmail(struct queue *queue, const char *sender, int nodot)
{
	char line[1000];	/* by RFC2822 */
	size_t linelen;
	size_t error;
	int had_headers = 0;
	int had_from = 0;
	int had_messagid = 0;
	int had_date = 0;

	error = fprintf(queue->mailf,
		"Received: from %s (uid %d)\n"
		"\t(envelope-from %s)\n"
		"\tid %s\n"
		"\tby %s (%s)\n"
		"\t%s\n",
		username, getuid(),
		sender,
		queue->id,
		hostname(), VERSION,
		rfc822date());
	if ((ssize_t)error < 0)
		return (-1);

	while (!feof(stdin)) {
		if (fgets(line, sizeof(line), stdin) == NULL)
			break;
		linelen = strlen(line);
		if (linelen == 0 || line[linelen - 1] != '\n') {
			errno = EINVAL;		/* XXX mark permanent errors */
			return (-1);
		}
		if (!had_headers) {
			if (strprefixcmp(line, "Date:") == 0)
				had_date = 1;
			else if (strprefixcmp(line, "Message-Id:") == 0)
				had_messagid = 1;
			else if (strprefixcmp(line, "From:") == 0)
				had_from = 1;
		}
		if (strcmp(line, "\n") == 0 && !had_headers) {
			had_headers = 1;
			while (!had_date || !had_messagid || !had_from) {
				if (!had_date) {
					had_date = 1;
					snprintf(line, sizeof(line), "Date: %s\n", rfc822date());
				} else if (!had_messagid) {
					/* XXX better msgid, assign earlier and log? */
					had_messagid = 1;
					snprintf(line, sizeof(line), "Message-Id: <%s@%s>\n",
						 queue->id, hostname());
				} else if (!had_from) {
					had_from = 1;
					snprintf(line, sizeof(line), "From: <%s>\n", sender);
				}
				if (fwrite(line, strlen(line), 1, queue->mailf) != 1)
					return (-1);
			}
			strcpy(line, "\n");
		}
		if (!nodot && linelen == 2 && line[0] == '.')
			break;
		if (fwrite(line, strlen(line), 1, queue->mailf) != 1)
			return (-1);
	}

	return (0);
}
