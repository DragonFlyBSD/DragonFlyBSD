#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>

#include "libcryptsetup.h"
#include "luks.h"
#include "internal.h"

struct crypt_device {
	char *type;

	char *device;
	struct luks_masterkey *volume_key;
	uint64_t timeout;
	uint64_t iteration_time;
	int tries;
	int password_verify;

	/* used in CRYPT_LUKS1 */
	struct luks_phdr hdr;
	uint64_t PBKDF2_per_sec;

	/* used in CRYPT_PLAIN */
	struct crypt_params_plain plain_hdr;
	char *plain_cipher;
	char *plain_cipher_mode;
	char *plain_uuid;

	/* callbacks definitions */
	void (*log)(int level, const char *msg, void *usrptr);
	void *log_usrptr;
	int (*confirm)(const char *msg, void *usrptr);
	void *confirm_usrptr;
	int (*password)(const char *msg, char *buf, size_t length, void *usrptr);
	void *password_usrptr;
};

/* Log helper */
static void (*_default_log)(int level, const char *msg, void *usrptr) = NULL;
static int _debug_level = 0;

void crypt_set_debug_level(int level)
{
	_debug_level = level;
}

int crypt_get_debug_level()
{
	return _debug_level;
}

void crypt_log(struct crypt_device *cd, int level, const char *msg)
{
	if (cd && cd->log)
		cd->log(level, msg, cd->log_usrptr);
	else if (_default_log)
		_default_log(level, msg, NULL);
}

void logger(struct crypt_device *cd, int level, const char *file,
	    int line, const char *format, ...)
{
	va_list argp;
	char *target = NULL;

	va_start(argp, format);

	if (vasprintf(&target, format, argp) > 0) {
		if (level >= 0) {
			crypt_log(cd, level, target);
#ifdef CRYPT_DEBUG
		} else if (_debug_level)
			printf("# %s:%d %s\n", file ?: "?", line, target);
#else
		} else if (_debug_level)
			printf("# %s\n", target);
#endif
	}

	va_end(argp);
	free(target);
}

/*
 * Password processing behaviour matrix of process_key
 *
 * from binary file: check if there is sufficently large key material
 * interactive & from fd: hash if requested, otherwise crop or pad with '0'
 */
static char *process_key(struct crypt_device *cd, const char *hash_name,
			 const char *key_file, size_t key_size,
			 const char *pass, size_t passLen)
{
	char *key = safe_alloc(key_size);
	memset(key, 0, key_size);

	/* key is coming from binary file */
	if (key_file && strcmp(key_file, "-")) {
		if(passLen < key_size) {
			log_err(cd, _("Cannot not read %d bytes from key file %s.\n"),
				key_size, key_file);
			safe_free(key);
			return NULL;
		}
		memcpy(key, pass, key_size);
		return key;
	}

	/* key is coming from tty, fd or binary stdin */
	if (hash_name) {
		if (hash(NULL, hash_name, key, key_size, pass, passLen) < 0) {
			log_err(cd, _("Key processing error (using hash algorithm %s).\n"),
				hash_name);
			safe_free(key);
			return NULL;
		}
	} else if (passLen > key_size) {
		memcpy(key, pass, key_size);
	} else {
		memcpy(key, pass, passLen);
	}

	return key;
}

int parse_into_name_and_mode(const char *nameAndMode, char *name, char *mode)
{
/* Token content stringification, see info cpp/stringification */
#define str(s) #s
#define xstr(s) str(s)
#define scanpattern1 "%" xstr(LUKS_CIPHERNAME_L) "[^-]-%" xstr(LUKS_CIPHERMODE_L)  "s"
#define scanpattern2 "%" xstr(LUKS_CIPHERNAME_L) "[^-]"

	int r;

	if(sscanf(nameAndMode,scanpattern1, name, mode) != 2) {
		if((r = sscanf(nameAndMode,scanpattern2,name)) == 1)
			strncpy(mode,"cbc-plain",10);
		else
			return -EINVAL;
	}

	return 0;

#undef scanpattern1
#undef scanpattern2
#undef str
#undef xstr
}

static int isPLAIN(const char *type)
{
	return (type && !strcmp(CRYPT_PLAIN, type));
}

static int isLUKS(const char *type)
{
	return (type && !strcmp(CRYPT_LUKS1, type));
}

/* keyslot helpers */
static int keyslot_verify_or_find_empty(struct crypt_device *cd, int *keyslot)
{
	if (*keyslot == CRYPT_ANY_SLOT) {
		*keyslot = LUKS_keyslot_find_empty(&cd->hdr);
		if (*keyslot < 0) {
			log_err(cd, _("All key slots full.\n"));
			return -EINVAL;
		}
	}

	switch (LUKS_keyslot_info(&cd->hdr, *keyslot)) {
		case CRYPT_SLOT_INVALID:
			log_err(cd, _("Key slot %d is invalid, please select between 0 and %d.\n"),
				*keyslot, LUKS_NUMKEYS - 1);
			return -EINVAL;
		case CRYPT_SLOT_INACTIVE:
			break;
		default:
			log_err(cd, _("Key slot %d is full, please select another one.\n"),
				*keyslot);
			return -EINVAL;
	}

	return 0;
}

static int verify_other_keyslot(struct crypt_device *cd,
				const char *key_file,
				unsigned int flags,
				int keyIndex)
{
	struct luks_masterkey *mk;
	crypt_keyslot_info ki;
	int openedIndex;
	char *password = NULL;
	unsigned int passwordLen;

	get_key(_("Enter any remaining LUKS passphrase: "), &password,
		&passwordLen, 0, key_file, cd->timeout, flags, cd);
	if(!password)
		return -EINVAL;

	ki = crypt_keyslot_status(cd, keyIndex);
	if (ki == CRYPT_SLOT_ACTIVE) /* Not last slot */
		LUKS_keyslot_set(&cd->hdr, keyIndex, 0);

	openedIndex = LUKS_open_key_with_hdr(cd->device, CRYPT_ANY_SLOT,
					     password, passwordLen,
					     &cd->hdr, &mk, cd);

	if (ki == CRYPT_SLOT_ACTIVE)
		LUKS_keyslot_set(&cd->hdr, keyIndex, 1);
	LUKS_dealloc_masterkey(mk);
	safe_free(password);

	if (openedIndex < 0)
		return -EPERM;

	log_verbose(cd, _("Key slot %d verified.\n"), openedIndex);
	return 0;
}

static int find_keyslot_by_passphrase(struct crypt_device *cd,
				      const char *key_file,
				      unsigned int flags,
				      char *message)
{
	struct luks_masterkey *mk;
	char *password = NULL;
	unsigned int passwordLen;
	int keyIndex;

	get_key(message,&password,&passwordLen, 0, key_file,
		cd->timeout, flags, cd);
	if(!password)
		return -EINVAL;

	keyIndex = LUKS_open_key_with_hdr(cd->device, CRYPT_ANY_SLOT, password,
					  passwordLen, &cd->hdr, &mk, cd);
	LUKS_dealloc_masterkey(mk);
	safe_free(password);

	return keyIndex;
}

static int device_check_and_adjust(struct crypt_device *cd,
				   const char *device,
				   uint64_t *size, uint64_t *offset,
				   int *read_only)
{
	struct device_infos infos;

	if (!device || get_device_infos(device, &infos, cd) < 0) {
		log_err(cd, _("Cannot get info about device %s.\n"),
			device ?: "[none]");
		return -ENOTBLK;
	}

	if (!*size) {
		*size = infos.size;
		if (!*size) {
			log_err(cd, _("Device %s has zero size.\n"), device);
			return -ENOTBLK;
		}
		if (*size < *offset) {
			log_err(cd, _("Device %s is too small.\n"), device);
			return -EINVAL;
		}
		*size -= *offset;
	}

	if (infos.readonly)
		*read_only = 1;

	log_dbg("Calculated device size is %" PRIu64 " sectors (%s), offset %" PRIu64 ".",
		*size, *read_only ? "RO" : "RW", *offset);
	return 0;
}

static int luks_remove_helper(struct crypt_device *cd,
			      int key_slot,
			      const char *other_key_file,
			      const char *key_file,
			      int verify)
{
	crypt_keyslot_info ki;
	int r = -EINVAL;

	if (key_slot == CRYPT_ANY_SLOT) {
		key_slot = find_keyslot_by_passphrase(cd, key_file, 0,
				_("Enter LUKS passphrase to be deleted: "));
		if(key_slot < 0) {
			r = -EPERM;
			goto out;
		}

		log_std(cd, _("key slot %d selected for deletion.\n"), key_slot);
	}

	ki = crypt_keyslot_status(cd, key_slot);
	if (ki == CRYPT_SLOT_INVALID) {
		log_err(cd, _("Key slot %d is invalid, please select between 0 and %d.\n"),
			key_slot, LUKS_NUMKEYS - 1);
		r = -EINVAL;
		goto out;
	}
	if (ki <= CRYPT_SLOT_INACTIVE) {
		log_err(cd, _("Key %d not active. Can't wipe.\n"), key_slot);
		r = -EINVAL;
		goto out;
	}

	if (ki == CRYPT_SLOT_ACTIVE_LAST && cd->confirm &&
	    !(cd->confirm(_("This is the last keyslot."
			    " Device will become unusable after purging this key."),
			 cd->confirm_usrptr))) {
		r = -EINVAL;
		goto out;
	}

	if(verify)
		r = verify_other_keyslot(cd, other_key_file, 0, key_slot);
	else
		r = 0;

	if (!r)
		r = crypt_keyslot_destroy(cd, key_slot);
out:
	return (r < 0) ? r : 0;
}

static int create_device_helper(struct crypt_device *cd,
				const char *name,
				const char *hash,
				const char *cipher,
				const char *cipher_mode,
				const char *key_file,
				const char *key,
				unsigned int keyLen,
				int key_size,
				uint64_t size,
				uint64_t skip,
				uint64_t offset,
				const char *uuid,
				int read_only,
				unsigned int flags,
				int reload)
{
	crypt_status_info ci;
	char *dm_cipher = NULL;
	char *processed_key = NULL;
	int r;

	ci = crypt_status(cd, name);
	if (ci == CRYPT_INVALID)
		return -EINVAL;

	if (reload && ci < CRYPT_ACTIVE)
		return -EINVAL;

	if (!reload && ci >= CRYPT_ACTIVE) {
		log_err(cd, _("Device %s already exists.\n"), name);
		return -EEXIST;
	}

	if (key_size < 0 || key_size > 1024) {
		log_err(cd, _("Invalid key size %d.\n"), key_size);
		return -EINVAL;
	}

	r = device_check_and_adjust(cd, cd->device, &size, &offset, &read_only);
	if (r)
		return r;

	if (cipher_mode && asprintf(&dm_cipher, "%s-%s", cipher, cipher_mode) < 0)
		return -ENOMEM;

	processed_key = process_key(cd, hash, key_file, key_size, key, keyLen);
	if (!processed_key)
		return -ENOENT;

	r = dm_create_device(name, cd->device, dm_cipher ?: cipher, cd->type, uuid, size, skip, offset,
			     key_size, processed_key, read_only, reload);

	free(dm_cipher);
	safe_free(processed_key);
	return r;
}

static int open_from_hdr_and_mk(struct crypt_device *cd,
				struct luks_masterkey *mk,
				const char *name,
				uint32_t flags)
{
	uint64_t size, offset;
	char *cipher;
	int read_only, no_uuid, r;

	size = 0;
	offset = crypt_get_data_offset(cd);
	read_only = flags & CRYPT_ACTIVATE_READONLY;
	no_uuid = flags & CRYPT_ACTIVATE_NO_UUID;

	r = device_check_and_adjust(cd, cd->device, &size, &offset, &read_only);
	if (r)
		return r;

	if (asprintf(&cipher, "%s-%s", crypt_get_cipher(cd),
		     crypt_get_cipher_mode(cd)) < 0)
		r = -ENOMEM;
	else
		r = dm_create_device(name, cd->device, cipher, cd->type,
				     no_uuid ? NULL : crypt_get_uuid(cd),
				     size, 0, offset, mk->keyLength, mk->key,
				     read_only, 0);
	free(cipher);
	return r;
}

static void log_wrapper(int level, const char *msg, void *usrptr)
{
	void (*xlog)(int level, char *msg) = usrptr;
	xlog(level, (char *)msg);
}

static int yesDialog_wrapper(const char *msg, void *usrptr)
{
	int (*xyesDialog)(char *msg) = usrptr;
	return xyesDialog((char*)msg);
}

int crypt_confirm(struct crypt_device *cd, const char *msg)
{
	if (!cd || !cd->confirm)
		return 1;
	else
		return cd->confirm(msg, cd->confirm_usrptr);
}

static void key_from_terminal(struct crypt_device *cd, char *msg, char **key,
			      unsigned int *key_len, int force_verify)
{
	int r, flags = 0;

	if (cd->password) {
		*key = safe_alloc(MAX_TTY_PASSWORD_LEN);
		if (*key)
			return;
		r = cd->password(msg, *key, (size_t)key_len, cd->password_usrptr);
		if (r < 0) {
			safe_free(*key);
			*key = NULL;
		} else
			*key_len = r;
	} else {
		if (force_verify || cd->password_verify)
			flags |= CRYPT_FLAG_VERIFY_IF_POSSIBLE;
		get_key(msg, key, key_len, 0, NULL, cd->timeout, flags, cd);
	}
}

static int volume_key_by_terminal_passphrase(struct crypt_device *cd, int keyslot,
					     struct luks_masterkey **mk)
{
	char *prompt = NULL, *passphrase_read = NULL;
	unsigned int passphrase_size_read;
	int r = -EINVAL, tries = cd->tries;

	if(asprintf(&prompt, _("Enter passphrase for %s: "), cd->device) < 0)
		return -ENOMEM;

	*mk = NULL;
	do {
		if (*mk)
			LUKS_dealloc_masterkey(*mk);
		*mk = NULL;

		key_from_terminal(cd, prompt, &passphrase_read,
				  &passphrase_size_read, 0);
		if(!passphrase_read) {
			r = -EINVAL;
			break;
		}

		r = LUKS_open_key_with_hdr(cd->device, keyslot, passphrase_read,
					   passphrase_size_read, &cd->hdr, mk, cd);
		safe_free(passphrase_read);
		passphrase_read = NULL;
	} while (r == -EPERM && (--tries > 0));

	if (r < 0 && *mk) {
		LUKS_dealloc_masterkey(*mk);
		*mk = NULL;
	}
	free(prompt);

	return r;

}

static void key_from_file(struct crypt_device *cd, char *msg,
			  char **key, unsigned int *key_len,
			  const char *key_file, size_t key_size)
{
	get_key(msg, key, key_len, key_size, key_file, cd->timeout, 0, cd);
}

static int _crypt_init(struct crypt_device **cd,
		       const char *type,
		       struct crypt_options *options,
		       int load, int need_dm)
{
	int init_by_name, r;

	/* if it is plain device and mapping table is being reloaded
	initialize it by name*/
	init_by_name = (type && !strcmp(type, CRYPT_PLAIN) && load);

	/* Some of old API calls do not require DM in kernel,
	   fake initialisation by initialise it with kernel_check disabled */
	if (!need_dm)
		(void)dm_init(NULL, 0);
	if (init_by_name)
		r = crypt_init_by_name(cd, options->name);
	else
		r = crypt_init(cd, options->device);
	if (!need_dm)
		dm_exit();

	if (r)
		return -EINVAL;

	crypt_set_log_callback(*cd, log_wrapper, options->icb->log);
	crypt_set_confirm_callback(*cd, yesDialog_wrapper, options->icb->yesDialog);

	crypt_set_timeout(*cd, options->timeout);
	crypt_set_password_retry(*cd, options->tries);
	crypt_set_iterarion_time(*cd, options->iteration_time ?: 1000);
	crypt_set_password_verify(*cd, options->flags & CRYPT_FLAG_VERIFY);

	if (load && !init_by_name)
		r = crypt_load(*cd, type, NULL);

	if (!r && type && !(*cd)->type) {
		(*cd)->type = strdup(type);
		if (!(*cd)->type)
			r = -ENOMEM;
	}

	if (r)
		crypt_free(*cd);

	return r;
}

void crypt_set_log_callback(struct crypt_device *cd,
	void (*log)(int level, const char *msg, void *usrptr),
	void *usrptr)
{
	if (!cd)
		_default_log = log;
	else {
		cd->log = log;
		cd->log_usrptr = usrptr;
	}
}

void crypt_set_confirm_callback(struct crypt_device *cd,
	int (*confirm)(const char *msg, void *usrptr),
	void *usrptr)
{
	cd->confirm = confirm;
	cd->confirm_usrptr = usrptr;
}

void crypt_set_password_callback(struct crypt_device *cd,
	int (*password)(const char *msg, char *buf, size_t length, void *usrptr),
	void *usrptr)
{
	cd->password = password;
	cd->password_usrptr = usrptr;
}

/* OPTIONS: name, cipher, device, hash, key_file, key_size, key_slot,
 *          offset, size, skip, timeout, tries, passphrase_fd (ignored),
 *          flags, icb */
static int crypt_create_and_update_device(struct crypt_options *options, int update)
{
	struct crypt_device *cd = NULL;
	char *key = NULL;
	unsigned int keyLen;
	int r;

	r = _crypt_init(&cd, CRYPT_PLAIN, options, 0, 1);
	if (r)
		return r;

	get_key(_("Enter passphrase: "), &key, &keyLen, options->key_size,
		options->key_file, cd->timeout, options->flags, cd);
	if (!key)
		r = -ENOENT;
	else
		r = create_device_helper(cd, options->name, options->hash,
			options->cipher, NULL, options->key_file, key, keyLen,
			options->key_size, options->size, options->skip,
			options->offset, NULL, options->flags & CRYPT_FLAG_READONLY,
			options->flags, update);

	safe_free(key);
	crypt_free(cd);
	return r;
}

int crypt_create_device(struct crypt_options *options)
{
	return crypt_create_and_update_device(options, 0);
}

int crypt_update_device(struct crypt_options *options)
{
	return crypt_create_and_update_device(options, 1);
}

/* OPTIONS: name, size, icb */
int crypt_resize_device(struct crypt_options *options)
{
	struct crypt_device *cd = NULL;
	char *device = NULL, *cipher = NULL, *uuid = NULL, *key = NULL;
	char *type = NULL;
	uint64_t size, skip, offset;
	int key_size, read_only, r;

	log_dbg("Resizing device %s to %" PRIu64 " sectors.", options->name, options->size);

	if (dm_init(NULL, 1) < 0)
		return -ENOSYS;

	r = dm_query_device(options->name, &device, &size, &skip, &offset,
			    &cipher, &key_size, &key, &read_only, NULL, &uuid);
	if (r < 0) {
		log_err(NULL, _("Device %s is not active.\n"), options->name);
		goto out;
	}

	/* Try to determine type of device from UUID */
	type = CRYPT_PLAIN;
	if (uuid) {
		if (!strncmp(uuid, CRYPT_PLAIN, strlen(CRYPT_PLAIN))) {
			type = CRYPT_PLAIN;
			free (uuid);
			uuid = NULL;
		} else if (!strncmp(uuid, CRYPT_LUKS1, strlen(CRYPT_LUKS1)))
			type = CRYPT_LUKS1;
	}

	if (!options->device)
		options->device = device;

	r = _crypt_init(&cd, type, options, 1, 1);
	if (r)
		goto out;

	size = options->size;
	r = device_check_and_adjust(cd, device, &size, &offset, &read_only);
	if (r)
		goto out;

	r = dm_create_device(options->name, device, cipher, type,
			     crypt_get_uuid(cd), size, skip, offset,
			     key_size, key, read_only, 1);
out:
	safe_free(key);
	free(cipher);
	if (options->device == device)
		options->device = NULL;
	free(device);
	free(uuid);
	crypt_free(cd);
	dm_exit();
	return r;
}

/* OPTIONS: name, icb */
int crypt_query_device(struct crypt_options *options)
{
	int read_only, r;

	log_dbg("Query device %s.", options->name);

	if (dm_init(NULL, 1) < 0)
		return -ENOSYS;

	r = dm_status_device(options->name);
	if (r == -ENODEV) {
		dm_exit();
		return 0;
	}

	r = dm_query_device(options->name, (char **)&options->device, &options->size,
			    &options->skip, &options->offset, (char **)&options->cipher,
			    &options->key_size, NULL, &read_only, NULL, NULL);

	dm_exit();
	if (r < 0)
		return r;

	if (read_only)
		options->flags |= CRYPT_FLAG_READONLY;

	options->flags |= CRYPT_FLAG_FREE_DEVICE;
	options->flags |= CRYPT_FLAG_FREE_CIPHER;

	return 1;
}

/* OPTIONS: name, icb */
int crypt_remove_device(struct crypt_options *options)
{
	struct crypt_device *cd = NULL;
	int r;

	r = crypt_init_by_name(&cd, options->name);
	if (r == 0)
		r = crypt_deactivate(cd, options->name);

	crypt_free(cd);
	return r;

}

/* OPTIONS: device, cipher, hash, align_payload, key_size (master key), key_slot
 *          new_key_file, iteration_time, timeout, flags, icb */
int crypt_luksFormat(struct crypt_options *options)
{
	char cipherName[LUKS_CIPHERNAME_L];
	char cipherMode[LUKS_CIPHERMODE_L];
	char *password=NULL;
	unsigned int passwordLen;
	struct crypt_device *cd = NULL;
	struct crypt_params_luks1 cp = {
		.hash = options->hash,
		.data_alignment = options->align_payload
	};
	int r;

	r = parse_into_name_and_mode(options->cipher, cipherName, cipherMode);
	if(r < 0) {
		log_err(cd, _("No known cipher specification pattern detected.\n"));
		return r;
	}

	if ((r = _crypt_init(&cd, CRYPT_LUKS1, options, 0, 1)))
		return r;

	if (options->key_slot >= LUKS_NUMKEYS && options->key_slot != CRYPT_ANY_SLOT) {
		log_err(cd, _("Key slot %d is invalid, please select between 0 and %d.\n"),
			options->key_slot, LUKS_NUMKEYS - 1);
		r = -EINVAL;
		goto out;
	}

	get_key(_("Enter LUKS passphrase: "), &password, &passwordLen, 0,
		options->new_key_file, options->timeout, options->flags, cd);

	if(!password) {
		r = -EINVAL;
		goto out;
	}

	r = crypt_format(cd, CRYPT_LUKS1, cipherName, cipherMode,
			 NULL, NULL, options->key_size, &cp);
	if (r < 0)
		goto out;

	/* Add keyslot using internally stored volume key generated during format */
	r = crypt_keyslot_add_by_volume_key(cd, options->key_slot, NULL, 0,
					    password, passwordLen);
out:
	crypt_free(cd);
	safe_free(password);
	return (r < 0) ? r : 0;
}

/* OPTIONS: name, device, key_size, key_file, timeout, tries, flags, icb */
int crypt_luksOpen(struct crypt_options *options)
{
	struct crypt_device *cd = NULL;
	uint32_t flags = 0;
	int r;

	if (!options->name)
		return -EINVAL;

	r = _crypt_init(&cd, CRYPT_LUKS1, options, 1, 1);
	if (r)
		return r;

	if (options->flags & CRYPT_FLAG_READONLY)
		flags |= CRYPT_ACTIVATE_READONLY;

	if (options->flags & CRYPT_FLAG_NON_EXCLUSIVE_ACCESS)
		flags |= CRYPT_ACTIVATE_NO_UUID;

	if (options->key_file)
		r = crypt_activate_by_keyfile(cd, options->name,
			CRYPT_ANY_SLOT, options->key_file, options->key_size,
			flags);
	else
		r = crypt_activate_by_passphrase(cd, options->name,
			CRYPT_ANY_SLOT, options->passphrase,
			options->passphrase ? strlen(options->passphrase) : 0,
			flags);

	crypt_free(cd);
	return (r < 0) ? r : 0;
}

/* OPTIONS: device, keys_slot, key_file, timeout, flags, icb */
int crypt_luksKillSlot(struct crypt_options *options)
{
	struct crypt_device *cd = NULL;
	int r;

	r = _crypt_init(&cd, CRYPT_LUKS1, options, 1, 1);
	if (r)
		return r;

	r = luks_remove_helper(cd, options->key_slot, options->key_file, NULL,
			       options->flags & CRYPT_FLAG_VERIFY_ON_DELKEY);

	crypt_free(cd);
	return (r < 0) ? r : 0;
}

/* OPTIONS: device, new_key_file, key_file, timeout, flags, icb */
int crypt_luksRemoveKey(struct crypt_options *options)
{
	struct crypt_device *cd = NULL;
	int r;

	r = _crypt_init(&cd, CRYPT_LUKS1, options, 1, 1);
	if (r)
		return r;

	r = luks_remove_helper(cd, CRYPT_ANY_SLOT, options->key_file, options->new_key_file,
			       options->flags & CRYPT_FLAG_VERIFY_ON_DELKEY);

	crypt_free(cd);
	return (r < 0) ? r : 0;
}


/* OPTIONS: device, new_key_file, key_file, key_slot, flags,
            iteration_time, timeout, icb */
int crypt_luksAddKey(struct crypt_options *options)
{
	struct crypt_device *cd = NULL;
	int r = -EINVAL;

	r = _crypt_init(&cd, CRYPT_LUKS1, options, 1, 1);
	if (r)
		return r;

	if (options->key_file || options->new_key_file)
		r = crypt_keyslot_add_by_keyfile(cd, options->key_slot,
						 options->key_file, 0,
						 options->new_key_file, 0);
	else
		r = crypt_keyslot_add_by_passphrase(cd, options->key_slot,
						    NULL, 0, NULL, 0);

	crypt_free(cd);
	return (r < 0) ? r : 0;
}

/* OPTIONS: device, icb */
int crypt_luksUUID(struct crypt_options *options)
{
	struct crypt_device *cd = NULL;
	char *uuid;
	int r;

	r = _crypt_init(&cd, CRYPT_LUKS1, options, 1, 0);
	if (r)
		return r;

	uuid = (char *)crypt_get_uuid(cd);
	log_std(cd, uuid ?: "");
	log_std(cd, "\n");
	crypt_free(cd);
	return 0;
}

/* OPTIONS: device, icb */
int crypt_isLuks(struct crypt_options *options)
{
	struct crypt_device *cd = NULL;
	int r;

	log_dbg("Check device %s for LUKS header.", options->device);

	if (init_crypto()) {
		log_err(cd, _("Cannot initialize crypto backend.\n"));
		return -ENOSYS;
	}

	r = crypt_init(&cd, options->device);
	if (r < 0)
		return -EINVAL;

	/* Do print fail here, no need to crypt_load() */
	r = LUKS_read_phdr(cd->device, &cd->hdr, 0, cd) ? -EINVAL : 0;

	crypt_free(cd);
	return r;
}

/* OPTIONS: device, icb */
int crypt_luksDump(struct crypt_options *options)
{
	struct crypt_device *cd = NULL;
	int r;

	r = _crypt_init(&cd, CRYPT_LUKS1, options, 1, 0);
	if(r < 0)
		return r;

	r = crypt_dump(cd);

	crypt_free(cd);
	return 0;
}

void crypt_get_error(char *buf, size_t size)
{
	const char *error = get_error();

	if (!buf || size < 1)
		set_error(NULL);
	else if (error) {
		strncpy(buf, error, size - 1);
		buf[size - 1] = '\0';
		set_error(NULL);
	} else
		buf[0] = '\0';
}

void crypt_put_options(struct crypt_options *options)
{
	if (options->flags & CRYPT_FLAG_FREE_DEVICE) {
		free((char *)options->device);
		options->device = NULL;
		options->flags &= ~CRYPT_FLAG_FREE_DEVICE;
	}
	if (options->flags & CRYPT_FLAG_FREE_CIPHER) {
		free((char *)options->cipher);
		options->cipher = NULL;
		options->flags &= ~CRYPT_FLAG_FREE_CIPHER;
	}
}

const char *crypt_get_dir(void)
{
	return dm_get_dir();
}

/////////////////////////////////
//
// New API
//

int crypt_init(struct crypt_device **cd, const char *device)
{
	struct crypt_device *h = NULL;

	if (!cd)
		return -EINVAL;

	log_dbg("Allocating crypt device %s context.", device);

	if (device && !device_ready(NULL, device, O_RDONLY))
		return -ENOTBLK;

	if (!(h = malloc(sizeof(struct crypt_device))))
		return -ENOMEM;

	memset(h, 0, sizeof(*h));

	if (device) {
		h->device = strdup(device);
		if (!h->device) {
			free(h);
			return -ENOMEM;
		}
	} else
		h->device = NULL;

	if (dm_init(h, 1) < 0) {
		free(h);
		return -ENOSYS;
	}

	h->iteration_time = 1000;
	h->password_verify = 0;
	h->tries = 3;
	*cd = h;
	return 0;
}

int crypt_init_by_name(struct crypt_device **cd, const char *name)
{
	crypt_status_info ci;
	char *device = NULL;
	int r;

	log_dbg("Allocating crypt device context by device %s.", name);

	ci = crypt_status(NULL, name);
	if (ci == CRYPT_INVALID)
		return -ENODEV;

	if (ci < CRYPT_ACTIVE) {
		log_err(NULL, _("Device %s is not active.\n"), name);
		return -ENODEV;
	}

	r = dm_query_device(name, &device, NULL, NULL, NULL,
			    NULL, NULL, NULL, NULL, NULL, NULL);

	/* Underlying device disappeared but mapping still active */
	if (r >= 0 && !device)
		log_verbose(NULL, _("Underlying device for crypt device %s disappeared.\n"),
			    name);

	if (r >= 0)
		r = crypt_init(cd, device);

	free(device);
	return r;
}

static int _crypt_format_plain(struct crypt_device *cd,
			       const char *cipher,
			       const char *cipher_mode,
			       const char *uuid,
			       struct crypt_params_plain *params)
{
	if (!cipher || !cipher_mode) {
		log_err(cd, _("Invalid plain crypt parameters.\n"));
		return -EINVAL;
	}

	if (cd->volume_key->keyLength > 1024) {
		log_err(cd, _("Invalid key size.\n"));
		return -EINVAL;
	}

	cd->plain_cipher = strdup(cipher);
	cd->plain_cipher_mode = strdup(cipher_mode);

	if (uuid)
		cd->plain_uuid = strdup(uuid);

	if (params && params->hash)
		cd->plain_hdr.hash = strdup(params->hash);

	cd->plain_hdr.offset = params ? params->offset : 0;
	cd->plain_hdr.skip = params ? params->skip : 0;

	if (!cd->plain_cipher || !cd->plain_cipher_mode)
		return -ENOMEM;

	return 0;
}

static int _crypt_format_luks1(struct crypt_device *cd,
			       const char *cipher,
			       const char *cipher_mode,
			       const char *uuid,
			       struct crypt_params_luks1 *params)
{
	int r;
	unsigned long required_alignment = DEFAULT_ALIGNMENT;
	unsigned long alignment_offset = 0;

	if (!cd->device) {
		log_err(cd, _("Can't format LUKS without device.\n"));
		return -EINVAL;
	}

	if (params && params->data_alignment)
		required_alignment = params->data_alignment * SECTOR_SIZE;
	else
		get_topology_alignment(cd->device, &required_alignment,
				       &alignment_offset, DEFAULT_ALIGNMENT);

	r = LUKS_generate_phdr(&cd->hdr, cd->volume_key, cipher, cipher_mode,
			       (params && params->hash) ? params->hash : "sha1",
			       uuid, LUKS_STRIPES,
			       required_alignment / SECTOR_SIZE,
			       alignment_offset / SECTOR_SIZE,
			       cd->iteration_time, &cd->PBKDF2_per_sec, cd);
	if(r < 0)
		return r;

	/* Wipe first 8 sectors - fs magic numbers etc. */
	r = wipe_device_header(cd->device, 8);
	if(r < 0) {
		log_err(cd, _("Can't wipe header on device %s.\n"), cd->device);
		return r;
	}

	r = LUKS_write_phdr(cd->device, &cd->hdr, cd);

	return r;
}

int crypt_format(struct crypt_device *cd,
	const char *type,
	const char *cipher,
	const char *cipher_mode,
	const char *uuid,
	const char *volume_key,
	size_t volume_key_size,
	void *params)
{
	int r;

	log_dbg("Formatting device %s as type %s.", cd->device ?: "(none)", cd->type ?: "(none)");

	if (!type)
		return -EINVAL;

	/* Some hash functions need initialized gcrypt library */
	if (init_crypto()) {
		log_err(cd, _("Cannot initialize crypto backend.\n"));
		return -ENOSYS;
	}

	if (volume_key)
		cd->volume_key = LUKS_alloc_masterkey(volume_key_size, 
						      volume_key);
	else
		cd->volume_key = LUKS_generate_masterkey(volume_key_size);

	if(!cd->volume_key)
		return -ENOMEM;

	if (isPLAIN(type))
		r = _crypt_format_plain(cd, cipher, cipher_mode,
					uuid, params);
	else if (isLUKS(type))
		r = _crypt_format_luks1(cd, cipher, cipher_mode,
					uuid, params);
	else {
		/* FIXME: allow plugins here? */
		log_err(cd, _("Unknown crypt device type %s requested.\n"), type);
		r = -EINVAL;
	}

	if (!r && !(cd->type = strdup(type)))
		r = -ENOMEM;

	if (r < 0) {
		LUKS_dealloc_masterkey(cd->volume_key);
		cd->volume_key = NULL;
	}

	return r;
}

int crypt_load(struct crypt_device *cd,
	       const char *requested_type,
	       void *params)
{
	struct luks_phdr hdr;
	int r;

	log_dbg("Trying to load %s crypt type from device %s.",
		requested_type ?: "any", cd->device ?: "(none)");

	if (!cd->device)
		return -EINVAL;

	if (requested_type && !isLUKS(requested_type))
		return -EINVAL;

	/* Some hash functions need initialized gcrypt library */
	if (init_crypto()) {
		log_err(cd, _("Cannot initialize crypto backend.\n"));
		return -ENOSYS;
	}

	r = LUKS_read_phdr(cd->device, &hdr, 1, cd);

	if (!r) {
		memcpy(&cd->hdr, &hdr, sizeof(hdr));
		cd->type = strdup(requested_type);
		if (!cd->type)
			r = -ENOMEM;
	}

	return r;
}

int crypt_header_backup(struct crypt_device *cd,
			const char *requested_type,
			const char *backup_file)
{
	if ((requested_type && !isLUKS(requested_type)) || !backup_file)
		return -EINVAL;

	/* Some hash functions need initialized gcrypt library */
	if (init_crypto()) {
		log_err(cd, _("Cannot initialize crypto backend.\n"));
		return -ENOSYS;
	}

	log_dbg("Requested header backup of device %s (%s) to "
		"file %s.", cd->device, requested_type, backup_file);

	return LUKS_hdr_backup(backup_file, cd->device, &cd->hdr, cd);
}

int crypt_header_restore(struct crypt_device *cd,
			 const char *requested_type,
			 const char *backup_file)
{
	if (requested_type && !isLUKS(requested_type))
		return -EINVAL;

	/* Some hash functions need initialized gcrypt library */
	if (init_crypto()) {
		log_err(cd, _("Cannot initialize crypto backend.\n"));
		return -ENOSYS;
	}

	log_dbg("Requested header restore to device %s (%s) from "
		"file %s.", cd->device, requested_type, backup_file);

	return LUKS_hdr_restore(backup_file, cd->device, &cd->hdr, cd);
}

void crypt_free(struct crypt_device *cd)
{
	if (cd) {
		log_dbg("Releasing crypt device %s context.", cd->device);

		dm_exit();
		if (cd->volume_key)
			LUKS_dealloc_masterkey(cd->volume_key);

		free(cd->device);
		free(cd->type);

		/* used in plain device only */
		free((char*)cd->plain_hdr.hash);
		free(cd->plain_cipher);
		free(cd->plain_cipher_mode);
		free(cd->plain_uuid);

		free(cd);
	}
}

int crypt_suspend(struct crypt_device *cd,
		  const char *name)
{
	crypt_status_info ci;
	int r, suspended = 0;

	log_dbg("Suspending volume %s.", name);

	ci = crypt_status(NULL, name);
	if (ci < CRYPT_ACTIVE) {
		log_err(cd, _("Volume %s is not active.\n"), name);
		return -EINVAL;
	}

	if (!cd && dm_init(NULL, 1) < 0)
		return -ENOSYS;

	r = dm_query_device(name, NULL, NULL, NULL, NULL,
			    NULL, NULL, NULL, NULL, &suspended, NULL);
	if (r < 0)
		goto out;

	if (suspended) {
		log_err(cd, _("Volume %s is already suspended.\n"), name);
		r = -EINVAL;
		goto out;
	}

	r = dm_suspend_and_wipe_key(name);
	if (r == -ENOTSUP)
		log_err(cd, "Suspend is not supported for device %s.\n", name);
	else if (r)
		log_err(cd, "Error during suspending device %s.\n", name);
out:
	if (!cd)
		dm_exit();
	return r;
}

int crypt_resume_by_passphrase(struct crypt_device *cd,
			       const char *name,
			       int keyslot,
			       const char *passphrase,
			       size_t passphrase_size)
{
	struct luks_masterkey *mk = NULL;
	int r, suspended = 0;

	log_dbg("Resuming volume %s.", name);

	if (!isLUKS(cd->type)) {
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		r = -EINVAL;
		goto out;
	}

	r = dm_query_device(name, NULL, NULL, NULL, NULL,
			    NULL, NULL, NULL, NULL, &suspended, NULL);
	if (r < 0)
		return r;

	if (!suspended) {
		log_err(cd, _("Volume %s is not suspended.\n"), name);
		return -EINVAL;
	}

	if (passphrase) {
		r = LUKS_open_key_with_hdr(cd->device, keyslot, passphrase,
					   passphrase_size, &cd->hdr, &mk, cd);
	} else
		r = volume_key_by_terminal_passphrase(cd, keyslot, &mk);

	if (r >= 0) {
		keyslot = r;
		r = dm_resume_and_reinstate_key(name, mk->keyLength, mk->key);
		if (r == -ENOTSUP)
			log_err(cd, "Resume is not supported for device %s.\n", name);
		else if (r)
			log_err(cd, "Error during resuming device %s.\n", name);
	} else
		r = keyslot;
out:
	LUKS_dealloc_masterkey(mk);
	return r < 0 ? r : keyslot;
}

int crypt_resume_by_keyfile(struct crypt_device *cd,
			    const char *name,
			    int keyslot,
			    const char *keyfile,
			    size_t keyfile_size)
{
	struct luks_masterkey *mk = NULL;
	char *passphrase_read = NULL;
	unsigned int passphrase_size_read;
	int r, suspended = 0;

	log_dbg("Resuming volume %s.", name);

	if (!isLUKS(cd->type)) {
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		r = -EINVAL;
		goto out;
	}

	r = dm_query_device(name, NULL, NULL, NULL, NULL,
			    NULL, NULL, NULL, NULL, &suspended, NULL);
	if (r < 0)
		return r;

	if (!suspended) {
		log_err(cd, _("Volume %s is not suspended.\n"), name);
		return -EINVAL;
	}

	if (!keyfile)
		return -EINVAL;

	key_from_file(cd, _("Enter passphrase: "), &passphrase_read,
		      &passphrase_size_read, keyfile, keyfile_size);

	if(!passphrase_read)
		r = -EINVAL;
	else {
		r = LUKS_open_key_with_hdr(cd->device, keyslot, passphrase_read,
					   passphrase_size_read, &cd->hdr, &mk, cd);
		safe_free(passphrase_read);
	}

	if (r >= 0) {
		keyslot = r;
		r = dm_resume_and_reinstate_key(name, mk->keyLength, mk->key);
		if (r)
			log_err(cd, "Error during resuming device %s.\n", name);
	} else
		r = keyslot;
out:
	LUKS_dealloc_masterkey(mk);
	return r < 0 ? r : keyslot;
}

// slot manipulation
int crypt_keyslot_add_by_passphrase(struct crypt_device *cd,
	int keyslot, // -1 any
	const char *passphrase, // NULL -> terminal
	size_t passphrase_size,
	const char *new_passphrase, // NULL -> terminal
	size_t new_passphrase_size)
{
	struct luks_masterkey *mk = NULL;
	char *password = NULL, *new_password = NULL;
	unsigned int passwordLen, new_passwordLen;
	int r;

	log_dbg("Adding new keyslot, existing passphrase %sprovided,"
		"new passphrase %sprovided.",
		passphrase ? "" : "not ", new_passphrase  ? "" : "not ");

	if (!isLUKS(cd->type)) {
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		return -EINVAL;
	}

	r = keyslot_verify_or_find_empty(cd, &keyslot);
	if (r)
		return r;

	if (!LUKS_keyslot_active_count(&cd->hdr)) {
		/* No slots used, try to use pre-generated key in header */
		if (cd->volume_key) {
			mk = LUKS_alloc_masterkey(cd->volume_key->keyLength, cd->volume_key->key);
			r = mk ? 0 : -ENOMEM;
		} else {
			log_err(cd, _("Cannot add key slot, all slots disabled and no volume key provided.\n"));
			return -EINVAL;
		}
	} else if (passphrase) {
		/* Passphrase provided, use it to unlock existing keyslot */
		r = LUKS_open_key_with_hdr(cd->device, CRYPT_ANY_SLOT, passphrase,
					   passphrase_size, &cd->hdr, &mk, cd);
	} else {
		/* Passphrase not provided, ask first and use it to unlock existing keyslot */
		key_from_terminal(cd, _("Enter any passphrase: "),
				  &password, &passwordLen, 0);
		if (!password) {
			r = -EINVAL;
			goto out;
		}

		r = LUKS_open_key_with_hdr(cd->device, CRYPT_ANY_SLOT, password,
					   passwordLen, &cd->hdr, &mk, cd);
		safe_free(password);
	}

	if(r < 0)
		goto out;

	if (new_passphrase) {
		new_password = (char *)new_passphrase;
		new_passwordLen = new_passphrase_size;
	} else {
		key_from_terminal(cd, _("Enter new passphrase for key slot: "),
				  &new_password, &new_passwordLen, 1);
		if(!new_password) {
			r = -EINVAL;
			goto out;
		}
	}

	r = LUKS_set_key(cd->device, keyslot, new_password, new_passwordLen,
			 &cd->hdr, mk, cd->iteration_time, &cd->PBKDF2_per_sec, cd);
	if(r < 0) goto out;

	r = 0;
out:
	if (!new_passphrase)
		safe_free(new_password);
	LUKS_dealloc_masterkey(mk);
	return r ?: keyslot;
}

int crypt_keyslot_add_by_keyfile(struct crypt_device *cd,
	int keyslot,
	const char *keyfile,
	size_t keyfile_size,
	const char *new_keyfile,
	size_t new_keyfile_size)
{
	struct luks_masterkey *mk=NULL;
	char *password=NULL; unsigned int passwordLen;
	char *new_password = NULL; unsigned int new_passwordLen;
	int r;

	log_dbg("Adding new keyslot, existing keyfile %s, new keyfile %s.",
		keyfile ?: "[none]", new_keyfile ?: "[none]");

	if (!isLUKS(cd->type)) {
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		return -EINVAL;
	}

	r = keyslot_verify_or_find_empty(cd, &keyslot);
	if (r)
		return r;

	if (!LUKS_keyslot_active_count(&cd->hdr)) {
		/* No slots used, try to use pre-generated key in header */
		if (cd->volume_key) {
			mk = LUKS_alloc_masterkey(cd->volume_key->keyLength, cd->volume_key->key);
			r = mk ? 0 : -ENOMEM;
		} else {
			log_err(cd, _("Cannot add key slot, all slots disabled and no volume key provided.\n"));
			return -EINVAL;
		}
	} else {
		/* Read password from file of (if NULL) from terminal */
		if (keyfile)
			key_from_file(cd, _("Enter any passphrase: "), &password, &passwordLen,
				      keyfile, keyfile_size);
		else
			key_from_terminal(cd, _("Enter any passphrase: "),
					&password, &passwordLen, 0);

		if (!password)
			return -EINVAL;

		r = LUKS_open_key_with_hdr(cd->device, CRYPT_ANY_SLOT, password, passwordLen,
					   &cd->hdr, &mk, cd);
		safe_free(password);
	}

	if(r < 0)
		goto out;

	if (new_keyfile)
		key_from_file(cd, _("Enter new passphrase for key slot: "),
			      &new_password, &new_passwordLen, new_keyfile,
			      new_keyfile_size);
	else
		key_from_terminal(cd, _("Enter new passphrase for key slot: "),
				  &new_password, &new_passwordLen, 1);

	if(!new_password) {
		r = -EINVAL;
		goto out;
	}

	r = LUKS_set_key(cd->device, keyslot, new_password, new_passwordLen,
			 &cd->hdr, mk, cd->iteration_time, &cd->PBKDF2_per_sec, cd);
out:
	safe_free(new_password);
	LUKS_dealloc_masterkey(mk);
	return r < 0 ? r : keyslot;
}

int crypt_keyslot_add_by_volume_key(struct crypt_device *cd,
	int keyslot,
	const char *volume_key,
	size_t volume_key_size,
	const char *passphrase,
	size_t passphrase_size)
{
	struct luks_masterkey *mk = NULL;
	int r = -EINVAL;
	char *new_password = NULL; unsigned int new_passwordLen;

	log_dbg("Adding new keyslot %d using volume key.", keyslot);

	if (!isLUKS(cd->type)) {
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		return -EINVAL;
	}

	if (volume_key)
		mk = LUKS_alloc_masterkey(volume_key_size, volume_key);
	else if (cd->volume_key)
		mk = LUKS_alloc_masterkey(cd->volume_key->keyLength, cd->volume_key->key);

	if (!mk)
		return -ENOMEM;

	r = LUKS_verify_master_key(&cd->hdr, mk);
	if (r < 0) {
		log_err(cd, _("Volume key does not match the volume.\n"));
		goto out;
	}

	r = keyslot_verify_or_find_empty(cd, &keyslot);
	if (r)
		goto out;

	if (!passphrase) {
		key_from_terminal(cd, _("Enter new passphrase for key slot: "),
				  &new_password, &new_passwordLen, 1);
		passphrase = new_password;
		passphrase_size = new_passwordLen;
	}

	r = LUKS_set_key(cd->device, keyslot, passphrase, passphrase_size,
			 &cd->hdr, mk, cd->iteration_time, &cd->PBKDF2_per_sec, cd);
out:
	if (new_password)
		safe_free(new_password);
	LUKS_dealloc_masterkey(mk);
	return r ?: keyslot;
}

int crypt_keyslot_destroy(struct crypt_device *cd, int keyslot)
{
	crypt_keyslot_info ki;

	log_dbg("Destroying keyslot %d.", keyslot);

	if (!isLUKS(cd->type)) {
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		return -EINVAL;
	}

	ki = crypt_keyslot_status(cd, keyslot);
	if (ki == CRYPT_SLOT_INVALID) {
		log_err(cd, _("Key slot %d is invalid.\n"), keyslot);
		return -EINVAL;
	}

	if (ki == CRYPT_SLOT_INACTIVE) {
		log_err(cd, _("Key slot %d is not used.\n"), keyslot);
		return -EINVAL;
	}

	return LUKS_del_key(cd->device, keyslot, &cd->hdr, cd);
}

// activation/deactivation of device mapping
int crypt_activate_by_passphrase(struct crypt_device *cd,
	const char *name,
	int keyslot,
	const char *passphrase,
	size_t passphrase_size,
	uint32_t flags)
{
	crypt_status_info ci;
	struct luks_masterkey *mk = NULL;
	char *prompt = NULL;
	int r;

	log_dbg("%s volume %s [keyslot %d] using %spassphrase.",
		name ? "Activating" : "Checking", name ?: "",
		keyslot, passphrase ? "" : "[none] ");

	if (!name)
		return -EINVAL;

	/* plain, use hashed passphrase */
	if (isPLAIN(cd->type))
		return create_device_helper(cd, name, cd->plain_hdr.hash,
			cd->plain_cipher, cd->plain_cipher_mode, NULL, passphrase, passphrase_size,
			cd->volume_key->keyLength, 0, cd->plain_hdr.skip,
			cd->plain_hdr.offset, cd->plain_uuid, flags & CRYPT_ACTIVATE_READONLY, 0, 0);

	if (name) {
		ci = crypt_status(NULL, name);
		if (ci == CRYPT_INVALID)
			return -EINVAL;
		else if (ci >= CRYPT_ACTIVE) {
			log_err(cd, _("Device %s already exists.\n"), name);
			return -EEXIST;
		}
	}

	if(asprintf(&prompt, _("Enter passphrase for %s: "), cd->device) < 0)
		return -ENOMEM;

	/* provided passphrase, do not retry */
	if (passphrase) {
		r = LUKS_open_key_with_hdr(cd->device, keyslot, passphrase,
					   passphrase_size, &cd->hdr, &mk, cd);
	} else
		r = volume_key_by_terminal_passphrase(cd, keyslot, &mk);

	if (r >= 0) {
		keyslot = r;
		if (name)
			r = open_from_hdr_and_mk(cd, mk, name, flags);
	}

	LUKS_dealloc_masterkey(mk);
	free(prompt);

	return r < 0  ? r : keyslot;
}

int crypt_activate_by_keyfile(struct crypt_device *cd,
	const char *name,
	int keyslot,
	const char *keyfile,
	size_t keyfile_size,
	uint32_t flags)
{
	crypt_status_info ci;
	struct luks_masterkey *mk = NULL;
	char *passphrase_read = NULL;
	unsigned int passphrase_size_read;
	int r;

	log_dbg("Activating volume %s [keyslot %d] using keyfile %s.",
		name, keyslot, keyfile ?: "[none]");

	if (!isLUKS(cd->type)) {
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		return -EINVAL;
	}

	if (name) {
		ci = crypt_status(NULL, name);
		if (ci == CRYPT_INVALID)
			return -EINVAL;
		else if (ci >= CRYPT_ACTIVE) {
			log_err(cd, _("Device %s already exists.\n"), name);
			return -EEXIST;
		}
	}

	if (!keyfile)
		return -EINVAL;

	key_from_file(cd, _("Enter passphrase: "), &passphrase_read,
		      &passphrase_size_read, keyfile, keyfile_size);
	if(!passphrase_read)
		r = -EINVAL;
	else {
		r = LUKS_open_key_with_hdr(cd->device, keyslot, passphrase_read,
					   passphrase_size_read, &cd->hdr, &mk, cd);
		safe_free(passphrase_read);
	}

	if (r >= 0) {
		keyslot = r;
		r = open_from_hdr_and_mk(cd, mk, name, flags);
	}

	LUKS_dealloc_masterkey(mk);

	return r < 0 ? r : keyslot;
}

int crypt_activate_by_volume_key(struct crypt_device *cd,
	const char *name,
	const char *volume_key,
	size_t volume_key_size,
	uint32_t flags)
{
	crypt_status_info ci;
	struct luks_masterkey *mk;
	int r;

	log_dbg("Activating volume %s by volume key.", name);

	/* use key directly, no hash */
	if (isPLAIN(cd->type))
		return create_device_helper(cd, name, NULL,
			cd->plain_cipher, cd->plain_cipher_mode, NULL, volume_key, volume_key_size,
			cd->volume_key->keyLength, 0, cd->plain_hdr.skip,
			cd->plain_hdr.offset, cd->plain_uuid, flags & CRYPT_ACTIVATE_READONLY, 0, 0);

	if (!isLUKS(cd->type)) {
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		return -EINVAL;
	}

	if (name) {
		ci = crypt_status(NULL, name);
		if (ci == CRYPT_INVALID)
			return -EINVAL;
		else if (ci >= CRYPT_ACTIVE) {
			log_err(cd, _("Device %s already exists.\n"), name);
			return -EEXIST;
		}
	}

	mk = LUKS_alloc_masterkey(volume_key_size, volume_key);
	if (!mk)
		return -ENOMEM;
	r = LUKS_verify_master_key(&cd->hdr, mk);

	if (r == -EPERM)
		log_err(cd, _("Volume key does not match the volume.\n"));

	if (!r && name)
		r = open_from_hdr_and_mk(cd, mk, name, flags);

	LUKS_dealloc_masterkey(mk);

	return r;
}

int crypt_deactivate(struct crypt_device *cd, const char *name)
{
	int r;

	if (!name)
		return -EINVAL;

	log_dbg("Deactivating volume %s.", name);

	if (!cd && dm_init(NULL, 1) < 0)
		return -ENOSYS;

	switch (crypt_status(cd, name)) {
		case CRYPT_ACTIVE:
			r = dm_remove_device(name, 0, 0);
			break;
		case CRYPT_BUSY:
			log_err(cd, _("Device %s is busy.\n"), name);
			r = -EBUSY;
			break;
		case CRYPT_INACTIVE:
			log_err(cd, _("Device %s is not active.\n"), name);
			r = -ENODEV;
			break;
		default:
			log_err(cd, _("Invalid device %s.\n"), name);
			r = -EINVAL;
	}

	if (!cd)
		dm_exit();

	return r;
}

// misc helper functions
int crypt_volume_key_get(struct crypt_device *cd,
	int keyslot,
	char *volume_key,
	size_t *volume_key_size,
	const char *passphrase,
	size_t passphrase_size)
{
	struct luks_masterkey *mk;
	char *processed_key = NULL;
	int r, key_len;

	key_len = crypt_get_volume_key_size(cd);
	if (key_len > *volume_key_size) {
		log_err(cd, _("Volume key buffer too small.\n"));
		return -ENOMEM;
	}

	if (isPLAIN(cd->type) && cd->plain_hdr.hash) {
		processed_key = process_key(cd, cd->plain_hdr.hash, NULL, key_len,
					    passphrase, passphrase_size);
		if (!processed_key) {
			log_err(cd, _("Cannot retrieve volume key for plain device.\n"));
			return -EINVAL;
		}
		memcpy(volume_key, processed_key, key_len);
		*volume_key_size = key_len;
		safe_free(processed_key);
		return 0;
	}

	if (isLUKS(cd->type)) {
		r = LUKS_open_key_with_hdr(cd->device, keyslot, passphrase,
					passphrase_size, &cd->hdr, &mk, cd);

		if (r >= 0) {
			memcpy(volume_key, mk->key, mk->keyLength);
			*volume_key_size = mk->keyLength;
		}

		LUKS_dealloc_masterkey(mk);
		return r;
	}

	log_err(cd, _("This operation is not supported for %s crypt device.\n"), cd->type ?: "(none)");
	return -EINVAL;
}

int crypt_volume_key_verify(struct crypt_device *cd,
	const char *volume_key,
	size_t volume_key_size)
{
	struct luks_masterkey *mk;
	int r;

	if (!isLUKS(cd->type)) {
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		return -EINVAL;
	}

	mk = LUKS_alloc_masterkey(volume_key_size, volume_key);
	if (!mk)
		return -ENOMEM;

	r = LUKS_verify_master_key(&cd->hdr, mk);

	if (r == -EPERM)
		log_err(cd, _("Volume key does not match the volume.\n"));

	LUKS_dealloc_masterkey(mk);

	return r;
}

void crypt_set_timeout(struct crypt_device *cd, uint64_t timeout_sec)
{
	log_dbg("Timeout set to %" PRIu64 " miliseconds.", timeout_sec);
	cd->timeout = timeout_sec;
}

void crypt_set_password_retry(struct crypt_device *cd, int tries)
{
	log_dbg("Password retry count set to %d.", tries);
	cd->tries = tries;
}

void crypt_set_iterarion_time(struct crypt_device *cd, uint64_t iteration_time_ms)
{
	log_dbg("Iteration time set to %" PRIu64 " miliseconds.", iteration_time_ms);
	cd->iteration_time = iteration_time_ms;
}

void crypt_set_password_verify(struct crypt_device *cd, int password_verify)
{
	log_dbg("Password verification %s.", password_verify ? "enabled" : "disabled");
	cd->password_verify = password_verify ? 1 : 0;
}

int crypt_memory_lock(struct crypt_device *cd, int lock)
{
	return lock ? crypt_memlock_inc(cd) : crypt_memlock_dec(cd);
}

// reporting
crypt_status_info crypt_status(struct crypt_device *cd, const char *name)
{
	int r;

	if (!cd && dm_init(NULL, 1) < 0)
		return CRYPT_INVALID;

	r = dm_status_device(name);

	if (!cd)
		dm_exit();

	if (r < 0 && r != -ENODEV)
		return CRYPT_INVALID;

	if (r == 0)
		return CRYPT_ACTIVE;

	if (r > 0)
		return CRYPT_BUSY;

	return CRYPT_INACTIVE;
}

static void hexprintICB(struct crypt_device *cd, char *d, int n)
{
	int i;
	for(i = 0; i < n; i++)
		log_std(cd, "%02hhx ", (char)d[i]);
}

int crypt_dump(struct crypt_device *cd)
{
	int i;
	if (!isLUKS(cd->type)) { //FIXME
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		return -EINVAL;
	}

	log_std(cd, "LUKS header information for %s\n\n", cd->device);
	log_std(cd, "Version:       \t%d\n", cd->hdr.version);
	log_std(cd, "Cipher name:   \t%s\n", cd->hdr.cipherName);
	log_std(cd, "Cipher mode:   \t%s\n", cd->hdr.cipherMode);
	log_std(cd, "Hash spec:     \t%s\n", cd->hdr.hashSpec);
	log_std(cd, "Payload offset:\t%d\n", cd->hdr.payloadOffset);
	log_std(cd, "MK bits:       \t%d\n", cd->hdr.keyBytes * 8);
	log_std(cd, "MK digest:     \t");
	hexprintICB(cd, cd->hdr.mkDigest, LUKS_DIGESTSIZE);
	log_std(cd, "\n");
	log_std(cd, "MK salt:       \t");
	hexprintICB(cd, cd->hdr.mkDigestSalt, LUKS_SALTSIZE/2);
	log_std(cd, "\n               \t");
	hexprintICB(cd, cd->hdr.mkDigestSalt+LUKS_SALTSIZE/2, LUKS_SALTSIZE/2);
	log_std(cd, "\n");
	log_std(cd, "MK iterations: \t%d\n", cd->hdr.mkDigestIterations);
	log_std(cd, "UUID:          \t%s\n\n", cd->hdr.uuid);
	for(i = 0; i < LUKS_NUMKEYS; i++) {
		if(cd->hdr.keyblock[i].active == LUKS_KEY_ENABLED) {
			log_std(cd, "Key Slot %d: ENABLED\n",i);
			log_std(cd, "\tIterations:         \t%d\n",
				cd->hdr.keyblock[i].passwordIterations);
			log_std(cd, "\tSalt:               \t");
			hexprintICB(cd, cd->hdr.keyblock[i].passwordSalt,
				    LUKS_SALTSIZE/2);
			log_std(cd, "\n\t                      \t");
			hexprintICB(cd, cd->hdr.keyblock[i].passwordSalt +
				    LUKS_SALTSIZE/2, LUKS_SALTSIZE/2);
			log_std(cd, "\n");

			log_std(cd, "\tKey material offset:\t%d\n",
				cd->hdr.keyblock[i].keyMaterialOffset);
			log_std(cd, "\tAF stripes:            \t%d\n",
				cd->hdr.keyblock[i].stripes);
		}
		else 
			log_std(cd, "Key Slot %d: DISABLED\n", i);
	}
	return 0;
}

const char *crypt_get_cipher(struct crypt_device *cd)
{
	if (isPLAIN(cd->type))
		return cd->plain_cipher;

	if (isLUKS(cd->type))
		return cd->hdr.cipherName;

	return NULL;
}

const char *crypt_get_cipher_mode(struct crypt_device *cd)
{
	if (isPLAIN(cd->type))
		return cd->plain_cipher_mode;

	if (isLUKS(cd->type))
		return cd->hdr.cipherMode;

	return NULL;
}

const char *crypt_get_uuid(struct crypt_device *cd)
{
	if (isLUKS(cd->type))
		return cd->hdr.uuid;

	return NULL;
}

int crypt_get_volume_key_size(struct crypt_device *cd)
{
	if (isPLAIN(cd->type))
		return cd->volume_key->keyLength;

	if (isLUKS(cd->type))
		return cd->hdr.keyBytes;

	return 0;
}

uint64_t crypt_get_data_offset(struct crypt_device *cd)
{
	if (isPLAIN(cd->type))
		return cd->plain_hdr.offset;

	if (isLUKS(cd->type))
		return cd->hdr.payloadOffset;

	return 0;
}

crypt_keyslot_info crypt_keyslot_status(struct crypt_device *cd, int keyslot)
{
	if (!isLUKS(cd->type)) {
		log_err(cd, _("This operation is supported only for LUKS device.\n"));
		return CRYPT_SLOT_INVALID;
	}

	return LUKS_keyslot_info(&cd->hdr, keyslot);
}
