#ifndef _LIBCRYPTSETUP_H
#define _LIBCRYPTSETUP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct crypt_device; /* crypt device handle */

/**
 * Initialise crypt device handle and check if provided device exists.
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - returns pointer to crypt device handle
 * @device - path to device
 *
 * Note that logging is not initialized here, possible messages uses
 * default log function.
 */
int crypt_init(struct crypt_device **cd, const char *device);

/**
 * Initialise crypt device handle from provided active device name
 * and check if provided device exists.
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - crypt device handle
 * @name - name of active crypt device
 */
int crypt_init_by_name(struct crypt_device **cd, const char *name);

/**
 * Set log function.
 *
 * @cd - crypt device handle (can be NULL to set default log function)
 * @usrptr - provided identification in callback
 * @level  - log level below (debug messages can uses other levels)
 * @msg    - log message
 */
#define CRYPT_LOG_NORMAL 0
#define CRYPT_LOG_ERROR  1
#define CRYPT_LOG_VERBOSE  2
#define CRYPT_LOG_DEBUG -1 /* always on stdout */
void crypt_set_log_callback(struct crypt_device *cd,
	void (*log)(int level, const char *msg, void *usrptr),
	void *usrptr);

/**
 * Log message through log function.
 *
 * @cd - crypt device handle
 * @level  - log level
 * @msg    - log message
 */
void crypt_log(struct crypt_device *cd, int level, const char *msg);

/**
 * Set confirmation callback (yes/no)
 *
 * If code need confirmation (like deleting last key slot) this function
 * is called. If not defined, everything is confirmed.
 *
 * Calback should return 0 if operation is declined, other values mean accepted.
 *
 * @cd - crypt device handle
 * @usrptr - provided identification in callback
 * @msg - Message for user to confirm
 */
void crypt_set_confirm_callback(struct crypt_device *cd,
	int (*confirm)(const char *msg, void *usrptr),
	void *usrptr);

/**
 * Set password query callback.
 *
 * If code need _interactive_ query for password, this callback is called.
 * If not defined, compiled-in default is called (uses terminal input).
 *
 * @cd - crypt device handle
 * @usrptr - provided identification in callback
 * @msg - Message for user
 * @buf - buffer for password
 * @length - size of buffer
 *
 * - Note that if this function is defined, verify option is ignored
 *   (caller whch provided callback is responsible fo password verification)
 * - Only zero terminated passwords can be enteted this way, for complex
 *   API functions directly.
 * - Maximal length of password is limited to @length-1 (minimal 511 chars)
 */
void crypt_set_password_callback(struct crypt_device *cd,
	int (*password)(const char *msg, char *buf, size_t length, void *usrptr),
	void *usrptr);

/**
 * Various crypt device parameters
 *
 * @cd - crypt device handle
 * @timeout - timeout in secons for password entry if compiled-in function used
 * @password_retry - number of tries for password if not verified
 * @iteration_time - iteration time for LUKS header in miliseconds
 * @password_verify - for compiled-in password query always verify passwords twice
 */
void crypt_set_timeout(struct crypt_device *cd, uint64_t timeout_sec);
void crypt_set_password_retry(struct crypt_device *cd, int tries);
void crypt_set_iterarion_time(struct crypt_device *cd, uint64_t iteration_time_ms);
void crypt_set_password_verify(struct crypt_device *cd, int password_verify);

/**
 * Helper to lock/unlock memory to avoid swap sensitive data to disk
 *
 * @cd - crypt device handle, can be NULL
 * @lock - 0 to unloct otherwise lock memory
 *
 * Return value indicates that memory is locked (function can be called multiple times).
 * Only root can do this. Note it locks/unlocks all process memory, not only crypt context.
 */
int crypt_memory_lock(struct crypt_device *cd, int lock);

#define CRYPT_PLAIN "PLAIN" /* regular crypt device, no on-disk header */
#define CRYPT_LUKS1 "LUKS1" /* LUKS version 1 header on-disk */

struct crypt_params_plain {
	const char *hash; /* password hash function */
	uint64_t offset;  /* offset in sectors */
	uint64_t skip;    /* IV initilisation sector */
};

struct crypt_params_luks1 {
	const char *hash;      /* hash used in LUKS header */
	size_t data_alignment; /* in sectors, data offset is multiple of this */
};

/**
 * Create (format) new crypt device (and possible header on-disk) but not activates it.
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - crypt device handle
 * @type - type of device (optional params struct must be of this type)
 * @cipher - (e.g. "aes")
 * @cipher_mode - including IV specification (e.g. "xts-plain")
 * @uuid - requested UUID or NULL if it should be generated
 * @volume_key - pre-generated volume key or NULL if it should be generated (only for LUKS)
 * @volume_key_size - size og volume key in bytes.
 * @params - crypt type specific parameters
 *
 * Note that crypt_format do not enable any keyslot, but it stores volume key internally
 * and subsequent crypt_keyslot_add_* calls can be used.
 * (It is the only situation when crypt_keyslot_add_* do not require active key slots.)
 */
int crypt_format(struct crypt_device *cd,
	const char *type,
	const char *cipher,
	const char *cipher_mode,
	const char *uuid,
	const char *volume_key,
	size_t volume_key_size,
	void *params);

/**
 * Load crypt device parameters from on-disk header
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - crypt device handle
 * @requested_type - use NULL for all known
 * @params - crypt type specific parameters
 */
int crypt_load(struct crypt_device *cd,
	       const char *requested_type,
	       void *params);

/**
 * Suspends crypt device.
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - crypt device handle, can be NULL
 * @name - name of device to suspend
 */
int crypt_suspend(struct crypt_device *cd,
		  const char *name);

/**
 * Resumes crypt device using passphrase.
 *
 * Returns unlocked key slot number or negative errno otherwise.
 *
 * @cd - crypt device handle
 * @name - name of device to resume
 * @keyslot - requested keyslot or CRYPT_ANY_SLOT
 * @passphrase - passphrase used to unlock volume key, NULL for query
 * @passphrase_size - size of @passphrase (binary data)
 */
int crypt_resume_by_passphrase(struct crypt_device *cd,
			       const char *name,
			       int keyslot,
			       const char *passphrase,
			       size_t passphrase_size);

/**
 * Resumes crypt device using key file.
 *
 * Returns unlocked key slot number or negative errno otherwise.
 *
 * @cd - crypt device handle
 * @name - name of device to resume
 * @keyslot - requested keyslot or CRYPT_ANY_SLOT
 * @keyfile - key file used to unlock volume key, NULL for passphrase query
 * @keyfile_size - number of bytes to read from @keyfile, 0 is unlimited
 */
int crypt_resume_by_keyfile(struct crypt_device *cd,
			    const char *name,
			    int keyslot,
			    const char *keyfile,
			    size_t keyfile_size);

/**
 * Releases crypt device context and used memory.
 *
 * @cd - crypt device handle
 */
void crypt_free(struct crypt_device *cd);

/**
 * Add key slot using provided passphrase
 *
 * Returns allocated key slot number or negative errno otherwise.
 *
 * @cd - crypt device handle
 * @keyslot - requested keyslot or CRYPT_ANY_SLOT
 * @passphrase - passphrase used to unlock volume key, NULL for query
 * @passphrase_size - size of @passphrase (binary data)
 * @new_passphrase - passphrase for new keyslot, NULL for query
 * @new_passphrase_size - size of @new_passphrase (binary data)
 */
#define CRYPT_ANY_SLOT -1
int crypt_keyslot_add_by_passphrase(struct crypt_device *cd,
	int keyslot,
	const char *passphrase,
	size_t passphrase_size,
	const char *new_passphrase,
	size_t new_passphrase_size);

/**
* Add key slot using provided key file path
 *
 * Returns allocated key slot number or negative errno otherwise.
 *
 * @cd - crypt device handle
 * @keyslot - requested keyslot or CRYPT_ANY_SLOT
 * @keyfile - key file used to unlock volume key, NULL for passphrase query
 * @keyfile_size - number of bytes to read from @keyfile, 0 is unlimited
 * @new_keyfile - keyfile for new keyslot, NULL for passphrase query
 * @new_keyfile_size - number of bytes to read from @new_keyfile, 0 is unlimited
 *
 * Note that @keyfile can be "-" for STDIN
 */
int crypt_keyslot_add_by_keyfile(struct crypt_device *cd,
	int keyslot,
	const char *keyfile,
	size_t keyfile_size,
	const char *new_keyfile,
	size_t new_keyfile_size);

/**
 * Add key slot using provided volume key
 *
 * Returns allocated key slot number or negative errno otherwise.
 *
 * @cd - crypt device handle
 * @keyslot - requested keyslot or CRYPT_ANY_SLOT
 * @volume_key - provided volume key or NULL if used after crypt_format
 * @volume_key_size - size of @volume_key
 * @passphrase - passphrase for new keyslot, NULL for query
 * @passphrase_size - size of @passphrase
 */
int crypt_keyslot_add_by_volume_key(struct crypt_device *cd,
	int keyslot,
	const char *volume_key,
	size_t volume_key_size,
	const char *passphrase,
	size_t passphrase_size);

/**
 * Destroy (and disable) key slot
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - crypt device handle
 * @keyslot - requested key slot to destroy
 *
 * Note that there is no passphrase verification used.
 */
int crypt_keyslot_destroy(struct crypt_device *cd, int keyslot);

/**
 * Activation flags
 */
#define CRYPT_ACTIVATE_READONLY (1 << 0)
#define CRYPT_ACTIVATE_NO_UUID  (1 << 1)

/**
 * Activate device or check passphrase
 *
 * Returns unlocked key slot number or negative errno otherwise.
 *
 * @cd - crypt device handle
 * @name - name of device to create, if NULL only check passphrase
 * @keyslot - requested keyslot to check or CRYPT_ANY_SLOT
 * @passphrase - passphrase used to unlock volume key, NULL for query
 * @passphrase_size - size of @passphrase
 * @flags - activation flags
 */
int crypt_activate_by_passphrase(struct crypt_device *cd,
	const char *name,
	int keyslot,
	const char *passphrase,
	size_t passphrase_size,
	uint32_t flags);

/**
 * Activate device or check using key file
 *
 * Returns unlocked key slot number or negative errno otherwise.
 *
 * @cd - crypt device handle
 * @name - name of device to create, if NULL only check keyfile
 * @keyslot - requested keyslot to check or CRYPT_ANY_SLOT
 * @keyfile - key file used to unlock volume key
 * @keyfile_size - number of bytes to read from @keyfile, 0 is unlimited
 * @flags - activation flags
 */
int crypt_activate_by_keyfile(struct crypt_device *cd,
	const char *name,
	int keyslot,
	const char *keyfile,
	size_t keyfile_size,
	uint32_t flags);

/**
 * Activate device using provided volume key
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - crypt device handle
 * @name - name of device to create, if NULL only check volume key
 * @volume_key - provided volume key
 * @volume_key_size - size of @volume_key
 * @flags - activation flags
 */
int crypt_activate_by_volume_key(struct crypt_device *cd,
	const char *name,
	const char *volume_key,
	size_t volume_key_size,
	uint32_t flags);

/**
 * Deactivate crypt device
 *
 * @cd - crypt device handle, can be NULL
 * @name - name of device to deactivate
  */
int crypt_deactivate(struct crypt_device *cd, const char *name);

/**
 * Get volume key from of crypt device
 *
 * Returns unlocked key slot number or negative errno otherwise.
 *
 * @cd - crypt device handle
 * @keyslot - use this keyslot or CRYPT_ANY_SLOT
 * @volume_key - buffer for volume key
 * @volume_key_size - on input, size of buffer @volume_key,
 *                    on output size of @volume_key
 * @passphrase - passphrase used to unlock volume key, NULL for query
 * @passphrase_size - size of @passphrase
 */
int crypt_volume_key_get(struct crypt_device *cd,
	int keyslot,
	char *volume_key,
	size_t *volume_key_size,
	const char *passphrase,
	size_t passphrase_size);

/**
 * Verify that provided volume key is valid for crypt device
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - crypt device handle
 * @volume_key - provided volume key
 * @volume_key_size - size of @volume_key
 */
int crypt_volume_key_verify(struct crypt_device *cd,
	const char *volume_key,
	size_t volume_key_size);

/**
 * Get status info about device name
 *
 * Returns value defined by crypt_status_info.
 *
 * @cd - crypt device handle, can be NULL
 * @name -crypt device name
 *
 * CRYPT_INACTIVE - no such mapped device
 * CRYPT_ACTIVE - device is active
 * CRYPT_BUSY - device is active and has open count > 0
 */
typedef enum {
	CRYPT_INVALID,
	CRYPT_INACTIVE,
	CRYPT_ACTIVE,
	CRYPT_BUSY
} crypt_status_info;
crypt_status_info crypt_status(struct crypt_device *cd, const char *name);

/**
 * Dump text-formatted information about crypt device to log output
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - crypt device handle, can be NULL
 */
int crypt_dump(struct crypt_device *cd);

/**
 * Various crypt device info functions
 *
 * @cd - crypt device handle
 *
 * cipher - used cipher, e.g. "aes" or NULL otherwise
 * cipher_mode - used cipher mode including IV, e.g. "xts-plain" or NULL otherwise
 * uuid - device UUID or NULL if not set
 * data_offset - device offset in sectors where real data starts on underlying device)
 * volume_key_size - size (in bytes) of volume key for crypt device
 */
const char *crypt_get_cipher(struct crypt_device *cd);
const char *crypt_get_cipher_mode(struct crypt_device *cd);
const char *crypt_get_uuid(struct crypt_device *cd);
uint64_t crypt_get_data_offset(struct crypt_device *cd);
int crypt_get_volume_key_size(struct crypt_device *cd);

/**
 * Get information about particular key slot
 *
 * Returns value defined by crypt_keyslot_info.
 *
 * @cd - crypt device handle
 * @keyslot - requested keyslot to check or CRYPT_ANY_SLOT
 */
typedef enum {
	CRYPT_SLOT_INVALID,
	CRYPT_SLOT_INACTIVE,
	CRYPT_SLOT_ACTIVE,
	CRYPT_SLOT_ACTIVE_LAST
} crypt_keyslot_info;
crypt_keyslot_info crypt_keyslot_status(struct crypt_device *cd, int keyslot);

/**
 * Backup header and keyslots to file
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - crypt device handle
 * @requested_type - type of header to backup
 * @backup_file - file to backup header to
 */
int crypt_header_backup(struct crypt_device *cd,
	const char *requested_type,
	const char *backup_file);

/**
 * Restore header and keyslots from backup file
 *
 * Returns 0 on success or negative errno value otherwise.
 *
 * @cd - crypt device handle
 * @requested_type - type of header to restore
 * @backup_file - file to restore header from
 */
int crypt_header_restore(struct crypt_device *cd,
	const char *requested_type,
	const char *backup_file);

/**
 * Receives last reported error
 *
 * @buf - buffef for message
 * @size - size of buffer
 *
 * Note that this is old API function using global context.
 * All error messages are reported also through log callback.
 */
void crypt_get_error(char *buf, size_t size);

/**
 * Get directory where mapped crypt devices are created
 */
const char *crypt_get_dir(void);

/**
 * Set library debug level
 */
#define CRYPT_DEBUG_ALL  -1
#define CRYPT_DEBUG_NONE  0
void crypt_set_debug_level(int level);

/**
 * OLD DEPRECATED API **********************************
 *
 * Provided only for backward compatibility.
 */

struct interface_callbacks {
    int (*yesDialog)(char *msg);
    void (*log)(int level, char *msg);
};

#define	CRYPT_FLAG_VERIFY	        (1 << 0)
#define CRYPT_FLAG_READONLY	        (1 << 1)
#define	CRYPT_FLAG_VERIFY_IF_POSSIBLE	(1 << 2)
#define	CRYPT_FLAG_VERIFY_ON_DELKEY	(1 << 3)
#define	CRYPT_FLAG_NON_EXCLUSIVE_ACCESS	(1 << 4)

struct crypt_options {
	const char	*name;
	const char	*device;

	const char	*cipher;
	const char	*hash;

	const char	*passphrase;
	int		passphrase_fd;
	const char	*key_file;
	const char	*new_key_file;
	int		key_size;

	unsigned int	flags;
	int 	        key_slot;

	uint64_t	size;
	uint64_t	offset;
	uint64_t	skip;
	uint64_t        iteration_time;
	uint64_t	timeout;

	uint64_t	align_payload;
	int             tries;

	struct interface_callbacks *icb;
};

int crypt_create_device(struct crypt_options *options);
int crypt_update_device(struct crypt_options *options);
int crypt_resize_device(struct crypt_options *options);
int crypt_query_device(struct crypt_options *options);
int crypt_remove_device(struct crypt_options *options);
int crypt_luksFormat(struct crypt_options *options);
int crypt_luksOpen(struct crypt_options *options);
int crypt_luksKillSlot(struct crypt_options *options);
int crypt_luksRemoveKey(struct crypt_options *options);
int crypt_luksAddKey(struct crypt_options *options);
int crypt_luksUUID(struct crypt_options *options);
int crypt_isLuks(struct crypt_options *options);
int crypt_luksDump(struct crypt_options *options);

void crypt_put_options(struct crypt_options *options);

#ifdef __cplusplus
}
#endif
#endif /* _LIBCRYPTSETUP_H */
