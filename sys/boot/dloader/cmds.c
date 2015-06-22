/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
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

#include <stand.h>
#include <string.h>
#include "bootstrap.h"
#include "dloader.h"

static void menu_display(void);
static int menu_execute(int);

/*
 * This is called from common and must reference files to bring
 * library modules into common during linking.
 */
void
dloader_init_cmds(void)
{
}

/*
 * "local" intercepts assignments: lines of the form 'a=b'
 */
COMMAND_SET(local, "local", "List local variables", command_local);
COMMAND_SET(lunset, "lunset", "Unset local variable", command_lunset);
COMMAND_SET(lunsetif, "lunsetif", "Unset local variable if kenv variable is true", command_lunsetif);
COMMAND_SET(loadall, "loadall", "Load kernel + modules", command_loadall);
COMMAND_SET(menuclear, "menuclear", "Clear all menus", command_menuclear);
COMMAND_SET(menuitem, "menuitem", "Add menu bullet", command_menuitem);
COMMAND_SET(menuadd, "menuadd", "Add script line for bullet", command_menuadd);
COMMAND_SET(menu, "menu", "Run menu system", command_menu);

static int curitem;
static int curadd;

static char *kenv_vars[] = {
	"LINES",
	"acpi_load",
	"autoboot_delay",
	"boot_askname",
	"boot_cdrom",
	"boot_ddb",
	"boot_gdb",
	"boot_serial",
	"boot_single",
	"boot_userconfig",
	"boot_verbose",
	"boot_vidcons",
	"bootfile",
	"console",
	"currdev",
	"default_kernel",
	"dumpdev",
	"ehci_load",
	"interpret",
	"init_chroot",
	"init_path",
	"kernel_options",
	"kernelname",
	"loaddev",
	"module_path",
	"num_ide_disks",
	"prompt",
	"rootdev",
	"root_disk_unit",
	"xhci_load",
	NULL
};

/*
 * List or set local variable.  Sniff assignment of kenv_vars[] and
 * loader tunables (recognized by '.' in name).
 *
 * format for av[0]:
 *  - List: local
 *  - Set:  var=val
 */
static int
command_local(int ac, char **av)
{
	char *name;
	char *data;
	dvar_t dvar;
	int i;
	int j;

	/*
	 * local command executed directly.
	 */
	if (strcmp(av[0], "local") == 0) {
		pager_open();
		for (dvar = dvar_first(); dvar; dvar = dvar_next(dvar)) {
			for (j = 1; j < ac; ++j) {
				if (!strncmp(dvar->name, av[j], strlen(av[j])))
					break;
			}
			if (ac > 1 && j == ac)
				continue;

			pager_output(dvar->name);
			pager_output("=");
			for (i = 0; i < dvar->count; ++i) {
				if (i)
					pager_output(",");
				pager_output("\"");
				pager_output(dvar->data[i]);
				pager_output("\"");
			}
			pager_output("\n");
		}
		pager_close();
		return(CMD_OK);
	}

	/*
	 * local command intercept for 'var=val'
	 */
	name = av[0];
	data = strchr(name, '=');
	if (data == NULL) {
		sprintf(command_errbuf, "Bad variable syntax");
		return (CMD_ERROR);
	}
	*data++ = 0;

	if (*data)
		dvar_set(name, &data, 1);
	else
		dvar_unset(name);

	/*
	 * Take care of loader tunables and several other variables,
	 * all of which have to mirror to kenv because libstand or
	 * other consumers may have hooks into them.
	 */
	if (strchr(name, '.')) {
		setenv(name, data, 1);
	} else {
		for (i = 0; kenv_vars[i] != NULL; i++) {
			if (strcmp(name, kenv_vars[i]) == 0) {
				setenv(name, data, 1);
				return(CMD_OK);
			}
		}
	}
	return(CMD_OK);
}

/*
 * Unset local variables
 */
static int
command_lunset(int ac, char **av)
{
	int i;

	for (i = 1; i < ac; ++i)
		dvar_unset(av[i]);
	return(0);
}

static int
command_lunsetif(int ac, char **av)
{
	char *envdata;

	if (ac != 3) {
		sprintf(command_errbuf,
			"syntax error use lunsetif lname envname");
		return(CMD_ERROR);
	}
	envdata = getenv(av[2]);
	if (strcmp(envdata, "yes") == 0 ||
	    strcmp(envdata, "YES") == 0 ||
	    strtol(envdata, NULL, 0)) {
		dvar_unset(av[1]);
	}
	return (CMD_OK);
}

/*
 * Load the kernel + all modules specified with MODULE_load="YES"
 */
static int
command_loadall(int ac, char **av)
{
	char *argv[4];
	char *mod_name;
	char *mod_fname;
	char *mod_type;
	char *tmp_str;
	dvar_t dvar, dvar2;
	int len;
	int argc;
	int res;
	int tmp;

	argv[0] = "unload";
	(void)perform(1, argv);

	/*
	 * Load kernel
	 */
	argv[0] = "load";
	argv[1] = getenv("kernelname");
	argv[2] = getenv("kernel_options");
	if (argv[1] == NULL)
		argv[1] = strdup("kernel");
	res = perform((argv[2] == NULL)?2:3, argv);
	free(argv[1]);
	if (argv[2])
		free(argv[2]);

	if (res != CMD_OK) {
		printf("Unable to load %s%s\n", DirBase, argv[1]);
		return(res);
	}

	/*
	 * Load modules
	 */
	for (dvar = dvar_first(); dvar; dvar = dvar_next(dvar)) {
		len = strlen(dvar->name);
		if (len <= 5 || strcmp(dvar->name + len - 5, "_load"))
			continue;
		if (strcmp(dvar->data[0], "yes") != 0 &&
		    strcmp(dvar->data[0], "YES") != 0) {
			continue;
		}

		mod_name = strdup(dvar->name);
		mod_name[len - 5] = 0;
		mod_type = NULL;
		mod_fname = NULL;

		/* Check if there's a matching foo_type */
		for (dvar2 = dvar_first();
		     dvar2 && (mod_type == NULL);
		     dvar2 = dvar_next(dvar2)) {
			len = strlen(dvar2->name);
			if (len <= 5 || strcmp(dvar2->name + len - 5, "_type"))
				continue;
			tmp_str = strdup(dvar2->name);
			tmp_str[len - 5] = 0;
			if (strcmp(tmp_str, mod_name) == 0)
				mod_type = dvar2->data[0];

			free(tmp_str);
		}

		/* Check if there's a matching foo_name */
		for (dvar2 = dvar_first();
		     dvar2 && (mod_fname == NULL);
		     dvar2 = dvar_next(dvar2)) {
			len = strlen(dvar2->name);
			if (len <= 5 || strcmp(dvar2->name + len - 5, "_name"))
				continue;
			tmp_str = strdup(dvar2->name);
			tmp_str[len - 5] = 0;
			if (strcmp(tmp_str, mod_name) == 0) {
				mod_fname = dvar2->data[0];
				free(mod_name);
				mod_name = strdup(mod_fname);
			}

			free(tmp_str);
		}

		argv[0] = "load";
		if (mod_type) {
			argc = 4;
			argv[1] = "-t";
			argv[2] = mod_type;
			argv[3] = mod_name;
		} else {
			argc = 2;
			argv[1] = mod_name;
		}
		tmp = perform(argc, argv);
		if (tmp != CMD_OK) {
			time_t t = time(NULL);
			printf("Unable to load %s%s\n", DirBase, mod_name);
			while (time(NULL) == t)
				;
			/* don't kill the boot sequence */
			/* res = tmp; */
		}
		free(mod_name);
	}
	return(res);
}

/*
 * Clear all menus
 */
static int
command_menuclear(int ac, char **av)
{
	dvar_unset("menu_*");
	dvar_unset("item_*");
	curitem = 0;
	curadd = 0;
	return(0);
}

/*
 * Add menu bullet
 */
static int
command_menuitem(int ac, char **av)
{
	char namebuf[32];

	if (ac != 3) {
		sprintf(command_errbuf, "Bad menuitem syntax");
		return (CMD_ERROR);
	}
	curitem = (unsigned char)av[1][0];
	if (curitem == 0) {
		sprintf(command_errbuf, "Bad menuitem syntax");
		return (CMD_ERROR);
	}
	snprintf(namebuf, sizeof(namebuf), "menu_%c", curitem);
	dvar_set(namebuf, &av[2], 1);
	curadd = 0;

	return(CMD_OK);
}

/*
 * Add execution item
 */
static int
command_menuadd(int ac, char **av)
{
	char namebuf[32];

	if (ac == 1)
		return(CMD_OK);
	if (curitem == 0) {
		sprintf(command_errbuf, "Missing menuitem for menuadd");
		return(CMD_ERROR);
	}
	snprintf(namebuf, sizeof(namebuf), "item_%c_%d", curitem, curadd);
	dvar_set(namebuf, &av[1], ac - 1);
	++curadd;
	return (CMD_OK);
}

/*
 * Execute menu system
 */
static int
command_menu(int ac, char **av)
{
	int timeout = -1;
	time_t time_target;
	time_t time_last;
	time_t t;
	char *cp;
	int c;
	int res;
	int counting = 1;

	menu_display();
	if ((cp = getenv("autoboot_delay")) != NULL)
		timeout = strtol(cp, NULL, 0);
	if (timeout <= 0)
		timeout = 10;
	if (timeout > 24 * 60 * 60)
		timeout = 24 * 60 * 60;

	time_target = time(NULL) + timeout;
	time_last = 0;
	c = '1';
	for (;;) {
		if (ischar()) {
			c = getchar();
			if (c == '\r' || c == '\n') {
				c = '1';
				break;
			}
			if (c == ' ') {
				if (counting) {
					printf("\rCountdown halted by "
					       "space   ");
				}
				counting = 0;
				continue;
			}
			if (c == 0x1b) {
				setenv("autoboot_delay", "NO", 1);
				return(CMD_OK);
			}
			res = menu_execute(c);
			if (res >= 0) {
				setenv("autoboot_delay", "NO", 1);
				return(CMD_OK);
			}
			/* else ignore char */
		}
		if (counting) {
			t = time(NULL);
			if (time_last == t)
				continue;
			time_last = t;
			printf("\rBooting in %d second%s... ",
				(int)(time_target - t),
				((time_target - t) == 1 ? "" : "s"));
			if ((int)(time_target - t) <= 0) {
				c = '1';
				break;
			}
		}
	}
	res = menu_execute(c);
	if (res != CMD_OK)
		setenv("autoboot_delay", "NO", 1);
	return (res);
}

#define LOGO_LINES 16
#define FRED_LEFT 0
#define FRED_RIGHT 1
static char *logo_blank_line = "                                 ";

static char *logo_color[LOGO_LINES] = {
	"[31;1m ,--,                       ,--, [0m",
	"[31;1m |   `-,       [33;1m_[31m:[33;1m_[31;1m       ,-'   | [0m",
	"[31;1m  `,    `-,   [33;1m([31m/ \\[33;1m)[31;1m   ,-'    ,'  [0m",
	"[31;1m    `-,    `-,[31m/   \\[31;1m,-'    ,-'    [0m",
	"[31;1m       `------[31m{   }[31;1m------'       [0m",
	"[31;1m   ,----------[31m{   }[31;1m----------,   [0m",
	"[31;1m  |        _,-[31m{   }[31;1m-,_        |  [0m",
	"[31;1m   `-,__,-'   [31m\\   /[31;1m   `-,__,-'   [0m",
	"[31m               | |               [0m",
	"[31m               | |               [0m",
	"[31m               | |               [0m",
	"[31m               | |               [0m",
	"[31m               | |               [0m",
	"[31m               | |               [0m",
	"[31m               `,'               [0m",
	"                                 " };

static char *logo_indigo[LOGO_LINES] = {
	"[36m ,--,                       ,--[36;1m, [0m",
	"[36m |   `-,       [34;1m_[34m:[34;1m_[36m       ,-'   [36;1m| [0m",
	"[36m  `,    `-,   [34;1m([34m/ \\[34;1m)[36m   ,-'    [36;1m,'  [0m",
	"[36m    `-,    `-,[34m/   \\[36m,-'    [36;1m,-'    [0m",
	"[36m       `------[34m{   }[36m------[36;1m'       [0m",
	"[36m   ,----------[34m{   }[36m----------[36;1m,   [0m",
	"[36m  |        [36;1m_,-[34m{   }[36m-,_        [36;1m|  [0m",
	"[36m   `-,__[36;1m,-'   [34m\\   /[36m   `-,__[36;1m,-'   [0m",
	"[34m               | |               [0m",
	"[34m               | |               [0m",
	"[34m               | |               [0m",
	"[34m               | |               [0m",
	"[34m               | |               [0m",
	"[34m               | |               [0m",
	"[34m               `,'               [0m",
	"                                 " };

static char *logo_mono[LOGO_LINES] =  {
	" ,--,                       ,--, ",
	" |   `-,       _:_       ,-'   | ",
	"  `,    `-,   (/ \\)   ,-'    ,'  ",
	"    `-,    `-,/   \\,-'    ,-'    ",
	"       `------{   }------'       ",
	"   ,----------{   }----------,   ",
	"  |        _,-{   }-,_        |  ",
	"   `-,__,-'   \\   /   `-,__,-'   ",
	"               | |               ",
	"               | |               ",
	"               | |               ",
	"               | |               ",
	"               | |               ",
	"               | |               ",
	"               `,'               ",
	"                                 " };

static void
logo_display(char **logo, int line, int orientation, int barrier)
{
	const char *fmt;

	if (orientation == FRED_LEFT)
		fmt = barrier ? "%s  | " : "  %s  ";
	else
		fmt = barrier ? " |  %s" : "  %s  ";

	if (logo != NULL) {
		if (line < LOGO_LINES)
			printf(fmt, logo[line]);
		else
			printf(fmt, logo_blank_line);
	}
}

static void
menu_display(void)
{
	dvar_t dvar;
	int i;
	int logo_left = 0;		/* default to fred on right */
	int separated = 0;		/* default blue fred without line */
	char **logo = logo_indigo;
	char *console_val = getenv("console");

	if (dvar_istrue(dvar_get("fred_is_red")))
		logo = logo_color;

	if (dvar_istrue(dvar_get("loader_plain")))
		logo = logo_mono;

	if (strcmp(console_val, "comconsole") == 0)
		logo = logo_mono;

	if (dvar_istrue(dvar_get("fred_disable")))
		logo = NULL;

	if (dvar_istrue(dvar_get("fred_on_left")))
		logo_left = 1;

	if (dvar_istrue(dvar_get("fred_separated")))
		separated = 1;

	dvar = dvar_first();
	i = 0;

	if (logo != NULL) {
		if (logo_left)
			printf(separated ? "%35s|%43s\n" : "%35s %43s\n",
				" ", " ");
		else
			printf(separated ? "%43s|%35s\n" : "%43s %35s\n",
				" ", " ");
	}

	while (dvar || i < LOGO_LINES) {
		if (logo_left)
			logo_display(logo, i, FRED_LEFT, separated);

		while (dvar) {
			if (strncmp(dvar->name, "menu_", 5) == 0) {
				printf(" %c. %-38.38s",
				    dvar->name[5], dvar->data[0]);
				dvar = dvar_next(dvar);
				break;
			}
			dvar = dvar_next(dvar);
		}
		/*
		 * Pad when the number of menu entries is less than
		 * LOGO_LINES.
		 */
		if (dvar == NULL)
			printf("    %38.38s", " ");

		if (!logo_left)
			logo_display(logo, i, FRED_RIGHT, separated);
		printf("\n");
		i++;
	}
}

static int
menu_execute(int c)
{
	dvar_t dvar;
	dvar_t dvar_exec = NULL;
	dvar_t *dvar_execp = &dvar_exec;
	char namebuf[32];
	int res;

	snprintf(namebuf, sizeof(namebuf), "item_%c_0", c);

	/*
	 * Does this menu option exist?
	 */
	if (dvar_get(namebuf) == NULL)
		return(-1);

	snprintf(namebuf, sizeof(namebuf), "item_%c", c);
	res = CMD_OK;
	printf("\n");

	/*
	 * Copy the items to execute (the act of execution may modify our
	 * local variables so we need to copy).
	 */
	for (dvar = dvar_first(); dvar; dvar = dvar_next(dvar)) {
		if (strncmp(dvar->name, namebuf, 6) == 0) {
			*dvar_execp = dvar_copy(dvar);
			dvar_execp = &(*dvar_execp)->next;
		}
	}

	/*
	 * Execute items
	 */
	for (dvar = dvar_exec; dvar; dvar = dvar->next) {
		res = perform(dvar->count, dvar->data);
		if (res != CMD_OK) {
			printf("%s: %s\n",
				dvar->data[0], command_errmsg);
			setenv("autoboot_delay", "NO", 1);
			break;
		}
	}

	/*
	 * Free items
	 */
	while (dvar_exec)
		dvar_free(&dvar_exec);

	return(res);
}
