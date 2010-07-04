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

#include <libcryptsetup.h>
#include <popt.h>

#include "../config.h"

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
	char error[256], *error_;

	if(!opt_verbose)
		return;

	if(!errcode) {
		log_std(_("Command successful.\n"));
		return;
	}

	crypt_get_error(error, sizeof(error));

	if (!error[0]) {
		error_ = strerror_r(-errcode, error, sizeof(error));
		if (error_ != error) {
			strncpy(error, error_, sizeof(error));
			error[sizeof(error) - 1] = '\0';
		}
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

static void usage(poptContext popt_context, int exitcode,
                  const char *error, const char *more)
{
	poptPrintUsage(popt_context, stderr, 0);
	if (error)
		log_err("%s: %s\n", more, error);
	exit(exitcode);
}

static void help(poptContext popt_context, enum poptCallbackReason reason,
                 struct poptOption *key, const char * arg, void *data)
{
	if (key->shortName == '?') {
		struct action_type *action;

		log_std("%s\n",PACKAGE_STRING);

		poptPrintHelp(popt_context, stdout, 0);

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
	} else
		usage(popt_context, 0, NULL, NULL);
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
	static char *popt_tmp;
	static struct poptOption popt_help_options[] = {
		{ NULL,    '\0', POPT_ARG_CALLBACK, help, 0, NULL,                         NULL },
		{ "help",  '?',  POPT_ARG_NONE,     NULL, 0, N_("Show this help message"), NULL },
		{ "usage", '\0', POPT_ARG_NONE,     NULL, 0, N_("Display brief usage"),    NULL },
		POPT_TABLEEND
	};
	static struct poptOption popt_options[] = {
		{ NULL,                '\0', POPT_ARG_INCLUDE_TABLE, popt_help_options, 0, N_("Help options:"), NULL },
		{ "verbose",           'v',  POPT_ARG_NONE, &opt_verbose,               0, N_("Shows more detailed error messages"), NULL },
		{ "debug",             '\0', POPT_ARG_NONE, &opt_debug,                 0, N_("Show debug messages"), NULL },
		{ "cipher",            'c',  POPT_ARG_STRING, &opt_cipher,              0, N_("The cipher used to encrypt the disk (see /proc/crypto)"), NULL },
		{ "hash",              'h',  POPT_ARG_STRING, &opt_hash,                0, N_("The hash used to create the encryption key from the passphrase"), NULL },
		{ "verify-passphrase", 'y',  POPT_ARG_NONE, &opt_verify_passphrase,     0, N_("Verifies the passphrase by asking for it twice"), NULL },
		{ "key-file",          'd',  POPT_ARG_STRING, &opt_key_file,            0, N_("Read the key from a file (can be /dev/random)"), NULL },
		{ "master-key-file",  '\0',  POPT_ARG_STRING, &opt_master_key_file,     0, N_("Read the volume (master) key from file."), NULL },
		{ "key-size",          's',  POPT_ARG_INT, &opt_key_size,               0, N_("The size of the encryption key"), N_("BITS") },
		{ "key-slot",          'S',  POPT_ARG_INT, &opt_key_slot,               0, N_("Slot number for new key (default is first free)"), NULL },
		{ "size",              'b',  POPT_ARG_STRING, &popt_tmp,                1, N_("The size of the device"), N_("SECTORS") },
		{ "offset",            'o',  POPT_ARG_STRING, &popt_tmp,                2, N_("The start offset in the backend device"), N_("SECTORS") },
		{ "skip",              'p',  POPT_ARG_STRING, &popt_tmp,                3, N_("How many sectors of the encrypted data to skip at the beginning"), N_("SECTORS") },
		{ "readonly",          'r',  POPT_ARG_NONE, &opt_readonly,              0, N_("Create a readonly mapping"), NULL },
		{ "iter-time",         'i',  POPT_ARG_INT, &opt_iteration_time,         0, N_("PBKDF2 iteration time for LUKS (in ms)"), N_("msecs") },
		{ "batch-mode",        'q',  POPT_ARG_NONE, &opt_batch_mode,            0, N_("Do not ask for confirmation"), NULL },
		{ "version",           '\0', POPT_ARG_NONE, &opt_version_mode,          0, N_("Print package version"), NULL },
		{ "timeout",           't',  POPT_ARG_INT, &opt_timeout,                0, N_("Timeout for interactive passphrase prompt (in seconds)"), N_("secs") },
		{ "tries",             'T',  POPT_ARG_INT, &opt_tries,                  0, N_("How often the input of the passphrase can be retried"), NULL },
		{ "align-payload",     '\0', POPT_ARG_INT, &opt_align_payload,          0, N_("Align payload at <n> sector boundaries - for luksFormat"), N_("SECTORS") },
		{ "non-exclusive",     '\0', POPT_ARG_NONE, &opt_non_exclusive,         0, N_("(Obsoleted, see man page.)"), NULL },
		{ "header-backup-file",'\0', POPT_ARG_STRING, &opt_header_backup_file,  0, N_("File with LUKS header and keyslots backup."), NULL },
		POPT_TABLEEND
	};
	poptContext popt_context;
	struct action_type *action;
	char *aname;
	int r;
	const char *null_action_argv[] = {NULL};

	crypt_set_log_callback(NULL, _log, NULL);

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	popt_context = poptGetContext(PACKAGE, argc, (const char **)argv,
	                              popt_options, 0);
	poptSetOtherOptionHelp(popt_context,
	                       N_("[OPTION...] <action> <action-specific>]"));

	while((r = poptGetNextOpt(popt_context)) > 0) {
		unsigned long long ull_value;
		char *endp;

		ull_value = strtoull(popt_tmp, &endp, 0);
		if (*endp || !*popt_tmp)
			r = POPT_ERROR_BADNUMBER;

		switch(r) {
			case 1:
				opt_size = ull_value;
				break;
			case 2:
				opt_offset = ull_value;
				break;
			case 3:
				opt_skip = ull_value;
				break;
		}

		if (r < 0)
			break;
	}

	if (r < -1)
		usage(popt_context, 1, poptStrerror(r),
		      poptBadOption(popt_context, POPT_BADOPTION_NOALIAS));
	if (opt_version_mode) {
		log_std("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
		exit(0);
	}

	if (opt_key_size % 8)
		usage(popt_context, 1,
		      _("Key size must be a multiple of 8 bits"),
		      poptGetInvocationName(popt_context));

	if (!(aname = (char *)poptGetArg(popt_context)))
		usage(popt_context, 1, _("Argument <action> missing."),
		      poptGetInvocationName(popt_context));
	for(action = action_types; action->type; action++)
		if (strcmp(action->type, aname) == 0)
			break;
	if (!action->type)
		usage(popt_context, 1, _("Unknown action."),
		      poptGetInvocationName(popt_context));

	action_argc = 0;
	action_argv = poptGetArgs(popt_context);
	/* Make return values of poptGetArgs more consistent in case of remaining argc = 0 */
	if(!action_argv) 
		action_argv = null_action_argv;

	/* Count args, somewhat unnice, change? */
	while(action_argv[action_argc] != NULL)
		action_argc++;

	if(action_argc < action->required_action_argc) {
		char buf[128];
		snprintf(buf, 128,_("%s: requires %s as arguments"), action->type, action->arg_desc);
		usage(popt_context, 1, buf,
		      poptGetInvocationName(popt_context));
	}

	if (opt_debug) {
		opt_verbose = 1;
		crypt_set_debug_level(-1);
		_dbg_version_and_cmd(argc, argv);
	}

	return run_action(action);
}
