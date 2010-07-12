#ifndef INTERNAL_H
#define INTERNAL_H

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif

#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <inttypes.h>

#include "nls.h"

#define SECTOR_SHIFT		9
#define SECTOR_SIZE		(1 << SECTOR_SHIFT)
#define DEFAULT_ALIGNMENT	4096

#define MAX_TTY_PASSWORD_LEN	512

/* private struct crypt_options flags */

#define	CRYPT_FLAG_FREE_DEVICE	(1 << 24)
#define	CRYPT_FLAG_FREE_CIPHER	(1 << 25)

#define CRYPT_FLAG_PRIVATE_MASK ((unsigned int)-1 << 24)

#define at_least(a, b) ({ __typeof__(a) __at_least = (a); (__at_least >= (b))?__at_least:(b); })

struct hash_type {
	char		*name;
	void		*private;
	int		(*fn)(void *data, int size, char *key,
			      int sizep, const char *passphrase);
};

struct hash_backend {
	const char		*name;
	struct hash_type *	(*get_hashes)(void);
	void			(*free_hashes)(struct hash_type *hashes);
};

struct device_infos {
	uint64_t	size;
	int		readonly;
};

struct crypt_device;
int crypt_confirm(struct crypt_device *cd, const char *msg);

void set_error_va(const char *fmt, va_list va);
void set_error(const char *fmt, ...);
const char *get_error(void);
void *safe_alloc(size_t size);
void safe_free(void *data);
void *safe_realloc(void *data, size_t size);
char *safe_strdup(const char *s);
void set_debug_level(int level);

int init_crypto(void);
struct hash_backend *get_hash_backend(const char *name);
void put_hash_backend(struct hash_backend *backend);
int hash(const char *backend_name, const char *hash_name,
         char *result, size_t size,
         const char *passphrase, size_t sizep);

void hexprint(char *d, int n);

/* Device mapper backend */
const char *dm_get_dir(void);
int dm_init(struct crypt_device *context, int check_kernel);
void dm_exit(void);
int dm_remove_device(const char *name, int force, uint64_t size);
int dm_status_device(const char *name);
int dm_query_device(const char *name,
		    char **device,
		    uint64_t *size,
		    uint64_t *skip,
		    uint64_t *offset,
		    char **cipher,
		    int *key_size,
		    char **key,
		    int *read_only,
		    int *suspended,
		    char **uuid);
int dm_create_device(const char *name, const char *device, const char *cipher,
		     const char *type, const char *uuid,
		     uint64_t size, uint64_t skip, uint64_t offset,
		     size_t key_size, const char *key,
		     int read_only, int reload);
int dm_suspend_and_wipe_key(const char *name);
int dm_resume_and_reinstate_key(const char *name,
				size_t key_size,
				const char *key);

int sector_size_for_device(const char *device);
ssize_t write_blockwise(int fd, const void *buf, size_t count);
ssize_t read_blockwise(int fd, void *_buf, size_t count);
ssize_t write_lseek_blockwise(int fd, const char *buf, size_t count, off_t offset);
int device_ready(struct crypt_device *cd, const char *device, int mode);
int get_device_infos(const char *device, struct device_infos *infos, struct crypt_device *cd);
int wipe_device_header(const char *device, int sectors);

void get_key(char *prompt, char **key, unsigned int *passLen, int key_size,
	     const char *key_file, int timeout, int how2verify,
	     struct crypt_device *cd);

int parse_into_name_and_mode(const char *nameAndMode, char *name, char *mode);

void logger(struct crypt_device *cd, int class, const char *file, int line, const char *format, ...);
#define log_dbg(x...) logger(NULL, CRYPT_LOG_DEBUG, __FILE__, __LINE__, x)
#define log_std(c, x...) logger(c, CRYPT_LOG_NORMAL, __FILE__, __LINE__, x)
#define log_verbose(c, x...) logger(c, CRYPT_LOG_VERBOSE, __FILE__, __LINE__, x)
#define log_err(c, x...) do { \
	logger(c, CRYPT_LOG_ERROR, __FILE__, __LINE__, x); \
	set_error(x); } while(0)

int crypt_get_debug_level(void);
void debug_processes_using_device(const char *name);

int crypt_memlock_inc(struct crypt_device *ctx);
int crypt_memlock_dec(struct crypt_device *ctx);

void get_topology_alignment(const char *device,
			    unsigned long *required_alignment, /* bytes */
			    unsigned long *alignment_offset,   /* bytes */
			    unsigned long default_alignment);

#endif /* INTERNAL_H */
