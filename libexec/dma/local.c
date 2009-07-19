#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "dma.h"

int
deliver_local(struct qitem *it, const char **errmsg)
{
	char fn[PATH_MAX+1];
	char line[1000];
	size_t linelen;
	int mbox;
	int error;
	off_t mboxlen;
	time_t now = time(NULL);

	error = snprintf(fn, sizeof(fn), "%s/%s", _PATH_MAILDIR, it->addr);
	if (error < 0 || (size_t)error >= sizeof(fn)) {
		syslog(LOG_NOTICE, "local delivery deferred: %m");
		return (1);
	}

	/* mailx removes users mailspool file if empty, so open with O_CREAT */
	mbox = open_locked(fn, O_WRONLY | O_APPEND | O_CREAT);
	if (mbox < 0) {
		syslog(LOG_NOTICE, "local delivery deferred: can not open `%s': %m", fn);
		return (1);
	}
	mboxlen = lseek(mbox, 0, SEEK_CUR);

	if (fseek(it->mailf, it->hdrlen, SEEK_SET) != 0) {
		syslog(LOG_NOTICE, "local delivery deferred: can not seek: %m");
		return (1);
	}

	error = snprintf(line, sizeof(line), "From %s\t%s", it->sender, ctime(&now));
	if (error < 0 || (size_t)error >= sizeof(line)) {
		syslog(LOG_NOTICE, "local delivery deferred: can not write header: %m");
		return (1);
	}
	if (write(mbox, line, error) != error)
		goto wrerror;

	while (!feof(it->mailf)) {
		if (fgets(line, sizeof(line), it->mailf) == NULL)
			break;
		linelen = strlen(line);
		if (linelen == 0 || line[linelen - 1] != '\n') {
			syslog(LOG_CRIT, "local delivery failed: corrupted queue file");
			*errmsg = "corrupted queue file";
			error = -1;
			goto chop;
		}

		if (strncmp(line, "From ", 5) == 0) {
			const char *gt = ">";

			if (write(mbox, gt, 1) != 1)
				goto wrerror;
		}
		if ((size_t)write(mbox, line, linelen) != linelen)
			goto wrerror;
	}
	line[0] = '\n';
	if (write(mbox, line, 1) != 1)
		goto wrerror;
	close(mbox);
	return (0);

wrerror:
	syslog(LOG_ERR, "local delivery failed: write error: %m");
	error = 1;
chop:
	if (ftruncate(mbox, mboxlen) != 0)
		syslog(LOG_WARNING, "error recovering mbox `%s': %m", fn);
	close(mbox);
	return (error);
}
