/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "tcplay.h"

#ifndef SIGINFO
#define SIGINFO SIGUSR1
#endif

#define FLAG_LONG_FDE		0xff01
#define FLAG_LONG_USE_BACKUP	0xff02
#define FLAG_LONG_MOD		0xff04
#define FLAG_LONG_MOD_KF	0xff08
#define FLAG_LONG_MOD_PRF	0xff10
#define FLAG_LONG_MOD_NONE	0xff20
#define FLAG_LONG_MOD_TO_FILE	0xff40
#define FLAG_LONG_USE_HDR_FILE	0xfe01
#define FLAG_LONG_USE_HHDR_FILE	0xfe02
#define FLAG_LONG_NO_RETRIES	0xfabc


static
void
sig_handler(int sig)
{
	if ((sig == SIGUSR1 || sig == SIGINFO) && (summary_fn != NULL))
		summary_fn();
}

static
void
usage(void)
{
	fprintf(stderr,
	    "usage: tcplay -c -d device [-g] [-z] [-w] [-a pbkdf_hash] [-b cipher]\n"
	    "              [-f keyfile_hidden] [-k keyfile] [-x pbkdf_hash] [-y cipher]\n"
	    "       tcplay -i -d device [-e] [-f keyfile_hidden] [-k keyfile]\n"
	    "              [-s system_device] [--fde] [--use-backup]\n"
	    "              [--use-hdr-file=hdr_file] [--use-hidden-hdr-file=hdr_file]\n"
	    "       tcplay -m mapping -d device [-e] [-f keyfile_hidden] [-k keyfile]\n"
	    "              [-s system_device] [--fde] [--use-backup] [--allow-trim]\n"
	    "              [--use-hdr-file=hdr_file] [--use-hidden-hdr-file=hdr_file]\n"
	    "       tcplay --modify -d device [-k keyfile] [--new-keyfile=keyfile]\n"
	    "              [--new-pbkdf-prf=pbkdf_hash] [-s system_device] [--fde]\n"
	    "              [--use-backup] [--save-hdr-to-file=hdr_file] [-w]\n"
	    "              [--use-hdr-file=hdr_file] [--use-hidden-hdr-file=hdr_file]\n"
	    "       tcplay --modify -d device [-k keyfile] --restore-from-backup-hdr [-w]\n"
	    "       tcplay -j mapping\n"
	    "       tcplay -u mapping\n"
	    "       tcplay -h | -v\n"
	    "\n"
	    "Valid commands are:\n"
	    " -c, --create\n"
	    "\t Creates a new TC volume on the device specified by -d or --device.\n"
	    " -h, --help\n"
	    "\t Print help message and exit.\n"
	    " -i, --info\n"
	    "\t Gives information about the TC volume specified by -d or --device.\n"
	    " -j <mapping name>, --info-mapped=<mapping name>\n"
	    "\t Gives information about the mapped TC volume under the given mapping.\n"
	    " -m <mapping name>, --map=<mapping name>\n"
	    "\t Creates a dm-crypt mapping with the given name for the device\n"
	    "\t specified by -d or --device.\n"
	    " -u <mapping name>, --unmap=<mapping name>\n"
	    "\t Removes a dm-crypt mapping with the given name.\n"
	    " --modify\n"
	    "\t Changes the volume's passphrase, keyfile and optionally the hashing\n"
	    "\t function used for the PBKDF password derivation.\n"
	    " -v, --version\n"
	    "\t Print version message and exit.\n"
	    "\n"
	    "Valid options for --create are:\n"
	    " -a <pbkdf prf algorithm>, --pbkdf-prf=<pbkdf prf algorithm>\n"
	    "\t Specifies which hashing function to use for the PBKDF password\n"
	    "\t derivation when creating a new volume.\n"
	    "\t To see valid options, specify '-a help'.\n"
	    " -b <cipher>, --cipher=<cipher>\n"
	    "\t Specifies which cipher to use when creating a new TC volume.\n"
	    "\t To see valid options, specify '-b help'.\n"
	    " -g, --hidden\n"
	    "\t Specifies that the newly created volume will contain a hidden volume.\n"
	    " -x <pbkdf prf algorithm>, --pbkdf-prf=<pbkdf prf algorithm>\n"
	    "\t Specifies which hashing function to use for the PBKDF password\n"
	    "\t derivation when creating a new hidden volume.  By default, the\n"
	    "\t same as for the outer volume will be used.\n"
	    "\t To see valid options, specify '-x help'.\n"
	    " -y <cipher>, --cipher=<cipher>\n"
	    "\t Specifies which cipher to use when creating a new hidden volume.\n"
	    "\t By default, the same as for the outer volume will be used.\n"
	    "\t To see valid options, specify '-y help'.\n"
	    " -z, --insecure-erase\n"
	    "\t Skips the erase of the disk. Possible security hazard.\n"
	    " -w, --weak-keys\n"
	    "\t Uses a weak source of entropy (urandom) for key material.\n"
	    "\t WARNING: This is a REALLY REALLY bad idea for anything but\n"
	    "\t testing.\n"
	    "\n"
	    "Valid options for --modify are:\n"
	    " --new-keyfile=<key file>\n"
	    "\t Specifies a key file to use for the password derivation, when\n"
	    "\t re-encrypting the header, can appear multiple times.\n"
	    " --new-pbkdf-prf=<pbkdf prf algorithm>\n"
	    "\t Specifies which hashing function to use for the PBKDF password\n"
	    "\t derivation when re-encrypting the header.\n"
	    "\t To see valid options, specify '-a help'.\n"
	    " -s <disk path>, --system-encryption=<disk path>\n"
	    "\t Specifies that the disk (e.g. /dev/da0) is using system encryption.\n"
	    " --fde\n"
	    "\t Specifies that the disk (e.g. /dev/da0) is using full disk encryption.\n"
	    " --use-backup\n"
	    "\t Uses the backup headers (at the end of the volume) instead of the\n"
	    "\t primary headers. Both normal and backup headers will be modified!\n"
	    "\t This is useful when your primary headers have been corrupted.\n"
	    " --use-hdr-file=<header file>\n"
	    "\t Use the header in the specified file instead of the main header on the\n"
	    "\t disk as source for the modify operation.\n"
	    " --use-hidden-hdr-file=<header file>\n"
	    "\t Use the header in the specified file instead of the hidden header on the\n"
	    "\t disk as source for the modify operation.\n"
	    " --restore-from-backup-hdr\n"
	    "\t Implies --use-backup, no new PBKDF hashing function, no new keyfiles\n"
	    "\t and no new passphrase.\n"
	    "\t In other words, this will simply restore both headers from the backup\n"
	    "\t header. This option cannot be used to restore from a backup header file.\n"
	    " -w, --weak-keys\n"
	    "\t Uses a weak source of entropy (urandom) for salt material. The\n"
	    "\t key material is not affected, as the master keys are kept intact.\n"
	    "\t WARNING: This is a bad idea for anything but testing.\n"
	    " --save-hdr-backup=<header file>\n"
	    "\t Saves the modified header in the specified file instead of updating\n"
	    "\t the header files on disk.\n"
	    "\n"
	    "Valid options for --info and --map are:\n"
	    " -e, --protect-hidden\n"
	    "\t Protect a hidden volume when mounting the outer volume.\n"
	    " -s <disk path>, --system-encryption=<disk path>\n"
	    "\t Specifies that the disk (e.g. /dev/da0) is using system encryption.\n"
	    " -t, --allow-trim\n"
	    "\t Allow discards (TRIM command) on mapped volume.\n"
	    " --fde\n"
	    "\t Specifies that the disk (e.g. /dev/da0) is using full disk encryption.\n"
	    " --use-backup\n"
	    "\t Uses the backup headers (at the end of the volume) instead of the\n"
	    "\t primary headers.\n"
	    "\t This is useful when your primary headers have been corrupted.\n"
	    " --use-hdr-file=<header file>\n"
	    "\t Use the header in the specified file instead of the main header on the\n"
	    "\t disk.\n"
	    " --use-hidden-hdr-file=<header file>\n"
	    "\t Use the header in the specified file instead of the hidden header on the\n"
	    "\t disk.\n"
	    "\n"
	    "Valid options common to all commands are:\n"
	    " -d <device path>, --device=<device path>\n"
	    "\t Specifies the path to the volume to operate on (e.g. /dev/da0s1).\n"
	    " -f <key file>, --keyfile-hidden=<key file>\n"
	    "\t Specifies a key file to use for the hidden volume password derivation.\n"
	    "\t This option is only valid in combination with -e, --protect-hidden\n"
	    "\t or -g, --hidden.\n"
	    " -k <key file>, --keyfile=<key file>\n"
	    "\t Specifies a key file to use for the password derivation, can appear\n"
	    "\t multiple times.\n"
	    );

	exit(EXIT_FAILURE);
}

static struct option longopts[] = {
	{ "create",		no_argument,		NULL, 'c' },
	{ "cipher",		required_argument,	NULL, 'b' },
	{ "cipher-hidden",	required_argument,	NULL, 'y' },
	{ "hidden",		no_argument,		NULL, 'g' },
	{ "pbkdf-prf",		required_argument,	NULL, 'a' },
	{ "pbkdf-prf-hidden",	required_argument,	NULL, 'x' },
	{ "info",		no_argument,		NULL, 'i' },
	{ "info-mapped",	required_argument,	NULL, 'j' },
	{ "map",		required_argument,	NULL, 'm' },
	{ "keyfile",		required_argument,	NULL, 'k' },
	{ "keyfile-hidden",	required_argument,	NULL, 'f' },
	{ "protect-hidden",	no_argument,		NULL, 'e' },
	{ "device",		required_argument,	NULL, 'd' },
	{ "system-encryption",	required_argument,	NULL, 's' },
	{ "allow-trim",		no_argument,		NULL, 't' },
	{ "fde",		no_argument,		NULL, FLAG_LONG_FDE },
	{ "use-backup",		no_argument,		NULL, FLAG_LONG_USE_BACKUP },
	{ "use-hdr-file",	required_argument,	NULL, FLAG_LONG_USE_HDR_FILE },
	{ "use-hidden-hdr-file",required_argument,	NULL, FLAG_LONG_USE_HHDR_FILE },
	{ "modify",		no_argument,		NULL, FLAG_LONG_MOD },
	{ "new-keyfile",	required_argument,	NULL, FLAG_LONG_MOD_KF },
	{ "new-pbkdf-prf",	required_argument,	NULL, FLAG_LONG_MOD_PRF },
	{ "restore-from-backup-hdr", no_argument,	NULL, FLAG_LONG_MOD_NONE },
	{ "save-hdr-backup",	required_argument,	NULL, FLAG_LONG_MOD_TO_FILE },
	{ "unmap",		required_argument,	NULL, 'u' },
	{ "version",		no_argument,		NULL, 'v' },
	{ "weak-keys",		no_argument,		NULL, 'w' },
	{ "insecure-erase",	no_argument,		NULL, 'z' },
	{ "help",		no_argument,		NULL, 'h' },
	{ "no-retries",         no_argument,            NULL, FLAG_LONG_NO_RETRIES },
	{ NULL,			0,			NULL, 0   },
};

#define _set_str_opt(opt) \
	do {									\
		if ((opts->opt = strdup_safe_mem(optarg)) == NULL) {		\
			fprintf(stderr, "Could not allocate safe mem.\n");	\
			exit(EXIT_FAILURE);					\
		}								\
	} while(0)

int
main(int argc, char *argv[])
{
	struct tcplay_opts *opts;
	int ch, error;
	int info_vol = 0, map_vol = 0,
	    unmap_vol = 0, info_map = 0,
	    create_vol = 0, modify_vol = 0;

	if ((error = tc_play_init()) != 0) {
		fprintf(stderr, "Initialization failed, exiting.");
		exit(EXIT_FAILURE);
	}

	atexit(check_and_purge_safe_mem);
	signal(SIGUSR1, sig_handler);
	signal(SIGINFO, sig_handler);

	if ((opts = opts_init()) == NULL) {
		fprintf(stderr, "Initialization failed (opts), exiting.");
		exit(EXIT_FAILURE);
	}

	opts->interactive = 1;

	while ((ch = getopt_long(argc, argv, "a:b:cd:ef:ghij:k:m:s:tu:vwx:y:z",
	    longopts, NULL)) != -1) {
		switch(ch) {
		case 'a':
			if (opts->prf_algo != NULL)
				usage();
			if ((opts->prf_algo = check_prf_algo(optarg, 0)) == NULL) {
				if (strcmp(optarg, "help") == 0)
					exit(EXIT_SUCCESS);
				else
					usage();
				/* NOT REACHED */
			}
			break;
		case 'b':
			if (opts->cipher_chain != NULL)
				usage();
			if ((opts->cipher_chain = check_cipher_chain(optarg, 0)) == NULL) {
				if (strcmp(optarg, "help") == 0)
					exit(EXIT_SUCCESS);
				else
					usage();
				/* NOT REACHED */
			}
			break;
		case 'c':
			create_vol = 1;
			break;
		case 'd':
			_set_str_opt(dev);
			break;
		case 'e':
			opts->protect_hidden = 1;
			break;
		case 'f':
			if ((error = opts_add_keyfile_hidden(opts, optarg)) != 0) {
				fprintf(stderr, "Could not add keyfile: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'g':
			opts->hidden = 1;
			break;
		case 'i':
			info_vol = 1;
			break;
		case 'j':
			info_map = 1;
			_set_str_opt(map_name);
			break;
		case 'k':
			if ((error = opts_add_keyfile(opts, optarg)) != 0) {
				fprintf(stderr, "Could not add keyfile: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'm':
			map_vol = 1;
			_set_str_opt(map_name);
			break;
		case 's':
			opts->flags |= TC_FLAG_SYS;
			_set_str_opt(sys_dev);
			break;
		case 't':
			opts->flags |= TC_FLAG_ALLOW_TRIM;
			break;
		case 'u':
			unmap_vol = 1;
			_set_str_opt(map_name);
			break;
		case 'v':
			printf("tcplay v%d.%d\n", MAJ_VER, MIN_VER);
			exit(EXIT_SUCCESS);
			/* NOT REACHED */
		case 'w':
			fprintf(stderr, "WARNING: Using urandom as source of "
			    "entropy for key material is a really bad idea.\n");
			opts->weak_keys_and_salt = 1;
			break;
		case 'x':
			if (opts->h_prf_algo != NULL)
				usage();
			if ((opts->h_prf_algo = check_prf_algo(optarg, 0)) == NULL) {
				if (strcmp(optarg, "help") == 0)
					exit(EXIT_SUCCESS);
				else
					usage();
				/* NOT REACHED */
			}
			break;
		case 'y':
			if (opts->h_cipher_chain != NULL)
				usage();
			if ((opts->h_cipher_chain = check_cipher_chain(optarg, 0)) == NULL) {
				if (strcmp(optarg, "help") == 0)
					exit(EXIT_SUCCESS);
				else
					usage();
				/* NOT REACHED */
			}
			break;
		case 'z':
			opts->secure_erase = 0;
			break;
		case FLAG_LONG_FDE:
			opts->flags |= TC_FLAG_FDE;
			break;
		case FLAG_LONG_USE_BACKUP:
			opts->flags |= TC_FLAG_BACKUP;
			break;
		case FLAG_LONG_USE_HDR_FILE:
			opts->flags |= TC_FLAG_HDR_FROM_FILE;
			_set_str_opt(hdr_file_in);
			break;
		case FLAG_LONG_USE_HHDR_FILE:
			opts->flags |= TC_FLAG_H_HDR_FROM_FILE;
			_set_str_opt(h_hdr_file_in);
			break;
		case FLAG_LONG_MOD:
			modify_vol = 1;
			break;
		case FLAG_LONG_MOD_KF:
			if ((error = opts_add_keyfile_new(opts, optarg)) != 0) {
				fprintf(stderr, "Could not add keyfile: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case FLAG_LONG_MOD_PRF:
			if (opts->new_prf_algo != NULL)
				usage();
			if ((opts->new_prf_algo = check_prf_algo(optarg, 0)) == NULL) {
				if (strcmp(optarg, "help") == 0)
					exit(EXIT_SUCCESS);
				else
					usage();
				/* NOT REACHED */
			}
			break;
		case FLAG_LONG_MOD_NONE:
			opts->new_prf_algo = NULL;
			opts->flags |= TC_FLAG_ONLY_RESTORE;
			opts->flags |= TC_FLAG_BACKUP;
			break;
		case FLAG_LONG_MOD_TO_FILE:
			opts->flags |= TC_FLAG_SAVE_TO_FILE;
			_set_str_opt(hdr_file_out);
			break;
		case FLAG_LONG_NO_RETRIES:
			opts->retries = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
			/* NOT REACHED */
		}
	}

	argc -= optind;
	argv += optind;

	/* Check arguments */
	if (!(((map_vol || info_vol || create_vol || modify_vol) && opts->dev != NULL) ||
	    ((unmap_vol || info_map) && opts->map_name != NULL)) ||
	    (TC_FLAG_SET(opts->flags, SYS) && TC_FLAG_SET(opts->flags, FDE)) ||
	    (map_vol + info_vol + create_vol + unmap_vol + info_map + modify_vol > 1) ||
	    (opts->hidden && !create_vol) ||
	    (TC_FLAG_SET(opts->flags, SYS) && (opts->sys_dev == NULL)) ||
	    (TC_FLAG_SET(opts->flags, ONLY_RESTORE) && (opts->n_newkeyfiles > 0 || opts->new_prf_algo != NULL)) ||
	    (TC_FLAG_SET(opts->flags, BACKUP) && (opts->sys_dev != NULL || TC_FLAG_SET(opts->flags, FDE))) ||
	    (map_vol && (opts->map_name == NULL)) ||
	    (unmap_vol && (opts->map_name == NULL)) ||
	    (!modify_vol && opts->n_newkeyfiles > 0) ||
	    (!modify_vol && opts->new_prf_algo != NULL) ||
	    (!modify_vol && TC_FLAG_SET(opts->flags, ONLY_RESTORE)) ||
	    (!modify_vol && TC_FLAG_SET(opts->flags, SAVE_TO_FILE)) ||
	    (!(opts->protect_hidden || create_vol) && opts->n_hkeyfiles > 0)) {
		usage();
		/* NOT REACHED */
	}

	/* Create a new volume */
	if (create_vol) {
		error = create_volume(opts);
		if (error) {
			tc_log(1, "could not create new volume on %s\n", opts->dev);
		}
	} else if (info_map) {
		error = info_mapped_volume(opts);
	} else if (info_vol) {
		error = info_volume(opts);
	} else if (map_vol) {
		error = map_volume(opts);
	} else if (unmap_vol) {
		error = dm_teardown(opts->map_name, NULL);
	} else if (modify_vol) {
		error = modify_volume(opts);
	}

	return error;
}

#undef _set_str_opt
