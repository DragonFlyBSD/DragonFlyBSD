#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <getopt.h>

#include <libcryptsetup.h>

#include "config.h"

#include "cryptsetup.h"

static int opt_verbose = 0;
static int opt_debug = 0;
static char *opt_cipher = NULL;
static char *opt_hash = NULL;
static int opt_verify_passphrase = 0;
static char *opt_key_file = NULL;
static char *opt_master_key_file = NULL;
static char *opt_header_backup_file = NULL;
static unsigned int opt_key_size = 0;
static int opt_key_slot = CRYPT_ANY_SLOT;
static uint64_t opt_size = 0;
static uint64_t opt_offset = 0;
static uint64_t opt_skip = 0;
static int opt_readonly = 0;
static int opt_iteration_time = 1000;
static int opt_batch_mode = 0;
static int opt_version_mode = 0;
static int opt_timeout = 0;
static int opt_tries = 3;
static int opt_align_payload = 0;
static int opt_non_exclusive = 0;

static const char **action_argv;
static int action_argc;

static int action_create(int arg);
static int action_remove(int arg);
static int action_resize(int arg);
static int action_status(int arg);
static int action_luksFormat(int arg);
static int action_luksOpen(int arg);
static int action_luksAddKey(int arg);
static int action_luksDelKey(int arg);
static int action_luksKillSlot(int arg);
static int action_luksRemoveKey(int arg);
static int action_isLuks(int arg);
static int action_luksUUID(int arg);
static int action_luksDump(int arg);
static int action_luksSuspend(int arg);
static int action_luksResume(int arg);
static int action_luksBackup(int arg);
static int action_luksRestore(int arg);

static struct action_type {
	const char *type;
	int (*handler)(int);
	int arg;
	int required_action_argc;
	int required_memlock;
	const char *arg_desc;
	const char *desc;
} action_types[] = {
	{ "create",	action_create,		0, 2, 1, N_("<name> <device>"),N_("create device") },
	{ "remove",	action_remove,		0, 1, 1, N_("<name>"), N_("remove device") },
	{ "resize",	action_resize,		0, 1, 1, N_("<name>"), N_("resize active device") },
	{ "status",	action_status,		0, 1, 0, N_("<name>"), N_("show device status") },
	{ "luksFormat", action_luksFormat,	0, 1, 1, N_("<device> [<new key file>]"), N_("formats a LUKS device") },
	{ "luksOpen",	action_luksOpen,	0, 2, 1, N_("<device> <name> "), N_("open LUKS device as mapping <name>") },
	{ "luksAddKey",	action_luksAddKey,	0, 1, 1, N_("<device> [<new key file>]"), N_("add key to LUKS device") },
	{ "luksRemoveKey",action_luksRemoveKey,	0, 1, 1, N_("<device> [<key file>]"), N_("removes supplied key or key file from LUKS device") },
	{ "luksKillSlot",  action_luksKillSlot, 0, 2, 1, N_("<device> <key slot>"), N_("wipes key with number <key slot> from LUKS device") },
	{ "luksUUID",	action_luksUUID,	0, 1, 0, N_("<device>"), N_("print UUID of LUKS device") },
	{ "isLuks",	action_isLuks,		0, 1, 0, N_("<device>"), N_("tests <device> for LUKS partition header") },
	{ "luksClose",	action_remove,		0, 1, 1, N_("<name>"), N_("remove LUKS mapping") },
	{ "luksDump",	action_luksDump,	0, 1, 0, N_("<device>"), N_("dump LUKS partition information") },
	{ "luksSuspend",action_luksSuspend,	0, 1, 1, N_("<device>"), N_("Suspend LUKS device and wipe key (all IOs are frozen).") },
	{ "luksResume",	action_luksResume,	0, 1, 1, N_("<device>"), N_("Resume suspended LUKS device.") },
	{ "luksHeaderBackup",action_luksBackup,	0, 1, 1, N_("<device>"), N_("Backup LUKS device header and keyslots") },
	{ "luksHeaderRestore",action_luksRestore,0,1, 1, N_("<device>"), N_("Restore LUKS device header and keyslots") },
	{ "luksDelKey", action_luksDelKey,	0, 2, 1, N_("<device> <key slot>"), N_("identical to luksKillSlot - DEPRECATED - see man page") },
	{ "reload",	action_create,		1, 2, 1, N_("<name> <device>"), N_("modify active device - DEPRECATED - see man page") },
	{ NULL, NULL, 0, 0, 0, NULL, NULL }
};

static void clogger(struct crypt_device *cd, int level, const char *file,
		   int line, const char *format, ...)
{
	va_list argp;
	char *target = NULL;

	va_start(argp, format);

	if (vasprintf(&target, format, argp) > 0) {
		if (level >= 0) {
			crypt_log(cd, level, target);
#ifdef CRYPT_DEBUG
		} else if (opt_debug)
			printf("# %s:%d %s\n", file ?: "?", line, target);
#else
		} else if (opt_debug)
			printf("# %s\n", target);
#endif
	}

	va_end(argp);
	free(target);
}

/* Interface Callbacks */
static int yesDialog(char *msg)
{
	char *answer = NULL;
	size_t size = 0;
	int r = 1;

	if(isatty(0) && !opt_batch_mode) {
		log_std("\nWARNING!\n========\n");
		log_std("%s\n\nAre you sure? (Type uppercase yes): ", msg);
		if(getline(&answer, &size, stdin) == -1) {
			perror("getline");
			free(answer);
			return 0;
		}
		if(strcmp(answer, "YES\n"))
			r = 0;
		free(answer);
	}

	return r;
}

static void cmdLineLog(int level, char *msg) {
	switch(level) {

	case CRYPT_LOG_NORMAL:
		fputs(msg, stdout);
		break;
	case CRYPT_LOG_VERBOSE:
		if (opt_verbose)
			fputs(msg, stdout);
		break;
	case CRYPT_LOG_ERROR:
		fputs(msg, stderr);
		break;
	default:
		fprintf(stderr, "Internal error on logging class for msg: %s", msg);
		break;
	}
}

static struct interface_callbacks cmd_icb = {
	.yesDialog = yesDialog,
	.log = cmdLineLog,
};

static void _log(int level, const char *msg, void *usrptr)
{
	cmdLineLog(level, (char *)msg);
}

static int _yesDialog(const char *msg, void *usrptr)
{
	return yesDialog((char*)msg);
}

/* End ICBs */

static void show_status(int errcode)
{
	char error[256];
	int ret;

	if(!opt_verbose)
		return;

	if(!errcode) {
		log_std(_("Command successful.\n"));
		return;
	}

	crypt_get_error(error, sizeof(error));

	if (!error[0]) {
		ret = strerror_r(-errcode, error, sizeof(error));
	}

	log_err(_("Command failed with code %i"), -errcode);
	if (*error)
		log_err(": %s\n", error);
	else
		log_err(".\n");
}

static int action_create(int reload)
{
	struct crypt_options options = {
		.name = action_argv[0],
		.device = action_argv[1],
		.cipher = opt_cipher ? opt_cipher : DEFAULT_CIPHER(PLAIN),
		.hash = opt_hash ?: DEFAULT_PLAIN_HASH,
		.key_file = opt_key_file,
		.key_size = (opt_key_size ?: DEFAULT_PLAIN_KEYBITS) / 8,
		.key_slot = opt_key_slot,
		.flags = 0,
		.size = opt_size,
		.offset = opt_offset,
		.skip = opt_skip,
		.timeout = opt_timeout,
		.tries = opt_tries,
		.icb = &cmd_icb,
	};
	int r;

        if(reload) 
                log_err(_("The reload action is deprecated. Please use \"dmsetup reload\" in case you really need this functionality.\nWARNING: do not use reload to touch LUKS devices. If that is the case, hit Ctrl-C now.\n"));

	if (options.hash && strcmp(options.hash, "plain") == 0)
		options.hash = NULL;
	if (opt_verify_passphrase)
		options.flags |= CRYPT_FLAG_VERIFY;
	if (opt_readonly)
		options.flags |= CRYPT_FLAG_READONLY;

	if (reload)
		r = crypt_update_device(&options);
	else
		r = crypt_create_device(&options);

	return r;
}

static int action_remove(int arg)
{
	struct crypt_options options = {
		.name = action_argv[0],
		.icb = &cmd_icb,
	};

	return crypt_remove_device(&options);
}

static int action_resize(int arg)
{
	struct crypt_options options = {
		.name = action_argv[0],
		.size = opt_size,
		.icb = &cmd_icb,
	};

	return crypt_resize_device(&options);
}

static int action_status(int arg)
{
	struct crypt_options options = {
		.name = action_argv[0],
		.icb = &cmd_icb,
	};
	int r;

	r = crypt_query_device(&options);
	if (r < 0)
		return r;

	if (r == 0) {
		/* inactive */
		log_std("%s/%s is inactive.\n", crypt_get_dir(), options.name);
		r = 1;
	} else {
		/* active */
		log_std("%s/%s is active:\n", crypt_get_dir(), options.name);
		log_std("  cipher:  %s\n", options.cipher);
		log_std("  keysize: %d bits\n", options.key_size * 8);
		log_std("  device:  %s\n", options.device ?: "");
		log_std("  offset:  %" PRIu64 " sectors\n", options.offset);
		log_std("  size:    %" PRIu64 " sectors\n", options.size);
		if (options.skip)
			log_std("  skipped: %" PRIu64 " sectors\n", options.skip);
		log_std("  mode:    %s\n", (options.flags & CRYPT_FLAG_READONLY)
		                           ? "readonly" : "read/write");
		crypt_put_options(&options);
		r = 0;
	}
	return r;
}

static int _action_luksFormat_generateMK()
{
	struct crypt_options options = {
		.key_size = (opt_key_size ?: DEFAULT_LUKS1_KEYBITS) / 8,
		.key_slot = opt_key_slot,
		.device = action_argv[0],
		.cipher = opt_cipher ?: DEFAULT_CIPHER(LUKS1),
		.hash = opt_hash ?: DEFAULT_LUKS1_HASH,
		.new_key_file = opt_key_file ?: (action_argc > 1 ? action_argv[1] : NULL),
		.flags = opt_verify_passphrase ? CRYPT_FLAG_VERIFY : (!opt_batch_mode?CRYPT_FLAG_VERIFY_IF_POSSIBLE :  0),
		.iteration_time = opt_iteration_time,
		.timeout = opt_timeout,
		.align_payload = opt_align_payload,
		.icb = &cmd_icb,
	};

	return crypt_luksFormat(&options);
}

static int _read_mk(const char *file, char **key, int keysize)
{
	int fd;

	*key = malloc(keysize);
	if (!*key)
		return -ENOMEM;

	fd = open(file, O_RDONLY);
	if (fd == -1) {
		log_err("Cannot read keyfile %s.\n", file);
		return -EINVAL;
	}
	if ((read(fd, *key, keysize) != keysize)) {
		log_err("Cannot read %d bytes from keyfile %s.\n", keysize, file);
		close(fd);
		memset(*key, 0, keysize);
		free(*key);
		return -EINVAL;
	}
	close(fd);
	return 0;
}

static int _action_luksFormat_useMK()
{
	int r = -EINVAL, keysize;
	char *key = NULL, cipher [MAX_CIPHER_LEN], cipher_mode[MAX_CIPHER_LEN];
	struct crypt_device *cd = NULL;
	struct crypt_params_luks1 params = {
		.hash = opt_hash ?: DEFAULT_LUKS1_HASH,
		.data_alignment = opt_align_payload,
	};

	if (sscanf(opt_cipher ?: DEFAULT_CIPHER(LUKS1),
		   "%" MAX_CIPHER_LEN_STR "[^-]-%" MAX_CIPHER_LEN_STR "s",
		   cipher, cipher_mode) != 2) {
		log_err("No known cipher specification pattern detected.\n");
		return -EINVAL;
	}

	keysize = (opt_key_size ?: DEFAULT_LUKS1_KEYBITS) / 8;
	if (_read_mk(opt_master_key_file, &key, keysize) < 0)
		return -EINVAL;

	if ((r = crypt_init(&cd, action_argv[0])))
		goto out;

	crypt_set_password_verify(cd, 1);
	crypt_set_timeout(cd, opt_timeout);
	if (opt_iteration_time)
		crypt_set_iterarion_time(cd, opt_iteration_time);

	if ((r = crypt_format(cd, CRYPT_LUKS1, cipher, cipher_mode, NULL, key, keysize, &params)))
		goto out;

	r = crypt_keyslot_add_by_volume_key(cd, opt_key_slot, key, keysize, NULL, 0);
out:

	crypt_free(cd);
	if (key) {
		memset(key, 0, keysize);
		free(key);
	}
	return r;
}

static int action_luksFormat(int arg)
{
	int r = 0; char *msg = NULL;

	/* Avoid overwriting possibly wrong part of device than user requested by rejecting these options */
	if (opt_offset || opt_skip) {
		log_err("Options --offset and --skip are not supported for luksFormat.\n"); 
		return -EINVAL;
	}

	if (action_argc > 1 && opt_key_file)
		log_err(_("Option --key-file takes precedence over specified key file argument.\n"));

	if(asprintf(&msg, _("This will overwrite data on %s irrevocably."), action_argv[0]) == -1) {
		log_err(_("memory allocation error in action_luksFormat"));
		return -ENOMEM;
	}
	r = yesDialog(msg);
	free(msg);

	if (!r)
		return -EINVAL;

	if (opt_master_key_file)
		return _action_luksFormat_useMK();
	else
		return _action_luksFormat_generateMK();
}

static int action_luksOpen(int arg)
{
	struct crypt_options options = {
		.name = action_argv[1],
		.device = action_argv[0],
		.key_file = opt_key_file,
		.key_size = opt_key_file ? (opt_key_size / 8) : 0, /* limit bytes read from keyfile */
		.timeout = opt_timeout,
		.tries = opt_key_file ? 1 : opt_tries, /* verify is usefull only for tty */
		.icb = &cmd_icb,
	};

	if (opt_readonly)
		options.flags |= CRYPT_FLAG_READONLY;
	if (opt_non_exclusive)
		log_err(_("Obsolete option --non-exclusive is ignored.\n"));

	return crypt_luksOpen(&options);
}

static int action_luksDelKey(int arg)
{
	log_err("luksDelKey is a deprecated action name.\nPlease use luksKillSlot.\n"); 
	return action_luksKillSlot(arg);
}

static int action_luksKillSlot(int arg)
{
	struct crypt_options options = {
		.device = action_argv[0],
		.key_slot = atoi(action_argv[1]),
		.key_file = opt_key_file,
		.timeout = opt_timeout,
		.flags = !opt_batch_mode?CRYPT_FLAG_VERIFY_ON_DELKEY : 0,
		.icb = &cmd_icb,
	};

	return crypt_luksKillSlot(&options);
}

static int action_luksRemoveKey(int arg)
{
	struct crypt_options options = {
		.device = action_argv[0],
		.new_key_file = action_argc>1?action_argv[1]:NULL,
		.key_file = opt_key_file,
		.timeout = opt_timeout,
		.flags = !opt_batch_mode?CRYPT_FLAG_VERIFY_ON_DELKEY : 0,
		.icb = &cmd_icb,
	};

	return crypt_luksRemoveKey(&options);
}

static int _action_luksAddKey_useMK()
{
	int r = -EINVAL, keysize = 0;
	char *key = NULL;
	struct crypt_device *cd = NULL;

	if ((r = crypt_init(&cd, action_argv[0])))
		goto out;

	if ((r = crypt_load(cd, CRYPT_LUKS1, NULL)))
		goto out;

	keysize = crypt_get_volume_key_size(cd);
	crypt_set_password_verify(cd, 1);
	crypt_set_timeout(cd, opt_timeout);
	if (opt_iteration_time)
		crypt_set_iterarion_time(cd, opt_iteration_time);

	if (_read_mk(opt_master_key_file, &key, keysize) < 0)
		goto out;

	r = crypt_keyslot_add_by_volume_key(cd, opt_key_slot, key, keysize, NULL, 0);
out:
	crypt_free(cd);
	if (key) {
		memset(key, 0, keysize);
		free(key);
	}
	return r;
}

static int action_luksAddKey(int arg)
{
	struct crypt_options options = {
		.device = action_argv[0],
		.new_key_file = action_argc>1?action_argv[1]:NULL,
		.key_file = opt_key_file,
		.key_slot = opt_key_slot,
		.flags = opt_verify_passphrase ? CRYPT_FLAG_VERIFY : (!opt_batch_mode?CRYPT_FLAG_VERIFY_IF_POSSIBLE : 0),
		.iteration_time = opt_iteration_time,
		.timeout = opt_timeout,
		.icb = &cmd_icb,
	};

	if (opt_master_key_file)
		return _action_luksAddKey_useMK();
	else
		return crypt_luksAddKey(&options);
}

static int action_isLuks(int arg)
{
	struct crypt_options options = {
		.device = action_argv[0],
		.icb = &cmd_icb,
	};

	return crypt_isLuks(&options);
}

static int action_luksUUID(int arg)
{
	struct crypt_options options = {
		.device = action_argv[0],
		.icb = &cmd_icb,
	};

	return crypt_luksUUID(&options);
}

static int action_luksDump(int arg)
{
	struct crypt_options options = {
		.device = action_argv[0],
		.icb = &cmd_icb,
	};

	return crypt_luksDump(&options);
}

static int action_luksSuspend(int arg)
{
	struct crypt_device *cd = NULL;
	int r;

	r = crypt_init_by_name(&cd, action_argv[0]);
	if (!r)
		r = crypt_suspend(cd, action_argv[0]);

	crypt_free(cd);
	return r;
}

static int action_luksResume(int arg)
{
	struct crypt_device *cd = NULL;
	int r;

	if ((r = crypt_init_by_name(&cd, action_argv[0])))
		goto out;

	if ((r = crypt_load(cd, CRYPT_LUKS1, NULL)))
		goto out;

	if (opt_key_file)
		r = crypt_resume_by_keyfile(cd, action_argv[0], CRYPT_ANY_SLOT,
					    opt_key_file, opt_key_size / 8);
	else
		r = crypt_resume_by_passphrase(cd, action_argv[0], CRYPT_ANY_SLOT,
					       NULL, 0);
out:
	crypt_free(cd);
	return r;
}

static int action_luksBackup(int arg)
{
	struct crypt_device *cd = NULL;
	int r;

	if (!opt_header_backup_file) {
		log_err(_("Option --header-backup-file is required.\n"));
		return -EINVAL;
	}

	if ((r = crypt_init(&cd, action_argv[0])))
		goto out;

	crypt_set_log_callback(cd, _log, NULL);
	crypt_set_confirm_callback(cd, _yesDialog, NULL);

	r = crypt_header_backup(cd, CRYPT_LUKS1, opt_header_backup_file);
out:
	crypt_free(cd);
	return r;
}

static int action_luksRestore(int arg)
{
	struct crypt_device *cd = NULL;
	int r = 0;

	if (!opt_header_backup_file) {
		log_err(_("Option --header-backup-file is required.\n"));
		return -EINVAL;
	}

	if ((r = crypt_init(&cd, action_argv[0])))
		goto out;

	crypt_set_log_callback(cd, _log, NULL);
	crypt_set_confirm_callback(cd, _yesDialog, NULL);
	r = crypt_header_restore(cd, CRYPT_LUKS1, opt_header_backup_file);
out:
	crypt_free(cd);
	return r;
}

static void usage(const char *msg)
{
	log_err("Usage: cryptsetup [-?vyrq] [-?|--help] [--usage] [-v|--verbose]\n"
	    "        [--debug] [-c|--cipher=STRING] [-h|--hash=STRING]\n"
	    "        [-y|--verify-passphrase] [-d|--key-file=STRING]\n"
	    "        [--master-key-file=STRING] [-s|--key-size=BITS] [-S|--key-slot=INT]\n"
            "        [-b|--size=SECTORS] [-o|--offset=SECTORS] [-p|--skip=SECTORS]\n"
            "        [-r|--readonly] [-i|--iter-time=msecs] [-q|--batch-mode] [--version]\n"
            "        [-t|--timeout=secs] [-T|--tries=INT] [--align-payload=SECTORS]\n"
            "        [--non-exclusive] [--header-backup-file=STRING] [OPTION...]\n"
            "        <action> <action-specific>]\n");

	if (msg)
		log_err("%s\n", msg);

	exit(1);
}

static void help()
{
	struct action_type *action;

	log_std("%s\n",PACKAGE_STRING);
	log_std("Usage: cryptsetup [OPTION...] <action> <action-specific>]\n"
	    "  -v, --verbose                       Shows more detailed error messages\n"
	    "      --debug                         Show debug messages\n"
	    "  -c, --cipher=STRING                 The cipher used to encrypt the disk (see /proc/crypto)\n"
	    "  -h, --hash=STRING                   The hash used to create the encryption key from the passphrase\n"
	    "  -y, --verify-passphrase             Verifies the passphrase by asking for it twice\n"
	    "  -d, --key-file=STRING               Read the key from a file (can be /dev/random)\n"
	    "      --master-key-file=STRING        Read the volume (master) key from file.\n"
	    "  -s, --key-size=BITS                 The size of the encryption key\n"
	    "  -S, --key-slot=INT                  Slot number for new key (default is first free)\n"
	    "  -b, --size=SECTORS                  The size of the device\n"
	    "  -o, --offset=SECTORS                The start offset in the backend device\n"
	    "  -p, --skip=SECTORS                  How many sectors of the encrypted data to skip at the beginning\n"
	    "  -r, --readonly                      Create a readonly mapping\n"
	    "  -i, --iter-time=msecs               PBKDF2 iteration time for LUKS (in ms)\n"
	    "  -q, --batch-mode                    Do not ask for confirmation\n"
	    "      --version                       Print package version\n"
	    "  -t, --timeout=secs                  Timeout for interactive passphrase prompt (in seconds)\n"
	    "  -T, --tries=INT                     How often the input of the passphrase can be retried\n"
	    "      --align-payload=SECTORS         Align payload at <n> sector boundaries - for luksFormat\n"
	    "      --non-exclusive                 (Obsoleted, see man page.)\n"
	    "      --header-backup-file=STRING     File with LUKS header and keyslots backup.\n"
	    "\n"
	    "Help options:\n"
	    "  -?, --help                          Show this help message\n"
	    "      --usage                         Display brief usage\n" );

	log_std(_("\n"
		 "<action> is one of:\n"));

	for(action = action_types; action->type; action++)
		log_std("\t%s %s - %s\n", action->type, _(action->arg_desc), _(action->desc));
		
	log_std(_("\n"
		 "<name> is the device to create under %s\n"
		 "<device> is the encrypted device\n"
		 "<key slot> is the LUKS key slot number to modify\n"
		 "<key file> optional key file for the new key for luksAddKey action\n"),
		crypt_get_dir());

	log_std(_("\nDefault compiled-in device cipher parameters:\n"
		 "\tplain: %s, Key: %d bits, Password hashing: %s\n"
		 "\tLUKS1: %s, Key: %d bits, LUKS header hashing: %s\n"),
		 DEFAULT_CIPHER(PLAIN), DEFAULT_PLAIN_KEYBITS, DEFAULT_PLAIN_HASH,
		 DEFAULT_CIPHER(LUKS1), DEFAULT_LUKS1_KEYBITS, DEFAULT_LUKS1_HASH);
	exit(0);

}

void set_debug_level(int level);

static void _dbg_version_and_cmd(int argc, char **argv)
{
	int i;

	log_std("# %s %s processing \"", PACKAGE_NAME, PACKAGE_VERSION);
	for (i = 0; i < argc; i++) {
		if (i)
			log_std(" ");
		log_std(argv[i]);
	}
	log_std("\"\n");
}

static int run_action(struct action_type *action)
{
	int r;

	if (action->required_memlock)
		crypt_memory_lock(NULL, 1);

	r = action->handler(action->arg);

	if (action->required_memlock)
		crypt_memory_lock(NULL, 0);

	show_status(r);

	return r;
}

int main(int argc, char **argv)
{
	static struct option options[] = {
		{ "help",  	       no_argument, 		NULL, '?' },
		{ "usage", 	       no_argument, 		NULL, 'u' },
		{ "verbose",           no_argument, 		NULL, 'v' },
		{ "debug",             no_argument, 		&opt_debug, 1 },
		{ "cipher",            required_argument, 	NULL, 'c' },
		{ "hash",              required_argument, 	NULL, 'h' },
		{ "verify-passphrase", no_argument, 		NULL, 'y' },
		{ "key-file",          required_argument, 	NULL, 'd' },
		{ "master-key-file",   required_argument, 	NULL, 'm' },
		{ "key-size",          required_argument, 	NULL, 's' },
		{ "key-slot",          required_argument, 	NULL, 'S' },
		{ "size",              required_argument, 	NULL, 'b' },
		{ "offset",            required_argument, 	NULL, 'o' },
		{ "skip",              required_argument,	NULL, 'p' },
		{ "readonly",          no_argument, 		NULL, 'r' },
		{ "iter-time",         required_argument,	NULL, 'i' },
		{ "batch-mode",        no_argument,		NULL, 'q' },
		{ "version",           no_argument, 		&opt_version_mode, 1 },
		{ "timeout",           required_argument, 	NULL, 't' },
		{ "tries",             required_argument, 	NULL, 'T' },
		{ "align-payload",     required_argument, 	NULL, 'a' },
		{ "header-backup-file",required_argument, 	NULL, 'x' },
		{ NULL,			0,			NULL, 0 }
	};
	struct action_type *action;
	char *aname;
	int r;
	const char *null_action_argv[] = {NULL};

	crypt_set_log_callback(NULL, _log, NULL);

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while((r = getopt_long(argc, argv, "?vc:h:yd:m:s:S:b:o:p:ri:qt:T:", options, NULL)) != -1)
	{
		switch (r) {
		case 'u':
			usage(NULL);
			break;
		case 'v':
			opt_verbose = 1;
			break;
		case 'c':
			opt_cipher = optarg;
			break;
		case 'h':
			opt_hash = optarg;
			break;
		case 'y':
			opt_verify_passphrase = 1;
			break;
		case 'd':
			opt_key_file = optarg;
			break;
		case 'm':
			opt_master_key_file = optarg;
			break;
		case 's':
			opt_key_size = atoi(optarg);
			break;
		case 'S':
			opt_key_slot = atoi(optarg);
			break;
		case 'b':
			opt_size = strtoull(optarg, NULL, 0);
			break;
		case 'o':
			opt_offset = strtoull(optarg, NULL, 0);
			break;
		case 'p':
			opt_skip = strtoull(optarg, NULL, 0);
			break;
		case 'r':
			opt_readonly = 1;
			break;
		case 'i':
			opt_iteration_time = atoi(optarg);
			break;
		case 'q':
			opt_batch_mode = 1;
			break;
		case 't':
			opt_timeout = atoi(optarg);
			break;
		case 'T':
			opt_tries = atoi(optarg);
			break;
		case 'a':
			opt_align_payload = atoi(optarg);
			break;
		case 'x':
			opt_header_backup_file = optarg;
			break;
		case '?':
			help();
			break;
		case 0:
			if (opt_version_mode) {
				log_std("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
				exit(0);
			}
			break;
		}
	}

	if (opt_key_size % 8)
		usage(_("Key size must be a multiple of 8 bits"));

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		usage(_("Argument <action> missing."));
		/* NOTREACHED */	
	}

	aname = argv[0];
	for(action = action_types; action->type; action++)
		if (strcmp(action->type, aname) == 0)
			break;
	if (!action->type) {
		usage( _("Unknown action."));
		/* NOTREACHED */
	}

	action_argc = argc-1;
	action_argv = (void *)&argv[1];

	if(action_argc < action->required_action_argc) {
		char buf[128];
		snprintf(buf, 128,_("%s: requires %s as arguments"), action->type, action->arg_desc);
		usage(buf);
		/* NOTREACHED */
	}

	if (opt_debug) {
		opt_verbose = 1;
		crypt_set_debug_level(-1);
		_dbg_version_and_cmd(argc, argv);
	}

	return run_action(action);
}
