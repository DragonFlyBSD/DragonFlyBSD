/*
 * cryptsetup library API check functions
 *
 * Copyright (C) 2009 Red Hat, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
//#include <linux/fs.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "libcryptsetup.h"

#define DMDIR "/dev/mapper/"

#define DEVICE_1 "/dev/vn1"
#define DEVICE_1_UUID "28632274-8c8a-493f-835b-da802e1c576b"
#define DEVICE_2 "/dev/vn2"
#define DEVICE_EMPTY_name "crypt_zero"
#define DEVICE_EMPTY DMDIR DEVICE_EMPTY_name
#define DEVICE_ERROR_name "crypt_error"
#define DEVICE_ERROR DMDIR DEVICE_ERROR_name

#define CDEVICE_1 "ctest1"
#define CDEVICE_2 "ctest2"
#define CDEVICE_WRONG "O_o"

#define IMAGE1 "compatimage.img"
#define IMAGE_EMPTY "empty.img"

#define KEYFILE1 "key1.file"
#define KEY1 "compatkey"

#define KEYFILE2 "key2.file"
#define KEY2 "0123456789abcdef"

static int _debug   = 0;
static int _verbose = 1;

static char global_log[4096];
static int global_lines = 0;

static int gcrypt_compatible = 0;

// Helpers
static int _prepare_keyfile(const char *name, const char *passphrase)
{
	int fd, r;

	fd = open(name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR);
	if (fd != -1) {
		r = write(fd, passphrase, strlen(passphrase));
		close(fd);
	} else
		r = 0;

	return r == strlen(passphrase) ? 0 : 1;
}

static void _remove_keyfiles(void)
{
	remove(KEYFILE1);
	remove(KEYFILE2);
}

// Decode key from its hex representation
static int crypt_decode_key(char *key, char *hex, unsigned int size)
{
	char buffer[3];
	char *endp;
	unsigned int i;

	buffer[2] = '\0';

	for (i = 0; i < size; i++) {
		buffer[0] = *hex++;
		buffer[1] = *hex++;

		key[i] = (unsigned char)strtoul(buffer, &endp, 16);

		if (endp != &buffer[2])
			return -1;
	}

	if (*hex != '\0')
		return -1;

	return 0;
}

static int yesDialog(char *msg)
{
	return 1;
}

static void cmdLineLog(int level, char *msg)
{
	strncat(global_log, msg, sizeof(global_log) - strlen(global_log));
	global_lines++;
}

static void new_log(int level, const char *msg, void *usrptr)
{
	cmdLineLog(level, (char*)msg);
}


static void reset_log()
{
	memset(global_log, 0, sizeof(global_log));
	global_lines = 0;
}

static struct interface_callbacks cmd_icb = {
	.yesDialog = yesDialog,
	.log = cmdLineLog,
};

static void _cleanup(void)
{
	int r;
	struct stat st;

	//r = system("udevadm settle");

	if (!stat(DMDIR CDEVICE_1, &st))
		r = system("dmsetup remove " CDEVICE_1);

	if (!stat(DMDIR CDEVICE_2, &st))
		r = system("dmsetup remove " CDEVICE_2);

	if (!stat(DEVICE_EMPTY, &st))
		r = system("dmsetup remove " DEVICE_EMPTY_name);

	if (!stat(DEVICE_ERROR, &st))
		r = system("dmsetup remove " DEVICE_ERROR_name);

	if (!strncmp("/dev/vn", DEVICE_1, 7))
		r = system("vnconfig -u " DEVICE_1);

	if (!strncmp("/dev/vn", DEVICE_2, 7))
		r = system("vnconfig -u " DEVICE_2);

	r = system("rm -f " IMAGE_EMPTY);
	_remove_keyfiles();
}

static void _setup(void)
{
	int r;

	r = system("dmsetup create " DEVICE_EMPTY_name " --table \"0 10000 zero\"");
	r = system("dmsetup create " DEVICE_ERROR_name " --table \"0 10000 error\"");
	if (!strncmp("/dev/vn", DEVICE_1, 7)) {
		r = system(" [ ! -e " IMAGE1 " ] && bzip2 -dk " IMAGE1 ".bz2");
		r = system("vnconfig -S labels -T " DEVICE_1 " " IMAGE1);
	}
	if (!strncmp("/dev/vn", DEVICE_2, 7)) {
		r = system("dd if=/dev/zero of=" IMAGE_EMPTY " bs=1M count=4");
		r = system("vnconfig -S labels -T " DEVICE_2 " " IMAGE_EMPTY);
	}

}

void check_ok(int status, int line, const char *func)
{
	char buf[256];

	if (status) {
		crypt_get_error(buf, sizeof(buf));
		printf("FAIL line %d [%s]: code %d, %s\n", line, func, status, buf);
		_cleanup();
		exit(-1);
	}
}

void check_ko(int status, int line, const char *func)
{
	char buf[256];

	memset(buf, 0, sizeof(buf));
	crypt_get_error(buf, sizeof(buf));
	if (status >= 0) {
		printf("FAIL line %d [%s]: code %d, %s\n", line, func, status, buf);
		_cleanup();
		exit(-1);
	} else if (_verbose)
		printf("   => errno %d, errmsg: %s\n", status, buf);
}

void check_equal(int line, const char *func)
{
	printf("FAIL line %d [%s]: expected equal values differs.\n", line, func);
	_cleanup();
	exit(-1);
}

void xlog(const char *msg, const char *tst, const char *func, int line, const char *txt)
{
	if (_verbose) {
		if (txt)
			printf(" [%s,%s:%d] %s [%s]\n", msg, func, line, tst, txt);
		else
			printf(" [%s,%s:%d] %s\n", msg, func, line, tst);
	}
}
#define OK_(x)		do { xlog("(success)", #x, __FUNCTION__, __LINE__, NULL); \
			     check_ok((x), __LINE__, __FUNCTION__); \
			} while(0)
#define FAIL_(x, y)	do { xlog("(fail)   ", #x, __FUNCTION__, __LINE__, y); \
			     check_ko((x), __LINE__, __FUNCTION__); \
			} while(0)
#define EQ_(x, y)	do { xlog("(equal)  ", #x " == " #y, __FUNCTION__, __LINE__, NULL); \
			     if ((x) != (y)) check_equal(__LINE__, __FUNCTION__); \
			} while(0)

#define RUN_(x, y)		do { printf("%s: %s\n", #x, (y)); x(); } while (0)

// OLD API TESTS
static void LuksUUID(void)
{
	struct crypt_options co = { .icb = &cmd_icb };

	co.device = DEVICE_EMPTY;
	EQ_(crypt_luksUUID(&co), -EINVAL);

	co.device = DEVICE_ERROR;
	EQ_(crypt_luksUUID(&co), -EINVAL);

	reset_log();
	co.device = DEVICE_1;
	OK_(crypt_luksUUID(&co));
	EQ_(strlen(global_log), 37); /* UUID + "\n" */
	EQ_(strncmp(global_log, DEVICE_1_UUID, strlen(DEVICE_1_UUID)), 0);

}

static void IsLuks(void)
{
	struct crypt_options co = {  .icb = &cmd_icb };

	co.device = DEVICE_EMPTY;
	EQ_(crypt_isLuks(&co), -EINVAL);

	co.device = DEVICE_ERROR;
	EQ_(crypt_isLuks(&co), -EINVAL);

	co.device = DEVICE_1;
	OK_(crypt_isLuks(&co));
}

static void LuksOpen(void)
{
	struct crypt_options co = {
		.name = CDEVICE_1,
		//.passphrase = "blabla",
		.icb = &cmd_icb,
	};

	OK_(_prepare_keyfile(KEYFILE1, KEY1));
	co.key_file = KEYFILE1;

	co.device = DEVICE_EMPTY;
	EQ_(crypt_luksOpen(&co), -EINVAL);

	co.device = DEVICE_ERROR;
	EQ_(crypt_luksOpen(&co), -EINVAL);

	co.device = DEVICE_1;
	OK_(crypt_luksOpen(&co));
	FAIL_(crypt_luksOpen(&co), "already open");

	_remove_keyfiles();
}

static void query_device(void)
{
	struct crypt_options co = {. icb = &cmd_icb };

	co.name = CDEVICE_WRONG;
	EQ_(crypt_query_device(&co), 0);

	co.name = CDEVICE_1;
	EQ_(crypt_query_device(&co), 1);

	OK_(strncmp(crypt_get_dir(), DMDIR, 11));
	OK_(strcmp(co.cipher, "aes-cbc-essiv:sha256"));
	EQ_(co.key_size, 16);
	EQ_(co.offset, 1032);
	EQ_(co.flags & CRYPT_FLAG_READONLY, 0);
	EQ_(co.skip, 0);
	crypt_put_options(&co);
}

static void remove_device(void)
{
	int fd;
	struct crypt_options co = {. icb = &cmd_icb };

	co.name = CDEVICE_WRONG;
	EQ_(crypt_remove_device(&co), -ENODEV);

	fd = open(DMDIR CDEVICE_1, O_RDONLY);
	co.name = CDEVICE_1;
	FAIL_(crypt_remove_device(&co), "device busy");
	close(fd);

	OK_(crypt_remove_device(&co));
}

static void LuksFormat(void)
{
	struct crypt_options co = {
		.device = DEVICE_2,
		.key_size = 256 / 8,
		.key_slot = -1,
		.cipher = "aes-cbc-essiv:sha256",
		.hash = "sha1",
		.flags = 0,
		.iteration_time = 10,
		.align_payload = 0,
		.icb = &cmd_icb,
	};

	OK_(_prepare_keyfile(KEYFILE1, KEY1));

	co.new_key_file = KEYFILE1;
	co.device = DEVICE_ERROR;
	FAIL_(crypt_luksFormat(&co), "error device");

	co.device = DEVICE_2;
	OK_(crypt_luksFormat(&co));

	co.new_key_file = NULL;
	co.key_file = KEYFILE1;
	co.name = CDEVICE_2;
	OK_(crypt_luksOpen(&co));
	OK_(crypt_remove_device(&co));
	_remove_keyfiles();
}

static void LuksKeyGame(void)
{
	int i;
	struct crypt_options co = {
		.device = DEVICE_2,
		.key_size = 256 / 8,
		.key_slot = -1,
		.cipher = "aes-cbc-essiv:sha256",
		.hash = "sha1",
		.flags = 0,
		.iteration_time = 10,
		.align_payload = 0,
		.icb = &cmd_icb,
	};

	OK_(_prepare_keyfile(KEYFILE1, KEY1));
	OK_(_prepare_keyfile(KEYFILE2, KEY2));

	co.new_key_file = KEYFILE1;
	co.device = DEVICE_2;
	co.key_slot = 8;
	FAIL_(crypt_luksFormat(&co), "wrong slot #");

	co.key_slot = 7; // last slot
	OK_(crypt_luksFormat(&co));

	co.new_key_file = KEYFILE1;
	co.key_file = KEYFILE1;
	co.key_slot = 8;
	FAIL_(crypt_luksAddKey(&co), "wrong slot #");
	co.key_slot = 7;
	FAIL_(crypt_luksAddKey(&co), "slot already used");

	co.key_slot = 6;
	OK_(crypt_luksAddKey(&co));

	co.key_file = KEYFILE2 "blah";
	co.key_slot = 5;
	FAIL_(crypt_luksAddKey(&co), "keyfile not found");

	co.new_key_file = KEYFILE2; // key to add
	co.key_file = KEYFILE1;
	co.key_slot = -1;
	for (i = 0; i < 6; i++)
		OK_(crypt_luksAddKey(&co)); //FIXME: EQ_(i)?

	FAIL_(crypt_luksAddKey(&co), "all slots full");

	// REMOVE KEY
	co.new_key_file = KEYFILE1; // key to remove
	co.key_file = NULL;
	co.key_slot = 8; // should be ignored
	 // only 2 slots should use KEYFILE1
	OK_(crypt_luksRemoveKey(&co));
	OK_(crypt_luksRemoveKey(&co));
	FAIL_(crypt_luksRemoveKey(&co), "no slot with this passphrase");

	co.new_key_file = KEYFILE2 "blah";
	co.key_file = NULL;
	FAIL_(crypt_luksRemoveKey(&co), "keyfile not found");

	// KILL SLOT
	co.new_key_file = NULL;
	co.key_file = NULL;
	co.key_slot = 8;
	FAIL_(crypt_luksKillSlot(&co), "wrong slot #");
	co.key_slot = 7;
	FAIL_(crypt_luksKillSlot(&co), "slot already wiped");

	co.key_slot = 5;
	OK_(crypt_luksKillSlot(&co));

	_remove_keyfiles();
}

size_t _get_device_size(const char *device)
{
#if 0 /* XXX swildner */
	unsigned long size = 0;
	int fd;

	fd = open(device, O_RDONLY);
	if (fd == -1)
		return 0;
	(void)ioctl(fd, BLKGETSIZE, &size);
	close(fd);

	return size;
#endif
	return DEV_BSIZE;
}

void DeviceResizeGame(void)
{
	size_t orig_size;
	struct crypt_options co = {
		.name = CDEVICE_2,
		.device = DEVICE_2,
		.key_size = 128 / 8,
		.cipher = "aes-cbc-plain",
		.hash = "sha1",
		.offset = 333,
		.skip = 0,
		.icb = &cmd_icb,
	};

	orig_size = _get_device_size(DEVICE_2);

	OK_(_prepare_keyfile(KEYFILE2, KEY2));

	co.key_file = KEYFILE2;
	co.size = 1000;
	OK_(crypt_create_device(&co));
	EQ_(_get_device_size(DMDIR CDEVICE_2), 1000);

	co.size = 2000;
	OK_(crypt_resize_device(&co));
	EQ_(_get_device_size(DMDIR CDEVICE_2), 2000);

	co.size = 0;
	OK_(crypt_resize_device(&co));
	EQ_(_get_device_size(DMDIR CDEVICE_2), (orig_size - 333));
	co.size = 0;
	co.offset = 444;
	co.skip = 555;
	co.cipher = "aes-cbc-essiv:sha256";
	OK_(crypt_update_device(&co));
	EQ_(_get_device_size(DMDIR CDEVICE_2), (orig_size - 444));

	memset(&co, 0, sizeof(co));
	co.icb = &cmd_icb,
	co.name = CDEVICE_2;
	EQ_(crypt_query_device(&co), 1);
	EQ_(strcmp(co.cipher, "aes-cbc-essiv:sha256"), 0);
	EQ_(co.key_size, 128 / 8);
	EQ_(co.offset, 444);
	EQ_(co.skip, 555);
	crypt_put_options(&co);

	// dangerous switch device still works
	memset(&co, 0, sizeof(co));
	co.name = CDEVICE_2,
	co.device = DEVICE_1;
	co.key_file = KEYFILE2;
	co.key_size = 128 / 8;
	co.cipher = "aes-cbc-plain";
	co.hash = "sha1";
	co.icb = &cmd_icb;
	OK_(crypt_update_device(&co));

	memset(&co, 0, sizeof(co));
	co.icb = &cmd_icb,
	co.name = CDEVICE_2;
	EQ_(crypt_query_device(&co), 1);
	EQ_(strcmp(co.cipher, "aes-cbc-plain"), 0);
	EQ_(co.key_size, 128 / 8);
	EQ_(co.offset, 0);
	EQ_(co.skip, 0);
	// This expect lookup returns prefered /dev/loopX
	EQ_(strcmp(co.device, DEVICE_1), 0);
	crypt_put_options(&co);

	memset(&co, 0, sizeof(co));
	co.icb = &cmd_icb,
	co.name = CDEVICE_2;
	OK_(crypt_remove_device(&co));

	_remove_keyfiles();
}

// NEW API tests

static void AddDevicePlain(void)
{
	struct crypt_device *cd;
	struct crypt_params_plain params = {
		.hash = "sha1",
		.skip = 0,
		.offset = 0,
	};
	int fd;
	char key[128], key2[128], path[128];

	char *passphrase = "blabla";
	char *mk_hex = "bb21158c733229347bd4e681891e213d94c685be6a5b84818afe7a78a6de7a1a";
	size_t key_size = strlen(mk_hex) / 2;
	char *cipher = "aes";
	char *cipher_mode = "cbc-essiv:sha256";

	crypt_decode_key(key, mk_hex, key_size);

	FAIL_(crypt_init(&cd, ""), "empty device string");

	// default is "plain" hash - no password hash
	OK_(crypt_init(&cd, DEVICE_1));
	OK_(crypt_format(cd, CRYPT_PLAIN, cipher, cipher_mode, NULL, NULL, key_size, NULL));
	OK_(crypt_activate_by_volume_key(cd, CDEVICE_1, key, key_size, 0));
	EQ_(crypt_status(cd, CDEVICE_1), CRYPT_ACTIVE);
	// FIXME: this should get key from active device?
	//OK_(crypt_volume_key_get(cd, CRYPT_ANY_SLOT, key2, &key_size, passphrase, strlen(passphrase)));
	//OK_(memcmp(key, key2, key_size));
	OK_(crypt_deactivate(cd, CDEVICE_1));
	crypt_free(cd);

	// Now use hashed password
	OK_(crypt_init(&cd, DEVICE_1));
	OK_(crypt_format(cd, CRYPT_PLAIN, cipher, cipher_mode, NULL, NULL, key_size, &params));
	OK_(crypt_activate_by_passphrase(cd, CDEVICE_1, CRYPT_ANY_SLOT, passphrase, strlen(passphrase), 0));

	// device status check
	EQ_(crypt_status(cd, CDEVICE_1), CRYPT_ACTIVE);
	snprintf(path, sizeof(path), "%s/%s", crypt_get_dir(), CDEVICE_1);
	fd = open(path, O_RDONLY);
	EQ_(crypt_status(cd, CDEVICE_1), CRYPT_BUSY);
	FAIL_(crypt_deactivate(cd, CDEVICE_1), "Device is busy");
	close(fd);
	OK_(crypt_deactivate(cd, CDEVICE_1));
	EQ_(crypt_status(cd, CDEVICE_1), CRYPT_INACTIVE);

	OK_(crypt_activate_by_volume_key(cd, CDEVICE_1, key, key_size, 0));
	EQ_(crypt_status(cd, CDEVICE_1), CRYPT_ACTIVE);

	// retrieve volume key check
	memset(key2, 0, key_size);
	key_size--;
	// small buffer
	FAIL_(crypt_volume_key_get(cd, CRYPT_ANY_SLOT, key2, &key_size, passphrase, strlen(passphrase)), "small buffer");
	key_size++;
	OK_(crypt_volume_key_get(cd, CRYPT_ANY_SLOT, key2, &key_size, passphrase, strlen(passphrase)));

	OK_(memcmp(key, key2, key_size));
	OK_(strcmp(cipher, crypt_get_cipher(cd)));
	OK_(strcmp(cipher_mode, crypt_get_cipher_mode(cd)));
	EQ_(key_size, crypt_get_volume_key_size(cd));
	EQ_(0, crypt_get_data_offset(cd));
	OK_(crypt_deactivate(cd, CDEVICE_1));
	crypt_free(cd);
}

static void UseLuksDevice(void)
{
	struct crypt_device *cd;
	char key[128];
	size_t key_size;

	OK_(crypt_init(&cd, DEVICE_1));
	OK_(crypt_load(cd, CRYPT_LUKS1, NULL));
	EQ_(crypt_status(cd, CDEVICE_1), CRYPT_INACTIVE);
	OK_(crypt_activate_by_passphrase(cd, CDEVICE_1, CRYPT_ANY_SLOT, KEY1, strlen(KEY1), 0));
	FAIL_(crypt_activate_by_passphrase(cd, CDEVICE_1, CRYPT_ANY_SLOT, KEY1, strlen(KEY1), 0), "already open");
	EQ_(crypt_status(cd, CDEVICE_1), CRYPT_ACTIVE);
	OK_(crypt_deactivate(cd, CDEVICE_1));
	FAIL_(crypt_deactivate(cd, CDEVICE_1), "no such device");

	key_size = 16;
	OK_(strcmp("aes", crypt_get_cipher(cd)));
	OK_(strcmp("cbc-essiv:sha256", crypt_get_cipher_mode(cd)));
	OK_(strcmp(DEVICE_1_UUID, crypt_get_uuid(cd)));
	EQ_(key_size, crypt_get_volume_key_size(cd));
	EQ_(1032, crypt_get_data_offset(cd));

	EQ_(0, crypt_volume_key_get(cd, CRYPT_ANY_SLOT, key, &key_size, KEY1, strlen(KEY1)));
	OK_(crypt_volume_key_verify(cd, key, key_size));
	OK_(crypt_activate_by_volume_key(cd, CDEVICE_1, key, key_size, 0));
	EQ_(crypt_status(cd, CDEVICE_1), CRYPT_ACTIVE);
	OK_(crypt_deactivate(cd, CDEVICE_1));

	key[1] = ~key[1];
	FAIL_(crypt_volume_key_verify(cd, key, key_size), "key mismatch");
	FAIL_(crypt_activate_by_volume_key(cd, CDEVICE_1, key, key_size, 0), "key mismatch");
	crypt_free(cd);
}

static void SuspendDevice(void)
{
	int suspend_status;
	struct crypt_device *cd;

	OK_(crypt_init(&cd, DEVICE_1));
	OK_(crypt_load(cd, CRYPT_LUKS1, NULL));
	OK_(crypt_activate_by_passphrase(cd, CDEVICE_1, CRYPT_ANY_SLOT, KEY1, strlen(KEY1), 0));

	suspend_status = crypt_suspend(cd, CDEVICE_1);
	if (suspend_status == -ENOTSUP) {
		printf("WARNING: Suspend/Resume not supported, skipping test.\n");
		goto out;
	}
	OK_(suspend_status);
	FAIL_(crypt_suspend(cd, CDEVICE_1), "already suspended");

	FAIL_(crypt_resume_by_passphrase(cd, CDEVICE_1, CRYPT_ANY_SLOT, KEY1, strlen(KEY1)-1), "wrong key");
	OK_(crypt_resume_by_passphrase(cd, CDEVICE_1, CRYPT_ANY_SLOT, KEY1, strlen(KEY1)));
	FAIL_(crypt_resume_by_passphrase(cd, CDEVICE_1, CRYPT_ANY_SLOT, KEY1, strlen(KEY1)), "not suspended");

	OK_(_prepare_keyfile(KEYFILE1, KEY1));
	OK_(crypt_suspend(cd, CDEVICE_1));
	FAIL_(crypt_resume_by_keyfile(cd, CDEVICE_1, CRYPT_ANY_SLOT, KEYFILE1 "blah", 0), "wrong keyfile");
	OK_(crypt_resume_by_keyfile(cd, CDEVICE_1, CRYPT_ANY_SLOT, KEYFILE1, 0));
	FAIL_(crypt_resume_by_keyfile(cd, CDEVICE_1, CRYPT_ANY_SLOT, KEYFILE1, 0), "not suspended");
	_remove_keyfiles();
out:
	OK_(crypt_deactivate(cd, CDEVICE_1));
	crypt_free(cd);
}

static void AddDeviceLuks(void)
{
	struct crypt_device *cd;
	struct crypt_params_luks1 params = {
		.hash = "sha512",
		.data_alignment = 2048, // 4M, data offset will be 4096
	};
	char key[128], key2[128];

	char *passphrase = "blabla";
	char *mk_hex = "bb21158c733229347bd4e681891e213d94c685be6a5b84818afe7a78a6de7a1a";
	size_t key_size = strlen(mk_hex) / 2;
	char *cipher = "aes";
	char *cipher_mode = "cbc-essiv:sha256";

	crypt_decode_key(key, mk_hex, key_size);

	OK_(crypt_init(&cd, DEVICE_2));
	OK_(crypt_format(cd, CRYPT_LUKS1, cipher, cipher_mode, NULL, key, key_size, &params));

	// even with no keyslots defined it can be activated by volume key
	OK_(crypt_volume_key_verify(cd, key, key_size));
	OK_(crypt_activate_by_volume_key(cd, CDEVICE_2, key, key_size, 0));
	EQ_(crypt_status(cd, CDEVICE_2), CRYPT_ACTIVE);
	OK_(crypt_deactivate(cd, CDEVICE_2));

	// now with keyslot
	EQ_(7, crypt_keyslot_add_by_volume_key(cd, 7, key, key_size, passphrase, strlen(passphrase)));
	EQ_(CRYPT_SLOT_ACTIVE_LAST, crypt_keyslot_status(cd, 7));
	EQ_(7, crypt_activate_by_passphrase(cd, CDEVICE_2, CRYPT_ANY_SLOT, passphrase, strlen(passphrase), 0));
	EQ_(crypt_status(cd, CDEVICE_2), CRYPT_ACTIVE);
	OK_(crypt_deactivate(cd, CDEVICE_2));

	FAIL_(crypt_keyslot_add_by_volume_key(cd, 7, key, key_size, passphrase, strlen(passphrase)), "slot used");
	key[1] = ~key[1];
	FAIL_(crypt_keyslot_add_by_volume_key(cd, 6, key, key_size, passphrase, strlen(passphrase)), "key mismatch");
	key[1] = ~key[1];
	EQ_(6, crypt_keyslot_add_by_volume_key(cd, 6, key, key_size, passphrase, strlen(passphrase)));
	EQ_(CRYPT_SLOT_ACTIVE, crypt_keyslot_status(cd, 6));

	FAIL_(crypt_keyslot_destroy(cd, 8), "invalid keyslot");
	FAIL_(crypt_keyslot_destroy(cd, CRYPT_ANY_SLOT), "invalid keyslot");
	FAIL_(crypt_keyslot_destroy(cd, 0), "keyslot not used");
	OK_(crypt_keyslot_destroy(cd, 7));
	EQ_(CRYPT_SLOT_INACTIVE, crypt_keyslot_status(cd, 7));
	EQ_(CRYPT_SLOT_ACTIVE_LAST, crypt_keyslot_status(cd, 6));

	EQ_(6, crypt_volume_key_get(cd, CRYPT_ANY_SLOT, key2, &key_size, passphrase, strlen(passphrase)));
	OK_(crypt_volume_key_verify(cd, key2, key_size));

	OK_(memcmp(key, key2, key_size));
	OK_(strcmp(cipher, crypt_get_cipher(cd)));
	OK_(strcmp(cipher_mode, crypt_get_cipher_mode(cd)));
	EQ_(key_size, crypt_get_volume_key_size(cd));
	EQ_(4096, crypt_get_data_offset(cd));

	reset_log();
	crypt_set_log_callback(cd, &new_log, NULL);
	OK_(crypt_dump(cd));
	OK_(!(global_lines != 0));
	crypt_set_log_callback(cd, NULL, NULL);
	reset_log();

	FAIL_(crypt_deactivate(cd, CDEVICE_2), "not active");
	crypt_free(cd);
}

// Check that gcrypt is properly initialised in format
static void NonFIPSAlg(void)
{
	struct crypt_device *cd;
	struct crypt_params_luks1 params = {
		.hash = "whirlpool",
	};
	char key[128] = "";
	size_t key_size = 128;
	char *cipher = "aes";
	char *cipher_mode = "cbc-essiv:sha256";

	if (!gcrypt_compatible) {
		printf("WARNING: old libgcrypt, skipping test.\n");
		return;
	}
	OK_(crypt_init(&cd, DEVICE_2));
	OK_(crypt_format(cd, CRYPT_LUKS1, cipher, cipher_mode, NULL, key, key_size, &params));
	crypt_free(cd);
}


static void _gcrypt_compatible()
{
	int maj, min, patch;
	FILE *f;

	if (!(f = popen("libgcrypt-config --version", "r")))
		return;

	if (fscanf(f, "%d.%d.%d", &maj, &min, &patch) == 3 &&
	    maj >= 1 && min >= 4)
		gcrypt_compatible = 1;
	if (_debug)
		printf("libgcrypt version %d.%d.%d detected.\n", maj, min, patch);

	(void)fclose(f);
	return;
}

int main (int argc, char *argv[])
{
	int i;

	if (getuid() != 0) {
		printf("You must be root to run this test.\n");
		exit(0);
	}

	for (i = 1; i < argc; i++) {
		if (!strcmp("-v", argv[i]) || !strcmp("--verbose", argv[i]))
			_verbose = 1;
		else if (!strcmp("--debug", argv[i]))
			_debug = _verbose = 1;
	}

	_cleanup();
	_setup();
	_gcrypt_compatible();

	crypt_set_debug_level(_debug ? CRYPT_DEBUG_ALL : CRYPT_DEBUG_NONE);

	RUN_(NonFIPSAlg, "Crypto is properly initialised in format"); //must be the first!
	RUN_(LuksUUID, "luksUUID API call");
	RUN_(IsLuks, "isLuks API call");
	RUN_(LuksOpen, "luksOpen API call");
	RUN_(query_device, "crypt_query_device API call");
	RUN_(remove_device, "crypt_remove_device API call");
	RUN_(LuksFormat, "luksFormat API call");
	RUN_(LuksKeyGame, "luksAddKey, RemoveKey, KillSlot API calls");
	RUN_(DeviceResizeGame, "regular crypto, resize calls");

	RUN_(AddDevicePlain, "plain device API creation exercise");
	RUN_(AddDeviceLuks, "Format and use LUKS device");
	RUN_(UseLuksDevice, "Use pre-formated LUKS device");
	RUN_(SuspendDevice, "Suspend/Resume test");

	_cleanup();
	return 0;
}
