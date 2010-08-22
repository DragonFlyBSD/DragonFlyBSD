#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <cpu/param.h>
#include <sys/diskslice.h>

#include "libcryptsetup.h"
#include "internal.h"

struct safe_allocation {
	size_t	size;
	char	data[1];
};

static char *error=NULL;

void set_error_va(const char *fmt, va_list va)
{
	int r;

	if(error) {
		free(error);
		error = NULL;
	}

	if(!fmt) return;

	r = vasprintf(&error, fmt, va);
	if (r < 0) {
		free(error);
		error = NULL;
		return;
	}

	if (r && error[r - 1] == '\n')
		error[r - 1] = '\0';
}

void set_error(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	set_error_va(fmt, va);
	va_end(va);
}

const char *get_error(void)
{
	return error;
}

void *safe_alloc(size_t size)
{
	struct safe_allocation *alloc;

	if (!size)
		return NULL;

	alloc = malloc(size + offsetof(struct safe_allocation, data));
	if (!alloc)
		return NULL;

	alloc->size = size;

	return &alloc->data;
}

void safe_free(void *data)
{
	struct safe_allocation *alloc;

	if (!data)
		return;

	alloc = data - offsetof(struct safe_allocation, data);

	memset(data, 0, alloc->size);

	alloc->size = 0x55aa55aa;
	free(alloc);
}

void *safe_realloc(void *data, size_t size)
{
	void *new_data;

	new_data = safe_alloc(size);

	if (new_data && data) {
		struct safe_allocation *alloc;

		alloc = data - offsetof(struct safe_allocation, data);

		if (size > alloc->size)
			size = alloc->size;

		memcpy(new_data, data, size);
	}

	safe_free(data);
	return new_data;
}

char *safe_strdup(const char *s)
{
	char *s2 = safe_alloc(strlen(s) + 1);

	if (!s2)
		return NULL;

	return strcpy(s2, s);
}

static int get_alignment(int fd)
{
	int alignment = DEFAULT_ALIGNMENT;

#ifdef _PC_REC_XFER_ALIGN
	alignment = fpathconf(fd, _PC_REC_XFER_ALIGN);
	if (alignment < 0)
		alignment = DEFAULT_ALIGNMENT;
#endif
	return alignment;
}

static void *aligned_malloc(void **base, int size, int alignment)
{
#ifdef HAVE_POSIX_MEMALIGN
	return posix_memalign(base, alignment, size) ? NULL : *base;
#else
/* Credits go to Michal's padlock patches for this alignment code */
	char *ptr;

	ptr  = malloc(size + alignment);
	if(ptr == NULL) return NULL;

	*base = ptr;
	if(alignment > 1 && ((long)ptr & (alignment - 1))) {
		ptr += alignment - ((long)(ptr) & (alignment - 1));
	}
	return ptr;
#endif
}
static int sector_size(int fd) 
{
	int bsize;
	return DEV_BSIZE;
#if 0
	if (ioctl(fd,BLKSSZGET, &bsize) < 0)
		return -EINVAL;
	else
		return bsize;
#endif
}

int sector_size_for_device(const char *device)
{
	int fd = open(device, O_RDONLY);
	int r;
	if(fd < 0)
		return -EINVAL;
	r = sector_size(fd);
	close(fd);
	return r;
}

ssize_t write_blockwise(int fd, const void *orig_buf, size_t count)
{
	void *hangover_buf, *hangover_buf_base = NULL;
	void *buf, *buf_base = NULL;
	int r, hangover, solid, bsize, alignment;
	ssize_t ret = -1;

	if ((bsize = sector_size(fd)) < 0)
		return bsize;

	hangover = count % bsize;
	solid = count - hangover;
	alignment = get_alignment(fd);

	if ((long)orig_buf & (alignment - 1)) {
		buf = aligned_malloc(&buf_base, count, alignment);
		if (!buf)
			goto out;
		memcpy(buf, orig_buf, count);
	} else
		buf = (void *)orig_buf;

	r = write(fd, buf, solid);
	if (r < 0 || r != solid)
		goto out;

	if (hangover) {
		hangover_buf = aligned_malloc(&hangover_buf_base, bsize, alignment);
		if (!hangover_buf)
			goto out;

		r = read(fd, hangover_buf, bsize);
		if(r < 0 || r != bsize) goto out;

		r = lseek(fd, -bsize, SEEK_CUR);
		if (r < 0)
			goto out;
		memcpy(hangover_buf, buf + solid, hangover);

		r = write(fd, hangover_buf, bsize);
		if(r < 0 || r != bsize) goto out;
		free(hangover_buf_base);
	}
	ret = count;
 out:
	if (buf != orig_buf)
		free(buf_base);
	return ret;
}

ssize_t read_blockwise(int fd, void *orig_buf, size_t count) {
	void *hangover_buf, *hangover_buf_base;
	void *buf, *buf_base = NULL;
	int r, hangover, solid, bsize, alignment;
	ssize_t ret = -1;

	if ((bsize = sector_size(fd)) < 0)
		return bsize;

	hangover = count % bsize;
	solid = count - hangover;
	alignment = get_alignment(fd);

	if ((long)orig_buf & (alignment - 1)) {
		buf = aligned_malloc(&buf_base, count, alignment);
		if (!buf)
			goto out;
	} else
		buf = orig_buf;

	r = read(fd, buf, solid);
	if(r < 0 || r != solid)
		goto out;

	if (hangover) {
		hangover_buf = aligned_malloc(&hangover_buf_base, bsize, alignment);
		if (!hangover_buf)
			goto out;
		r = read(fd, hangover_buf, bsize);
		if (r <  0 || r != bsize)
			goto out;

		memcpy(buf + solid, hangover_buf, hangover);
		free(hangover_buf_base);
	}
	ret = count;
 out:
	if (buf != orig_buf) {
		memcpy(orig_buf, buf, count);
		free(buf_base);
	}
	return ret;
}

/* 
 * Combines llseek with blockwise write. write_blockwise can already deal with short writes
 * but we also need a function to deal with short writes at the start. But this information
 * is implicitly included in the read/write offset, which can not be set to non-aligned 
 * boundaries. Hence, we combine llseek with write.
 */

ssize_t write_lseek_blockwise(int fd, const char *buf, size_t count, off_t offset) {
	int bsize = sector_size(fd);
	const char *orig_buf = buf;
	char frontPadBuf[bsize];
	int frontHang = offset % bsize;
	int r;
	int innerCount = count < bsize ? count : bsize;

	if (bsize < 0)
		return bsize;

	lseek(fd, offset - frontHang, SEEK_SET);
	if(offset % bsize) {
		r = read(fd,frontPadBuf,bsize);
		if(r < 0) return -1;

		memcpy(frontPadBuf+frontHang, buf, innerCount);

		lseek(fd, offset - frontHang, SEEK_SET);
		r = write(fd,frontPadBuf,bsize);
		if(r < 0) return -1;

		buf += innerCount;
		count -= innerCount;
	}
	if(count <= 0) return buf - orig_buf;

	return write_blockwise(fd, buf, count) + innerCount;
}

/* Password reading helpers */

static int untimed_read(int fd, char *pass, size_t maxlen)
{
	ssize_t i;

	i = read(fd, pass, maxlen);
	if (i > 0) {
		pass[i-1] = '\0';
		i = 0;
	} else if (i == 0) { /* EOF */
		*pass = 0;
		i = -1;
	}
	return i;
}

static int timed_read(int fd, char *pass, size_t maxlen, long timeout)
{
	struct timeval t;
	fd_set fds;
	int failed = -1;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	t.tv_sec = timeout;
	t.tv_usec = 0;

	if (select(fd+1, &fds, NULL, NULL, &t) > 0)
		failed = untimed_read(fd, pass, maxlen);

	return failed;
}

static int interactive_pass(const char *prompt, char *pass, size_t maxlen,
		long timeout)
{
	struct termios orig, tmp;
	int failed = -1;
	int infd = STDIN_FILENO, outfd;

	if (maxlen < 1)
		goto out_err;

	/* Read and write to /dev/tty if available */
	if ((infd = outfd = open("/dev/tty", O_RDWR)) == -1) {
		infd = STDIN_FILENO;
		outfd = STDERR_FILENO;
	}

	if (tcgetattr(infd, &orig))
		goto out_err;

	memcpy(&tmp, &orig, sizeof(tmp));
	tmp.c_lflag &= ~ECHO;

	if (write(outfd, prompt, strlen(prompt)) < 0)
		goto out_err;

	tcsetattr(infd, TCSAFLUSH, &tmp);
	if (timeout)
		failed = timed_read(infd, pass, maxlen, timeout);
	else
		failed = untimed_read(infd, pass, maxlen);
	tcsetattr(infd, TCSAFLUSH, &orig);

out_err:
	if (!failed && write(outfd, "\n", 1));

	if (infd != STDIN_FILENO)
		close(infd);
	return failed;
}

/*
 * Password reading behaviour matrix of get_key
 * FIXME: rewrite this from scratch.
 *                    p   v   n   h
 * -----------------+---+---+---+---
 * interactive      | Y | Y | Y | Inf
 * from fd          | N | N | Y | Inf
 * from binary file | N | N | N | Inf or options->key_size
 *
 * Legend: p..prompt, v..can verify, n..newline-stop, h..read horizon
 *
 * Note: --key-file=- is interpreted as a read from a binary file (stdin)
 */

void get_key(char *prompt, char **key, unsigned int *passLen, int key_size,
            const char *key_file, int timeout, int how2verify,
	    struct crypt_device *cd)
{
	int fd = -1;
	const int verify = how2verify & CRYPT_FLAG_VERIFY;
	const int verify_if_possible = how2verify & CRYPT_FLAG_VERIFY_IF_POSSIBLE;
	char *pass = NULL;
	int read_horizon;
	int regular_file = 0;
	int read_stdin;
	int r;
	struct stat st;

	/* Passphrase read from stdin? */
	read_stdin = (!key_file || !strcmp(key_file, "-")) ? 1 : 0;

	/* read_horizon applies only for real keyfile, not stdin or terminal */
	read_horizon = (key_file && !read_stdin) ? key_size : 0 /* until EOF */;

	/* Setup file descriptior */
	fd = read_stdin ? STDIN_FILENO : open(key_file, O_RDONLY);
	if (fd < 0) {
		log_err(cd, _("Failed to open key file %s.\n"), key_file ?: "-");
		goto out_err;
	}

	/* Interactive case */
	if(isatty(fd)) {
		int i;

		pass = safe_alloc(MAX_TTY_PASSWORD_LEN);
		if (!pass || (i = interactive_pass(prompt, pass, MAX_TTY_PASSWORD_LEN, timeout))) {
			log_err(cd, _("Error reading passphrase from terminal.\n"));
			goto out_err;
		}
		if (verify || verify_if_possible) {
			char pass_verify[MAX_TTY_PASSWORD_LEN];
			i = interactive_pass(_("Verify passphrase: "), pass_verify, sizeof(pass_verify), timeout);
			if (i || strcmp(pass, pass_verify) != 0) {
				log_err(cd, _("Passphrases do not match.\n"));
				goto out_err;
			}
			memset(pass_verify, 0, sizeof(pass_verify));
		}
		*passLen = strlen(pass);
		*key = pass;
	} else {
		/* 
		 * This is either a fd-input or a file, in neither case we can verify the input,
		 * however we don't stop on new lines if it's a binary file.
		 */
		int buflen, i;

		if(verify) {
			log_err(cd, _("Can't do passphrase verification on non-tty inputs.\n"));
			goto out_err;
		}
		/* The following for control loop does an exhausting
		 * read on the key material file, if requested with
		 * key_size == 0, as it's done by LUKS. However, we
		 * should warn the user, if it's a non-regular file,
		 * such as /dev/random, because in this case, the loop
		 * will read forever.
		 */
		if(!read_stdin && read_horizon == 0) {
			if(stat(key_file, &st) < 0) {
				log_err(cd, _("Failed to stat key file %s.\n"), key_file);
				goto out_err;
			}
			if(!S_ISREG(st.st_mode))
				log_std(cd, _("Warning: exhausting read requested, but key file %s"
					" is not a regular file, function might never return.\n"),
					key_file);
			else
				regular_file = 1;
		}
		buflen = 0;
		for(i = 0; read_horizon == 0 || i < read_horizon; i++) {
			if(i >= buflen - 1) {
				buflen += 128;
				pass = safe_realloc(pass, buflen);
				if (!pass) {
					log_err(cd, _("Out of memory while reading passphrase.\n"));
					goto out_err;
				}
			}

			r = read(fd, pass + i, 1);
			if (r < 0) {
				log_err(cd, _("Error reading passphrase.\n"));
				goto out_err;
			}

			/* Stop on newline only if not requested read from keyfile */
			if(r == 0 || (!key_file && pass[i] == '\n'))
				break;
		}
		/* Fail if piped input dies reading nothing */
		if(!i && !regular_file) {
			log_dbg("Error reading passphrase.");
			goto out_err;
		}
		pass[i] = 0;
		*key = pass;
		*passLen = i;
	}
	if(fd != STDIN_FILENO)
		close(fd);
	return;

out_err:
	if(fd >= 0 && fd != STDIN_FILENO)
		close(fd);
	if(pass)
		safe_free(pass);
	*key = NULL;
	*passLen = 0;
}

int device_ready(struct crypt_device *cd, const char *device, int mode)
{
	int devfd, r = 1;
	ssize_t s;
	struct stat st;
	char buf[512];

	if(stat(device, &st) < 0) {
		log_err(cd, _("Device %s doesn't exist or access denied.\n"), device);
		return 0;
	}

	log_dbg("Trying to open and read device %s.", device);
	devfd = open(device, mode | O_DIRECT | O_SYNC);
	if(devfd < 0) {
		log_err(cd, _("Cannot open device %s for %s%s access.\n"), device,
			(mode & O_EXCL) ? _("exclusive ") : "",
			(mode & O_RDWR) ? _("writable") : _("read-only"));
		return 0;
	}

	 /* Try to read first sector */
	s = read_blockwise(devfd, buf, sizeof(buf));
	if (s < 0 || s != sizeof(buf)) {
		log_err(cd, _("Cannot read device %s.\n"), device);
		r = 0;
	}

	memset(buf, 0, sizeof(buf));
	close(devfd);

	return r;
}

int get_device_infos(const char *device, struct device_infos *infos, struct crypt_device *cd)
{
	struct partinfo pinfo;
	uint64_t size;
	unsigned long size_small;
	int readonly = 0;
	int ret = -1;
	int fd;

	/* Try to open read-write to check whether it is a read-only device */
	fd = open(device, O_RDWR);
	if (fd < 0) {
		if (errno == EROFS) {
			readonly = 1;
			fd = open(device, O_RDONLY);
		}
	} else {
		close(fd);
		fd = open(device, O_RDONLY);
	}
	if (fd < 0) {
		log_err(cd, _("Cannot open device: %s\n"), device);
		return -1;
	}

#ifdef BLKGETSIZE64
	if (ioctl(fd, BLKGETSIZE64, &size) >= 0) {
		size >>= SECTOR_SHIFT;
		ret = 0;
		goto out;
	}
#endif

#ifdef BLKGETSIZE
	if (ioctl(fd, BLKGETSIZE, &size_small) >= 0) {
		size = (uint64_t)size_small;
		ret = 0;
		goto out;
	}
#else
	if (ioctl(fd, DIOCGPART, &pinfo) >= 0) {
		size = pinfo.media_blocks;
		ret = 0;
		goto out;	
	}
#endif

	log_err(cd, _("BLKGETSIZE failed on device %s.\n"), device);
out:
	if (ret == 0) {
		infos->size = size;
		infos->readonly = readonly;
	}
	close(fd);
	return ret;
}

int wipe_device_header(const char *device, int sectors)
{
	char *buffer;
	int size = sectors * SECTOR_SIZE;
	int r = -1;
	int devfd;

	devfd = open(device, O_RDWR | O_DIRECT | O_SYNC);
	if(devfd == -1)
		return -EINVAL;

	buffer = malloc(size);
	if (!buffer) {
		close(devfd);
		return -ENOMEM;
	}
	memset(buffer, 0, size);

	r = write_blockwise(devfd, buffer, size) < size ? -EIO : 0;

	free(buffer);
	close(devfd);

	return r;
}

/* MEMLOCK */
#define DEFAULT_PROCESS_PRIORITY -18

static int _priority;
static int _memlock_count = 0;

// return 1 if memory is locked
int crypt_memlock_inc(struct crypt_device *ctx)
{
	if (!_memlock_count++) {
		log_dbg("Locking memory.");
		if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
#if 0
			log_err(ctx, _("WARNING!!! Possibly insecure memory. Are you root?\n"));
#endif
			log_err(ctx, _("WARNING!!! Possibly insecure memory, missing mlockall()\n"));
			_memlock_count--;
			return 0;
		}
		errno = 0;
		if (((_priority = getpriority(PRIO_PROCESS, 0)) == -1) && errno)
			log_err(ctx, _("Cannot get process priority.\n"));
		else
			if (setpriority(PRIO_PROCESS, 0, DEFAULT_PROCESS_PRIORITY))
				log_err(ctx, _("setpriority %u failed: %s"),
					DEFAULT_PROCESS_PRIORITY, strerror(errno));
	}
	return _memlock_count ? 1 : 0;
}

int crypt_memlock_dec(struct crypt_device *ctx)
{
	if (_memlock_count && (!--_memlock_count)) {
		log_dbg("Unlocking memory.");
		if (munlockall())
			log_err(ctx, _("Cannot unlock memory."));
		if (setpriority(PRIO_PROCESS, 0, _priority))
			log_err(ctx, _("setpriority %u failed: %s"), _priority, strerror(errno));
	}
	return _memlock_count ? 1 : 0;
}

/* DEVICE TOPOLOGY */

/* block device topology ioctls, introduced in 2.6.32 */
#ifndef BLKIOMIN
#define BLKIOMIN    _IO(0x12,120)
#define BLKIOOPT    _IO(0x12,121)
#define BLKALIGNOFF _IO(0x12,122)
#endif

void get_topology_alignment(const char *device,
			    unsigned long *required_alignment, /* bytes */
			    unsigned long *alignment_offset,   /* bytes */
			    unsigned long default_alignment)
{
	unsigned int dev_alignment_offset = 0;
	unsigned long min_io_size = 0, opt_io_size = 0;
	int fd;

	*required_alignment = default_alignment;
	*alignment_offset = 0;

	fd = open(device, O_RDONLY);
	if (fd == -1)
		return;

	/* minimum io size */
	if (ioctl(fd, BLKIOMIN, &min_io_size) == -1) {
		log_dbg("Topology info for %s not supported, using default offset %lu bytes.",
			device, default_alignment);
		goto out;
	}

	/* optimal io size */
	if (ioctl(fd, BLKIOOPT, &opt_io_size) == -1)
		opt_io_size = min_io_size;

	/* alignment offset, bogus -1 means misaligned/unknown */
	if (ioctl(fd, BLKALIGNOFF, &dev_alignment_offset) == -1 || (int)dev_alignment_offset < 0)
		dev_alignment_offset = 0;

	if (*required_alignment < min_io_size)
		*required_alignment = min_io_size;

	if (*required_alignment < opt_io_size)
		*required_alignment = opt_io_size;

	*alignment_offset = (unsigned long)dev_alignment_offset;

	log_dbg("Topology: IO (%lu/%lu), offset = %lu; Required alignment is %lu bytes.",
		min_io_size, opt_io_size, *alignment_offset, *required_alignment);
out:
	(void)close(fd);
}
