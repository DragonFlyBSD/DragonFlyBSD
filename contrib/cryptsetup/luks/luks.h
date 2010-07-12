#ifndef INCLUDED_CRYPTSETUP_LUKS_LUKS_H
#define INCLUDED_CRYPTSETUP_LUKS_LUKS_H

/*
 * LUKS partition header
 */

#include "libcryptsetup.h"

#define LUKS_CIPHERNAME_L 32
#define LUKS_CIPHERMODE_L 32
#define LUKS_HASHSPEC_L 32
#define LUKS_DIGESTSIZE 20 // since SHA1
#define LUKS_HMACSIZE 32
#define LUKS_SALTSIZE 32
#define LUKS_NUMKEYS 8

// Minimal number of iterations
#define LUKS_MKD_ITERATIONS_MIN  1000
#define LUKS_SLOT_ITERATIONS_MIN 1000

#define LUKS_KEY_DISABLED_OLD 0
#define LUKS_KEY_ENABLED_OLD 0xCAFE

#define LUKS_KEY_DISABLED 0x0000DEAD
#define LUKS_KEY_ENABLED  0x00AC71F3

#define LUKS_STRIPES 4000

// partition header starts with magic
#define LUKS_MAGIC {'L','U','K','S', 0xba, 0xbe};
#define LUKS_MAGIC_L 6

#define LUKS_PHDR_SIZE (sizeof(struct luks_phdr)/SECTOR_SIZE+1)

/* Actually we need only 37, but we don't want struct autoaligning to kick in */
#define UUID_STRING_L 40

/* Offset to align kesylot area */
#define LUKS_ALIGN_KEYSLOTS 4096

/* Any integer values are stored in network byte order on disk and must be
converted */

struct luks_phdr {
	char		magic[LUKS_MAGIC_L];
	uint16_t	version;
	char		cipherName[LUKS_CIPHERNAME_L];
	char		cipherMode[LUKS_CIPHERMODE_L];
	char            hashSpec[LUKS_HASHSPEC_L];
	uint32_t	payloadOffset;
	uint32_t	keyBytes;
	char		mkDigest[LUKS_DIGESTSIZE];
	char		mkDigestSalt[LUKS_SALTSIZE];
	uint32_t	mkDigestIterations;
	char            uuid[UUID_STRING_L];

	struct {
		uint32_t active;

		/* parameters used for password processing */
		uint32_t passwordIterations;
		char     passwordSalt[LUKS_SALTSIZE];

		/* parameters used for AF store/load */
		uint32_t keyMaterialOffset;
		uint32_t stripes;
	} keyblock[LUKS_NUMKEYS];

	/* Align it to 512 sector size */
	char		_padding[432];
};

struct luks_masterkey {
	size_t keyLength;
	char key[];
};

struct luks_masterkey *LUKS_alloc_masterkey(int keylength, const char *key);
void LUKS_dealloc_masterkey(struct luks_masterkey *mk);
struct luks_masterkey *LUKS_generate_masterkey(int keylength);
int LUKS_verify_master_key(const struct luks_phdr *hdr,
			   const struct luks_masterkey *mk);

int LUKS_generate_phdr(
	struct luks_phdr *header,
	const struct luks_masterkey *mk,
	const char *cipherName,
	const char *cipherMode,
	const char *hashSpec,
	const char *uuid,
	unsigned int stripes,
	unsigned int alignPayload,
	unsigned int alignOffset,
	uint32_t iteration_time_ms,
	uint64_t *PBKDF2_per_sec,
	struct crypt_device *ctx);

int LUKS_read_phdr(
	const char *device,
	struct luks_phdr *hdr,
	int require_luks_device,
	struct crypt_device *ctx);

int LUKS_read_phdr_backup(
	const char *backup_file,
	const char *device,
	struct luks_phdr *hdr,
	int require_luks_device,
	struct crypt_device *ctx);

int LUKS_hdr_backup(
	const char *backup_file,
	const char *device,
	struct luks_phdr *hdr,
	struct crypt_device *ctx);

int LUKS_hdr_restore(
	const char *backup_file,
	const char *device,
	struct luks_phdr *hdr,
	struct crypt_device *ctx);

int LUKS_write_phdr(
	const char *device,
	struct luks_phdr *hdr,
	struct crypt_device *ctx);

int LUKS_set_key(
	const char *device,
	unsigned int keyIndex,
	const char *password,
	size_t passwordLen,
	struct luks_phdr *hdr,
	struct luks_masterkey *mk,
	uint32_t iteration_time_ms,
	uint64_t *PBKDF2_per_sec,
	struct crypt_device *ctx);

int LUKS_open_key_with_hdr(
	const char *device,
	int keyIndex,
	const char *password,
	size_t passwordLen,
	struct luks_phdr *hdr,
	struct luks_masterkey **mk,
	struct crypt_device *ctx);

int LUKS_del_key(
	const char *device,
	unsigned int keyIndex,
	struct luks_phdr *hdr,
	struct crypt_device *ctx);

crypt_keyslot_info LUKS_keyslot_info(struct luks_phdr *hdr, int keyslot);
int LUKS_keyslot_find_empty(struct luks_phdr *hdr);
int LUKS_keyslot_active_count(struct luks_phdr *hdr);
int LUKS_keyslot_set(struct luks_phdr *hdr, int keyslot, int enable);

int LUKS_encrypt_to_storage(
	char *src, size_t srcLength,
	struct luks_phdr *hdr,
	char *key, size_t keyLength,
	const char *device,
	unsigned int sector,
	struct crypt_device *ctx);

int LUKS_decrypt_from_storage(
	char *dst, size_t dstLength,
	struct luks_phdr *hdr,
	char *key, size_t keyLength,
	const char *device,
	unsigned int sector,
	struct crypt_device *ctx);

#endif
