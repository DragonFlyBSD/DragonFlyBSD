/*
 * Copyright 2000 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/libc/gen/posixshm.c,v 1.2.2.1 2000/08/22 01:48:12 jhb Exp $
 */

#include "namespace.h"
#include <sys/types.h>
#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include "un-namespace.h"

static int mkintermediatedirs(int dfd, const char *path);

int
shm_open(const char *path, int flags, mode_t mode)
{
	int dfd;
	int fd;
	struct stat stab;

	if ((flags & O_ACCMODE) == O_WRONLY) {
		errno = EINVAL;
		return (-1);
	}

	while (*path == '/')
		++path;

	/*
	 * Expect a tmpfs filesystem for our shm base.  If for some reason
	 * it does not exist (it really should), fall-back to /tmp.
	 */
	dfd = _open("/var/run/shm", O_RDONLY|O_DIRECTORY);
	if (dfd < 0)
		dfd = _open("/tmp", O_RDONLY|O_DIRECTORY);

	if (dfd >= 0) {
		fd = _openat(dfd, path, flags, mode);
		if (fd < 0 && errno == ENOENT &&
		    mkintermediatedirs(dfd, path) == 0)
		{
			fd = _openat(dfd, path, flags, mode);
		}
		_close(dfd);
	} else {
		fd = -1;
	}

	if (fd != -1) {
		if (_fstat(fd, &stab) != 0 || !S_ISREG(stab.st_mode)) {
			_close(fd);
			errno = EINVAL;
			return (-1);
		}

		if (_fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 ||
		    _fcntl(fd, F_SETFL, (int)FPOSIXSHM) != 0) {
			_close(fd);
			return (-1);
		}
	}
	return (fd);
}

int
shm_unlink(const char *path)
{
	int res;
	int dfd;

	while (*path == '/')
		++path;

	/*
	 * Expect a tmpfs filesystem for our shm base.  If for some reason
	 * it does not exist (it really should), fall-back to /tmp.
	 */
	dfd = _open("/var/run/shm", O_RDONLY|O_DIRECTORY);
	if (dfd < 0)
		dfd = _open("/tmp", O_RDONLY|O_DIRECTORY);

	if (dfd >= 0) {
		res = _unlinkat(dfd, path, 0);
		_close(dfd);
	} else {
		res = -1;
	}
	return res;
}

/*
 * Create any missing intermediate directories, modes 1777
 */
static int
mkintermediatedirs(int dfd, const char *path)
{
	const char *eptr;
	char buf[PATH_MAX+1];
	size_t len;
	int res = 0;

	eptr = path - 1;
	while ((eptr = strchr(eptr + 1, '/')) != NULL) {
		len = eptr - path;
		if (len > PATH_MAX) {
			errno = EINVAL;
			return -1;
		}
		bcopy(path, buf, len);
		buf[len] = 0;
		res = _mkdirat(dfd, buf, 01777);
		if (res < 0 && errno != EEXIST)
			break;
	}
	return res;
}
