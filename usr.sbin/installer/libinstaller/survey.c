/*
 * Copyright (c)2004 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of the DragonFly Project nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * survey.c
 * Survey the storage capacity of the system.
 * $Id: survey.c,v 1.17 2005/02/06 21:05:18 cpressey Exp $
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <stdio.h>
#include <string.h>

#include "libaura/dict.h"

#include "commands.h"
#include "diskutil.h"
#include "functions.h"

static int	fgets_chomp(char *, int, FILE *);
static int	parse_geometry_info(char *, int *, int *, int *);
static int	parse_slice_info(char *, int *,
		    unsigned long *, unsigned long *, int *, int *);

/*
 * Get a line from a file.  Remove any trailing EOL's.
 * Return 1 if we did not hit EOF, 0 if we did.
 */
static int
fgets_chomp(char *line, int size, FILE *f)
{
	if (fgets(line, size, f) == NULL)
		return(0);
	while (strlen(line) > 0 && line[strlen(line) - 1] == '\n')
		line[strlen(line) - 1] = '\0';
	return(1);
}

/*
 * Given a geometry line from fdisk's summary output, return the
 * number of cylinders, heads, and sectors.
 */
static int
parse_geometry_info(char *line, int *cyl, int *head, int *sec)
{
	char *word;

	/*
	 * /dev/ad3: 2112 cyl 16 hd 63 sec
	 */
	if ((word = strtok(line, " \t")) == NULL)	/* /dev/ad3: */
		return(0);
	if ((word = strtok(NULL, " \t")) == NULL)	/* 2112 */
		return(0);
	*cyl = atoi(word);
	if ((word = strtok(NULL, " \t")) == NULL)	/* cyl */
		return(0);
	if ((word = strtok(NULL, " \t")) == NULL)	/* 16 */
		return(0);
	*head = atoi(word);
	if ((word = strtok(NULL, " \t")) == NULL)	/* hd */
		return(0);
	if ((word = strtok(NULL, " \t")) == NULL)	/* 63 */
		return(0);
	*sec = atoi(word);

	return(1);
}

/*
 * Given a slice description line from fdisk's summary output, return
 * the number of the slice, and its start, size, type, and flags.
 */
static int
parse_slice_info(char *line, int *slice,
		 unsigned long *start, unsigned long *size,
		 int *type, int *flags)
{
	char *word;

	/*
	 * Part        Start        Size Type Flags
	 *    1:          63     2128833 0xa5 0x80
	 */
	if ((word = strtok(line, " \t")) == NULL)	/* 1: */
		return(0);
	*slice = atoi(word);
	if ((word = strtok(NULL, " \t")) == NULL)	/* 63 */
		return(0);
	*start = strtoul(word, NULL, 10);
	if ((word = strtok(NULL, " \t")) == NULL)	/* 2128833 */
		return(0);
	*size = strtoul(word, NULL, 10);
	if ((word = strtok(NULL, " \t")) == NULL)	/* 0xa5 */
		return(0);
	if (!hex_to_int(word, type))
		return(0);
	if ((word = strtok(NULL, " \t")) == NULL)	/* 0x80 */
		return(0);
	if (!hex_to_int(word, flags))
		return(0);

	return(1);
}

/*
 * Survey storage capacity of this system.
 */
int
survey_storage(struct i_fn_args *a)
{
	unsigned long mem = 0;
	char disks[256], line[256];
	char *disk, *disk_ptr;
	struct commands *cmds;
	struct command *cmd;
	FILE *f;
	char *filename;
	struct disk *d = NULL;
	int number = 0;
	int failure = 0;
	size_t len;
	struct aura_dict *di;
	void *rk;
	size_t rk_len;

	disks_free(a->s);

	len = sizeof(mem);
	if (sysctlbyname("hw.physmem", &mem, &len, NULL, 0) < 0) {
		failure |= 1;
	} else {
		storage_set_memsize(a->s, next_power_of_two(mem >> 20));
	}
	len = 256;
	if (sysctlbyname("kern.disks", disks, &len, NULL, 0) < 0) {
		failure |= 1;
	}
	disk_ptr = disks;

	di = aura_dict_new(1, AURA_DICT_SORTED_LIST);
	while (!failure && (disk = strsep(&disk_ptr, " ")) != NULL) {
		if (disk[0] == '\0')
			continue;

		/*
		 * If the disk is a memory disk, floppy or CD-ROM, skip it.
		 */
		if (strncmp(disk, "md", 2) == 0 ||
		    strncmp(disk, "cd", 2) == 0 ||
		    strncmp(disk, "acd", 3) == 0 ||
		    strncmp(disk, "fd", 2) == 0)
			continue;

		aura_dict_store(di, disk, strlen(disk) + 1, "", 1);
	}

	cmds = commands_new();
	cmd = command_add(cmds, "%s%s -n '' >%ssurvey.txt",
	    a->os_root, cmd_name(a, "ECHO"), a->tmp);
	command_set_log_mode(cmd, COMMAND_LOG_SILENT);

	aura_dict_rewind(di);
	while (!aura_dict_eof(di)) {
		aura_dict_get_current_key(di, &rk, &rk_len),

		disk = (char *)rk;

		cmd = command_add(cmds, "%s%s '@DISK' >>%ssurvey.txt",
		    a->os_root, cmd_name(a, "ECHO"), a->tmp);
		command_set_log_mode(cmd, COMMAND_LOG_SILENT);
		cmd = command_add(cmds, "%s%s '%s' >>%ssurvey.txt",
		    a->os_root, cmd_name(a, "ECHO"), disk, a->tmp);
		command_set_log_mode(cmd, COMMAND_LOG_SILENT);

		/*
		 * Look for descriptions of this disk.
		 */
		cmd = command_add(cmds, "%s%s '@DESC' >>%ssurvey.txt",
		    a->os_root, cmd_name(a, "ECHO"), a->tmp);
		command_set_log_mode(cmd, COMMAND_LOG_SILENT);
		cmd = command_add(cmds, "%s%s -w '^%s: [0-9]*MB' %s%s >>%ssurvey.txt || %s%s '%s' >>%ssurvey.txt",
		    a->os_root, cmd_name(a, "GREP"),
		    disk,
		    a->os_root, cmd_name(a, "DMESG_BOOT"),
		    a->tmp,
		    a->os_root, cmd_name(a, "ECHO"),
		    disk,
		    a->tmp);
		cmd = command_add(cmds, "%s%s '@END' >>%ssurvey.txt",
		    a->os_root, cmd_name(a, "ECHO"), a->tmp);
		command_set_log_mode(cmd, COMMAND_LOG_SILENT);

		/*
		 * Look for the disk's serial number.
		 */
		cmd = command_add(cmds, "%s%s '@SERNO' >>%ssurvey.txt",
		    a->os_root, cmd_name(a, "ECHO"), a->tmp);
		command_set_log_mode(cmd, COMMAND_LOG_SILENT);
		cmd = command_add(cmds, "if %s%s -d /dev/serno; then %s%s -l /dev/serno | %s%s \"`%s%s -l /dev/%s | %s%s '{print $5, $6;}'`\" | %s%s '{print $10;}' >>%ssurvey.txt; fi",
		    a->os_root, cmd_name(a, "TEST"),
		    a->os_root, cmd_name(a, "LS"),
		    a->os_root, cmd_name(a, "GREP"),
		    a->os_root, cmd_name(a, "LS"),
		    disk,
		    a->os_root, cmd_name(a, "AWK"),
		    a->os_root, cmd_name(a, "AWK"),
		    a->tmp);
		cmd = command_add(cmds, "%s%s '@END' >>%ssurvey.txt",
		    a->os_root, cmd_name(a, "ECHO"), a->tmp);
		command_set_log_mode(cmd, COMMAND_LOG_SILENT);

		/*
		 * Probe the disk with fdisk.
		 */
		cmd = command_add(cmds, "%s%s '@SLICES' >>%ssurvey.txt",
		    a->os_root, cmd_name(a, "ECHO"), a->tmp);
		command_set_log_mode(cmd, COMMAND_LOG_SILENT);
		cmd = command_add(cmds, "%s%s -s %s 2>/dev/null >>%ssurvey.txt || %s%s '' >>%ssurvey.txt",
		    a->os_root, cmd_name(a, "FDISK"),
		    disk,
		    a->tmp,
		    a->os_root, cmd_name(a, "ECHO"),
		    a->tmp);
		cmd = command_add(cmds, "%s%s '@END' >>%ssurvey.txt",
		    a->os_root, cmd_name(a, "ECHO"), a->tmp);
		command_set_log_mode(cmd, COMMAND_LOG_SILENT);

		aura_dict_next(di);
	}

	cmd = command_add(cmds, "%s%s '.' >>%ssurvey.txt",
	    a->os_root, cmd_name(a, "ECHO"), a->tmp);
	command_set_log_mode(cmd, COMMAND_LOG_SILENT);

	if (!commands_execute(a, cmds))
		failure |= 1;
	commands_free(cmds);
	temp_file_add(a, "survey.txt");

	aura_dict_free(di);

	/*
	 * Now read in and parse the file that those commands just created.
	 */
	asprintf(&filename, "%ssurvey.txt", a->tmp);
	if ((f = fopen(filename, "r")) == NULL)
		failure |= 1;
	free(filename);

	while (!failure && fgets_chomp(line, 255, f)) {
		if (strcmp(line, "@DISK") == 0) {
			if (fgets_chomp(line, 255, f)) {
				d = disk_new(a->s, line);
				disk_set_number(d, number++);
			}
		} else if (strcmp(line, "@DESC") == 0) {
			while (d != NULL && strcmp(line, "@END") != 0 && fgets_chomp(line, 255, f)) {
				disk_set_desc(d, line);
			}
		} else if (strcmp(line, "@SERNO") == 0) {
			fgets_chomp(line, 255, f);
			if (line[0] != '\0' && strcmp(line, "@END") != 0)
				disk_set_serno(d, line);
		} else if (strcmp(line, "@SLICES") == 0) {
			int cyl, hd, sec;
			int sliceno, type, flags;
			unsigned long start, size;

			/*
			 * /dev/ad3: 2112 cyl 16 hd 63 sec
			 * Part        Start        Size Type Flags
			 *    1:          63     2128833 0xa5 0x80
			 */
			while (d != NULL && strcmp(line, "@END") != 0 && fgets_chomp(line, 255, f)) {
				if (strncmp(line, "/dev/", 5) == 0) {
					cyl = hd = sec = 0;
					parse_geometry_info(line, &cyl, &hd, &sec);
					disk_set_geometry(d, cyl, hd, sec);
				} else if (strncmp(line, "Part", 4) == 0) {
					/* ignore it */
				} else {
					if (parse_slice_info(line, &sliceno, &start, &size,
					    &type, &flags)) {
						/*
						fprintfo(log, "| Found slice #%d, sysid %d, "
						    "start %ld, size %ld\n", sliceno, type, start, size);
						*/
						slice_new(d, sliceno, type, flags, start, size);
					}
				}
			}
		}
	}

	if (f != NULL)
		fclose(f);

	/*
	 * Fix up any disk descriptions that didn't make it.
	 */
	for (d = storage_disk_first(a->s); d != NULL; d = disk_next(d)) {
		if (disk_get_desc(d) == NULL)
			disk_set_desc(d, disk_get_device_name(d));
	}

	return(!failure);
}
