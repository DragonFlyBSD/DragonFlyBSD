/**
 ** Copyright (c) 1995
 **      Michael Smith, msmith@freebsd.org.  All rights reserved.
 **
 ** This code contains a module marked :

 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 Jordan K. Hubbard
 * All rights reserved.
 * Copyright (c) 1994 David Greenman
 * All rights reserved.
 *
 * Many additional changes by Bruce Evans
 *
 * This code is derived from software contributed by the
 * University of California Berkeley, Jordan K. Hubbard,
 * David Greenman and Bruce Evans.

 ** As such, it contains code subject to the above copyrights.
 ** The module and its copyright can be found below.
 ** 
 ** Redistribution and use in source and binary forms, with or without
 ** modification, are permitted provided that the following conditions
 ** are met:
 ** 1. Redistributions of source code must retain the above copyright
 **    notice, this list of conditions and the following disclaimer as
 **    the first lines of this file unmodified.
 ** 2. Redistributions in binary form must reproduce the above copyright
 **    notice, this list of conditions and the following disclaimer in the
 **    documentation and/or other materials provided with the distribution.
 ** 3. All advertising materials mentioning features or use of this software
 **    must display the following acknowledgment:
 **      This product includes software developed by Michael Smith.
 ** 4. The name of the author may not be used to endorse or promote products
 **    derived from this software without specific prior written permission.
 **
 ** THIS SOFTWARE IS PROVIDED BY MICHAEL SMITH ``AS IS'' AND ANY EXPRESS OR
 ** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 ** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 ** IN NO EVENT SHALL MICHAEL SMITH BE LIABLE FOR ANY DIRECT, INDIRECT,
 ** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 ** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 ** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 ** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 ** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 ** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **
 ** $FreeBSD: src/sys/i386/i386/userconfig.c,v 1.175.2.10 2002/10/05 18:31:48 scottl Exp $
 **/

/**
 ** USERCONFIG
 **
 ** Kernel boot-time configuration manipulation tool for FreeBSD.
 **
 ** Two modes of operation are supported : the default is the line-editor mode,
 ** the command "visual" invokes the fullscreen mode.
 **
 ** The line-editor mode is the old favorite from FreeBSD 2.0/20.05 &c., the 
 ** fullscreen mode requires syscons or a minimal-ansi serial console.
 **/

/**
 ** USERCONFIG, visual mode.
 **
 **   msmith@freebsd.org
 **
 ** Look for "EDIT THIS LIST" to add to the list of known devices
 ** 
 **
 ** There are a number of assumptions made in this code.
 ** 
 ** - That the console supports a minimal set of ANSI escape sequences
 **   (See the screen manipulation section for a summary)
 **   and has at least 24 rows.
 ** - That values less than or equal to zero for any of the device
 **   parameters indicate that the driver does not use the parameter.
 ** - That flags are _always_ editable.
 **
 ** Devices marked as disabled are imported as such.
 **
 ** For this tool to be useful, the list of devices below _MUST_ be updated 
 ** when a new driver is brought into the kernel.  It is not possible to 
 ** extract this information from the drivers in the kernel.
 **
 ** XXX - TODO:
 ** 
 ** - Display _what_ a device conflicts with.
 ** - Implement page up/down (as what?)
 ** - Wizard mode (no restrictions)
 ** - Find out how to put syscons back into low-intensity mode so that the
 **   !b escape is useful on the console.  (It seems to be that it actually
 **   gets low/high intensity backwards. That looks OK.)
 **
 ** - Only display headings with devices under them. (difficult)
 **/

#include "opt_userconfig.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/reboot.h>
#include <sys/linker.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/cons.h>

#include <machine/md_var.h>
#include <machine/limits.h>

#define _BUS_ISA_ARCH_ISA_DEVICE_H_

#undef NPNP
#define NPNP 0

#if NPNP > 0
#include <machine_base/isa/pnp.h>
#endif

static MALLOC_DEFINE(M_DEVL, "uc_devlist", "uc_device lists in userconfig()");

#include <machine/uc_device.h>
static struct uc_device *uc_devlist;	/* list read by kget to extract changes */
static struct uc_device *uc_devtab;	/* fake uc_device table */

static int userconfig_boot_parsing;	/* set if we are reading from the boot instructions */

static void load_devtab(void);
static void free_devtab(void);
static void save_resource(struct uc_device *);

static int
sysctl_machdep_uc_devlist(SYSCTL_HANDLER_ARGS)
{
	struct uc_device *id;
	int error=0;
	char name[8];

	if(!req->oldptr) {
		/* Only sizing */
		id=uc_devlist;
		while(id) {
			error+=sizeof(struct uc_device)+8;
			id=id->id_next;
		}
		return(SYSCTL_OUT(req,0,error));
	} else {
		/* Output the data. The buffer is filled with consecutive
		 * struct uc_device and char buf[8], containing the name
		 * (not guaranteed to end with '\0').
		 */
		id=uc_devlist;
		while(id) {
			error=sysctl_handle_opaque(oidp,id,
				sizeof(struct uc_device),req);
			if(error) return(error);
			strncpy(name,id->id_name,8);
			error=sysctl_handle_opaque(oidp,name,
				8,req);
			if(error) return(error);
			id=id->id_next;
		}
		return(0);
	}
}

SYSCTL_PROC( _machdep, OID_AUTO, uc_devlist, CTLFLAG_RD,
	0, 0, sysctl_machdep_uc_devlist, "A",
	"List of ISA devices changed in UserConfig");

/*
** Obtain command input.
**
** Initially, input is read from a possibly-loaded script.
** At the end of the script, or if no script is supplied, 
** behaviour is determined by the RB_CONFIG (-c) flag.  If 
** the flag is set, user input is read from the console; if
** unset, the 'quit' command is invoked and userconfig
** will exit.
**
** Note that quit commands encountered in the script will be
** ignored if the RB_CONFIG flag is supplied.
*/
static const char	*config_script;
static int		config_script_size; /* use of int for -ve magic value */

#define has_config_script()	(config_script_size > 0)

static int
init_config_script(void)
{
    caddr_t		autoentry, autoattr;

    /* Look for loaded userconfig script */
    autoentry = preload_search_by_type("userconfig_script");
    if (autoentry != NULL) {
	/* We have one, get size and data */
	config_script_size = 0;
	if ((autoattr = preload_search_info(autoentry, MODINFO_SIZE)) != NULL)
	    config_script_size = (size_t)*(u_int32_t *)autoattr;
	config_script = NULL;
	if ((autoattr = preload_search_info(autoentry, MODINFO_ADDR)) != NULL)
	    config_script = *(const char **)autoattr;
	/* sanity check */
	if ((config_script_size == 0) || (config_script == NULL)) {
	    config_script_size = 0;
	    config_script = NULL;
	}
    }
    return has_config_script();
}

static int
kgetchar(void)
{
    int			c = -1;
#ifdef INTRO_USERCONFIG
    static int		intro = 0;
#endif
    
    if (has_config_script()) 
    {
	/* Consume character from loaded userconfig script, display */
	userconfig_boot_parsing = 1;
	c = *config_script;
	config_script++;
	config_script_size--;

    } else {
	
#ifdef INTRO_USERCONFIG
	if (userconfig_boot_parsing) {
	    if (!(boothowto & RB_CONFIG)) {
		/* userconfig_script, !RB_CONFIG -> quit */
		if (intro == 0) {
		    c = 'q';
		    config_script = "uit\n";
		    config_script_size = strlen(config_script);
		    /* userconfig_script will be 1 on the next pass */
		}
	    } else {
		/* userconfig_script, RB_CONFIG -> cngetc() */
	    }
	} else {
	    if (!(boothowto & RB_CONFIG)) {
		/* no userconfig_script, !RB_CONFIG -> show intro */
		if (intro == 0) {
		    intro = 1;
		    c = 'i';
		    config_script = "ntro\n";
		    config_script_size = strlen(config_script);
		    /* userconfig_script will be 1 on the next pass */
		}
	    } else {
		/* no userconfig_script, RB_CONFIG -> cngetc() */
	    }
	}
#else /* !INTRO_USERCONFIG */
	/* assert(boothowto & RB_CONFIG) */
#endif /* INTRO_USERCONFIG */
	userconfig_boot_parsing = 0;
	if (c <= 0)
	    c = cngetc();
    }
    return(c);
}

#ifndef FALSE
#define FALSE	(0)
#define TRUE	(!FALSE)
#endif

#ifdef VISUAL_USERCONFIG

typedef struct
{
    char	dev[16];		/* device basename */
    char	name[60];		/* long name */
    int		attrib;			/* things to do with the device */
    int		class;			/* device classification */
} DEV_INFO;

#define FLG_INVISIBLE	(1<<0)		/* device should not be shown */
#define FLG_MANDATORY	(1<<1)		/* device can be edited but not disabled */
#define FLG_FIXIRQ	(1<<2)		/* device IRQ cannot be changed */
#define FLG_FIXIOBASE	(1<<3)		/* device iobase cannot be changed */
#define FLG_FIXMADDR	(1<<4)		/* device maddr cannot be changed */
#define FLG_FIXMSIZE	(1<<5)		/* device msize cannot be changed */
#define FLG_FIXDRQ	(1<<6)		/* device DRQ cannot be changed */
#define FLG_FIXED	(FLG_FIXIRQ|FLG_FIXIOBASE|FLG_FIXMADDR|FLG_FIXMSIZE|FLG_FIXDRQ)
#define FLG_IMMUTABLE	(FLG_FIXED|FLG_MANDATORY)

#define CLS_STORAGE	1		/* storage devices */
#define CLS_NETWORK	2		/* network interfaces */
#define CLS_COMMS	3		/* serial, parallel ports */
#define CLS_INPUT	4		/* user input : mice, keyboards, joysticks etc */
#define CLS_MMEDIA	5		/* "multimedia" devices (sound, video, etc) */
#define CLS_MISC	255		/* none of the above */


typedef struct 
{
    char	name[60];
    int		number;
} DEVCLASS_INFO;

static DEVCLASS_INFO devclass_names[] = {
{	"Storage :        ",	CLS_STORAGE},
{	"Communications : ",	CLS_COMMS},
{	"Input :          ",	CLS_INPUT},
{	"Miscellaneous :  ",	CLS_MISC},
{	"",0}};


/********************* EDIT THIS LIST **********************/

/** Notes :
 ** 
 ** - Devices that shouldn't be seen or removed should be marked FLG_INVISIBLE.
 ** - XXX The list below should be reviewed by the driver authors to verify
 **   that the correct flags have been set for each driver, and that the
 **   descriptions are accurate.
 **/

static DEV_INFO device_info[] = {
/*---Name-----   ---Description---------------------------------------------- */
{"ata",		"ATA/ATAPI compatible disk controller",	0,		CLS_STORAGE},
{"fdc",         "Floppy disk controller",		FLG_FIXED,	CLS_STORAGE},
{"ad",		"ATA/ATAPI compatible storage device",	FLG_INVISIBLE,	CLS_STORAGE},	
{"fd",		"Floppy disk device",			FLG_INVISIBLE,	CLS_STORAGE},

{"sio",         "8250/16450/16550 Serial port",		0,		CLS_COMMS},
{"ppc",         "Parallel Port chipset",		0,		CLS_COMMS},

{"atkbdc",      "Keyboard controller",			FLG_INVISIBLE,	CLS_INPUT},
{"atkbd",       "Keyboard",				FLG_FIXED,	CLS_INPUT},
{"psm",         "PS/2 Mouse",				FLG_FIXED,	CLS_INPUT},
{"joy",         "Joystick",				FLG_FIXED,	CLS_INPUT},
{"sc",          "Syscons console driver",		FLG_IMMUTABLE,	CLS_INPUT},

{"pcic",        "PC-card controller",			0,		CLS_MISC},
{"npx",	        "Math coprocessor",			FLG_IMMUTABLE,	CLS_MISC},
{"vga",	        "Catchall PCI VGA driver",		FLG_INVISIBLE,	CLS_MISC},
{"","",0,0}};


typedef struct _devlist_struct
{
    char	name[80];
    int		attrib;			/* flag values as per the FLG_* defines above */
    int		class;			/* disk, etc as per the CLS_* defines above */
    char	dev[16];
    int		iobase,irq,drq,maddr,msize,unit,flags,id;
    int		comment;		/* 0 = device, 1 = comment, 2 = collapsed comment */
    int		conflicts;		/* set/reset by findconflict, count of conflicts */
    int		changed;		/* nonzero if the device has been edited */
    struct uc_device	*device;
    struct _devlist_struct *prev,*next;
} DEV_LIST;


#define DEV_DEVICE	0
#define DEV_COMMENT	1
#define DEV_ZOOMED	2

#define	LIST_CURRENT	(1<<0)
#define LIST_SELECTED	(1<<1)

#define KEY_EXIT	0	/* return codes from dolist() and friends */
#define KEY_DO		1
#define KEY_DEL		2
#define KEY_TAB		3
#define KEY_REDRAW	4

#define KEY_UP		5	/* these only returned from editval() */
#define KEY_DOWN	6
#define KEY_LEFT	7
#define KEY_RIGHT	8
#define KEY_NULL	9	/* this allows us to spin & redraw */

#define KEY_ZOOM	10	/* these for zoom all/collapse all */
#define KEY_UNZOOM	11

#define KEY_HELP	12	/* duh? */

static void redraw(void);
static void insdev(DEV_LIST *dev, DEV_LIST *list);
static int  devinfo(DEV_LIST *dev);
static int  visuserconfig(void);

static DEV_LIST	*active = NULL,*inactive = NULL;	/* driver lists */
static DEV_LIST	*alist,*ilist;				/* visible heads of the driver lists */
static DEV_LIST	scratch;				/* scratch record */
static int	conflicts;				/* total conflict count */


static char lines[] = "--------------------------------------------------------------------------------";
static char spaces[] = "                                                                                     ";


/**
 ** Device manipulation stuff : find, describe, configure.
 **/

/**
 ** setdev
 **
 ** Sets the device referenced by (*dev) to the parameters in the struct,
 ** and the enable flag according to (enabled)
 **/
static void 
setdev(DEV_LIST *dev, int enabled)
{
    dev->device->id_iobase = dev->iobase;				/* copy happy */
    dev->device->id_irq = (u_short)(dev->irq < 16 ? 1<<dev->irq : 0);	/* IRQ is bitfield */
    dev->device->id_drq = (short)dev->drq;
    dev->device->id_maddr = (caddr_t)dev->maddr;
    dev->device->id_msize = dev->msize;
    dev->device->id_flags = dev->flags;
    dev->device->id_enabled = enabled;
}


/**
 ** getdevs
 **
 ** Walk the kernel device tables and build the active and inactive lists
 **/
static void 
getdevs(void)
{
    int 		i;
    struct uc_device	*ap;

	ap = uc_devtab;				/* pointer to array of devices */
	for (i = 0; ap[i].id_id; i++)			/* for each device in this table */
	{
	    scratch.unit = ap[i].id_unit;		/* device parameters */
	    strcpy(scratch.dev,ap[i].id_name);
	    scratch.iobase = ap[i].id_iobase;
	    scratch.irq = ffs(ap[i].id_irq)-1;
	    scratch.drq = ap[i].id_drq;
	    scratch.maddr = (int)ap[i].id_maddr;
	    scratch.msize = ap[i].id_msize;
	    scratch.flags = ap[i].id_flags;

	    scratch.comment = DEV_DEVICE;		/* admin stuff */
	    scratch.conflicts = 0;
	    scratch.device = &ap[i];			/* save pointer for later reference */
	    scratch.changed = 0;
	    if (!devinfo(&scratch))			/* get more info on the device */
		insdev(&scratch,ap[i].id_enabled?active:inactive);
	}
}


/**
 ** Devinfo
 **
 ** Fill in (dev->name), (dev->attrib) and (dev->type) from the device_info array.
 ** If the device is unknown, put it in the CLS_MISC class, with no flags.
 **
 ** If the device is marked "invisible", return nonzero; the caller should
 ** not insert any such device into either list.
 **
 **/
static int
devinfo(DEV_LIST *dev)
{
    int		i;

    for (i = 0; device_info[i].class; i++)
    {
	if (!strcmp(dev->dev,device_info[i].dev))
	{
	    if (device_info[i].attrib & FLG_INVISIBLE)	/* forget we ever saw this one */
		return(1);
	    strcpy(dev->name,device_info[i].name);	/* get the name */
	    dev->attrib = device_info[i].attrib;
	    dev->class = device_info[i].class;
	    return(0);
	}
    }
    strcpy(dev->name,"Unknown device");
    dev->attrib = 0;
    dev->class = CLS_MISC;
    return(0);
}
    

/**
 ** List manipulation stuff : add, move, initialise, free, traverse
 **
 ** Note that there are assumptions throughout this code that
 ** the first entry in a list will never move. (assumed to be
 ** a comment).
 **/


/**
 ** Adddev
 ** 
 ** appends a copy of (dev) to the end of (*list)
 **/
static void 
addev(DEV_LIST *dev, DEV_LIST **list)
{

    DEV_LIST	*lp,*ap;

    lp = (DEV_LIST *)kmalloc(sizeof(DEV_LIST),M_DEVL,M_WAITOK);
    bcopy(dev,lp,sizeof(DEV_LIST));			/* create copied record */

    if (*list)						/* list exists */
    {
	ap = *list;
	while(ap->next)
	    ap = ap->next;				/* scoot to end of list */
	lp->prev = ap;
	lp->next = NULL;
	ap->next = lp;
    }else{						/* list does not yet exist */
	*list = lp;
	lp->prev = lp->next = NULL;			/* list now exists */
    }
}


/**
 ** Findspot
 **
 ** Finds the 'appropriate' place for (dev) in (list)
 **
 ** 'Appropriate' means in numeric order with other devices of the same type,
 ** or in alphabetic order following a comment of the appropriate type.
 ** or at the end of the list if an appropriate comment is not found. (this should
 ** never happen)
 ** (Note that the appropriate point is never the top, but may be the bottom)
 **/
static DEV_LIST	*
findspot(DEV_LIST *dev, DEV_LIST *list)
{
    DEV_LIST	*ap = NULL;

    /* search for a previous instance of the same device */
    for (ap = list; ap; ap = ap->next)
    {
	if (ap->comment != DEV_DEVICE)			/* ignore comments */
	    continue;
	if (!strcmp(dev->dev,ap->dev))			/* same base device */
	{
	    if ((dev->unit <= ap->unit)			/* belongs before (equal is bad) */
		|| !ap->next)				/* or end of list */
	    {
		ap = ap->prev;				/* back up one */
		break;					/* done here */
	    }
	    if (ap->next)					/* if the next item exists */
	    {
		if (ap->next->comment != DEV_DEVICE)	/* next is a comment */
		    break;
		if (strcmp(dev->dev,ap->next->dev))		/* next is a different device */
		    break;
	    }
	}
    }

    if (!ap)						/* not sure yet */
    {
	/* search for a class that the device might belong to */
	for (ap = list; ap; ap = ap->next)
	{
	    if (ap->comment != DEV_DEVICE)		/* look for simlar devices */
		continue;
	    if (dev->class != ap->class)		/* of same class too 8) */
		continue;
	    if (strcmp(dev->dev,ap->dev) < 0)		/* belongs before the current entry */
	    {
		ap = ap->prev;				/* back up one */
		break;					/* done here */
	    }
	    if (ap->next)				/* if the next item exists */
		if (ap->next->comment != DEV_DEVICE)	/* next is a comment, go here */
		    break;
	}
    }

    if (!ap)						/* didn't find a match */
    {
	for (ap = list; ap->next; ap = ap->next)	/* try for a matching comment */
	    if ((ap->comment != DEV_DEVICE) 
		&& (ap->class == dev->class))		/* appropriate place? */
		break;
    }							/* or just put up with last */

    return(ap);
}


/**
 ** Insdev
 **
 ** Inserts a copy of (dev) at the appropriate point in (list)
 **/
static void 
insdev(DEV_LIST *dev, DEV_LIST *list)
{
    DEV_LIST	*lp,*ap;

    lp = (DEV_LIST *)kmalloc(sizeof(DEV_LIST),M_DEVL,M_WAITOK);
    bcopy(dev,lp,sizeof(DEV_LIST));			/* create copied record */

    ap = findspot(lp,list);				/* find appropriate spot */
    lp->next = ap->next;				/* point to next */
    if (ap->next)
	ap->next->prev = lp;				/* point next to new */
    lp->prev = ap;					/* point new to current */
    ap->next = lp;					/* and current to new */
}


/**
 ** Movedev
 **
 ** Moves (dev) from its current list to an appropriate place in (list)
 ** (dev) may not come from the top of a list, but it may from the bottom.
 **/
static void 
movedev(DEV_LIST *dev, DEV_LIST *list)
{
    DEV_LIST	*ap;

    ap = findspot(dev,list);
    dev->prev->next = dev->next;			/* remove from old list */
    if (dev->next)
	dev->next->prev = dev->prev;
    
    dev->next = ap->next;				/* insert in new list */
    if (ap->next)
	ap->next->prev = dev;				/* point next to new */
    dev->prev = ap;					/* point new to current */
    ap->next = dev;					/* and current to new */
}


/**
 ** Initlist
 **
 ** Initialises (*list) with the basic headings
 **/
static void 
initlist(DEV_LIST **list)
{
    int		i;

    for(i = 0; devclass_names[i].name[0]; i++)		/* for each devtype name */
    {
	strcpy(scratch.name,devclass_names[i].name);
	scratch.comment = DEV_ZOOMED;
	scratch.class = devclass_names[i].number;
	scratch.attrib = FLG_MANDATORY;			/* can't be moved */
	addev(&scratch,list);				/* add to the list */
    }
}


/**
 ** savelist
 **
 ** Walks (list) and saves the settings of any entry marked as changed.
 **
 ** The device's active field is set according to (active).
 **
 ** Builds the uc_devlist used by kget to extract the changed device information.
 ** The code for this was taken almost verbatim from the original module.
 **/
static void
savelist(DEV_LIST *list, int active)
{
    struct uc_device	*id_p,*id_pn;
    char *name;

    while (list)
    {
	if ((list->comment == DEV_DEVICE) &&		/* is a device */
	    (list->changed) &&				/* has been changed */
	    (list->device != NULL)) {			/* has an uc_device structure */

	    setdev(list,active);			/* set the device itself */

	    id_pn = NULL;
	    for (id_p=uc_devlist; id_p; id_p=id_p->id_next) 
	    {						/* look on the list for it */
		if (id_p->id_id == list->device->id_id) 
		{
		    name = list->device->id_name;
		    id_pn = id_p->id_next;
		    if (id_p->id_name)
			    kfree(id_p->id_name, M_DEVL);
		    bcopy(list->device,id_p,sizeof(struct uc_device));
		    save_resource(list->device);
		    id_p->id_name = kmalloc(strlen(name) + 1, M_DEVL,M_WAITOK);
		    strcpy(id_p->id_name, name);
		    id_pn->id_next = uc_devlist;
		    id_p->id_next = id_pn;
		    break;
		}
	    }
	    if (!id_pn)					/* not already on the list */
	    {
		name = list->device->id_name;
		id_pn = kmalloc(sizeof(struct uc_device),M_DEVL,M_WAITOK);
		bcopy(list->device,id_pn,sizeof(struct uc_device));
		save_resource(list->device);
		id_pn->id_name = kmalloc(strlen(name) + 1, M_DEVL,M_WAITOK);
		strcpy(id_pn->id_name, name);
		id_pn->id_next = uc_devlist;
		uc_devlist = id_pn;			/* park at top of list */
	    }
	}
	list = list->next;
    }
}


/**
 ** nukelist
 **
 ** Frees all storage in use by a (list).
 **/
static void 
nukelist(DEV_LIST *list)
{
    DEV_LIST	*dp;

    if (!list)
	return;
    while(list->prev)					/* walk to head of list */
	list = list->prev;

    while(list)
    {
	dp = list;
	list = list->next;
	kfree(dp,M_DEVL);
    }
}


/**
 ** prevent
 **
 ** Returns the previous entry in (list), skipping zoomed regions.  Returns NULL
 ** if there is no previous entry. (Only possible if list->prev == NULL given the
 ** premise that there is always a comment at the head of the list)
 **/
static DEV_LIST *
prevent(DEV_LIST *list)
{
    DEV_LIST	*dp;

    if (!list)
	return(NULL);
    dp = list->prev;			/* start back one */
    while(dp)
    {
	if (dp->comment == DEV_ZOOMED)	/* previous section is zoomed */
	    return(dp);			/* so skip to comment */
	if (dp->comment == DEV_COMMENT)	/* not zoomed */
	    return(list->prev);		/* one back as normal */
	dp = dp->prev;			/* backpedal */
    }
    return(dp);				/* NULL, we can assume */
}


/**
 ** nextent
 **
 ** Returns the next entry in (list), skipping zoomed regions.  Returns NULL
 ** if there is no next entry. (Possible if the current entry is last, or
 ** if the current entry is the last heading and it's collapsed)
 **/
static DEV_LIST	*
nextent(DEV_LIST *list)
{
    DEV_LIST	*dp;

    if (!list)
	return(NULL);
    if (list->comment != DEV_ZOOMED)	/* no reason to skip */
	return(list->next);
    dp = list->next;
    while(dp)
    {
	if (dp->comment != DEV_DEVICE)	/* found another heading */
	    break;
	dp = dp->next;
    }
    return(dp);				/* back we go */
}
    

/**
 ** ofsent
 **
 ** Returns the (ofs)th entry down from (list), or NULL if it doesn't exist 
 **/
static DEV_LIST *
ofsent(int ofs, DEV_LIST *list)
{
    while (ofs-- && list)
	list = nextent(list);
    return(list);
}


/**
 ** findconflict
 **
 ** Scans every element of (list) and sets the conflict tags appropriately
 ** Returns the number of conflicts found.
 **/
static int
findconflict(DEV_LIST *list)
{
    int		count = 0;			/* number of conflicts found */
    DEV_LIST	*dp,*sp;

    for (dp = list; dp; dp = dp->next)		/* over the whole list */
    {
	if (dp->comment != DEV_DEVICE)		/* comments don't usually conflict */
	    continue;

	dp->conflicts = 0;			/* assume the best */
	for (sp = list; sp; sp = sp->next)	/* scan the entire list for conflicts */
	{
	    if (sp->comment != DEV_DEVICE)	/* likewise */
		continue;

	    if (sp == dp)			/* always conflict with itself */
		continue;

	    if ((dp->iobase > 0) &&		/* iobase conflict? */
		(dp->iobase == sp->iobase))
		dp->conflicts = 1;
	    if ((dp->irq > 0) &&		/* irq conflict? */
		(dp->irq == sp->irq))
		dp->conflicts = 1;
	    if ((dp->drq > 0) &&		/* drq conflict? */
		(dp->drq == sp->drq))
		dp->conflicts = 1;
	    if ((sp->maddr > 0) &&		/* maddr/msize conflict? */
		(dp->maddr > 0) &&
		(sp->maddr + ((sp->msize == 0) ? 1 : sp->msize) > dp->maddr) &&
		(dp->maddr + ((dp->msize == 0) ? 1 : dp->msize) > sp->maddr))
		dp->conflicts = 1;
	}
	count += dp->conflicts;			/* count conflicts */
    }
    return(count);
}


/**
 ** expandlist
 **
 ** Unzooms all headings in (list)
 **/
static void
expandlist(DEV_LIST *list)
{
    while(list)
    {
	if (list->comment == DEV_COMMENT)
	    list->comment = DEV_ZOOMED;
	list = list->next;
    }
}


/**
 ** collapselist
 **
 ** Zooms all headings in (list)
 **/
static void
collapselist(DEV_LIST *list)
{
    while(list)
    {
	if (list->comment == DEV_ZOOMED)
	    list->comment = DEV_COMMENT;
	list = list->next;
    }
}


/**
 ** Screen-manipulation stuff
 **
 ** This is the basic screen layout :
 **
 **     0    5   10   15   20   25   30   35   40   45   50   55   60   67   70   75
 **     |....|....|....|....|....|....|....|....|....|....|....|....|....|....|....|....
 **    +--------------------------------------------------------------------------------+
 ** 0 -|---Active Drivers----------------------------xx Conflicts------Dev---IRQ--Port--|
 ** 1 -| ........................                                    .......  ..  0x....|
 ** 2 -| ........................                                    .......  ..  0x....|
 ** 3 -| ........................                                    .......  ..  0x....|
 ** 4 -| ........................                                    .......  ..  0x....|
 ** 5 -| ........................                                    .......  ..  0x....|
 ** 6 -| ........................                                    .......  ..  0x....|
 ** 7 -| ........................                                    .......  ..  0x....|
 ** 8 -| ........................                                    .......  ..  0x....|
 ** 9 -|---Inactive Drivers--------------------------------------------Dev--------------|
 ** 10-| ........................                                    .......            |
 ** 11-| ........................                                    .......            |
 ** 12-| ........................                                    .......            |
 ** 13-| ........................                                    .......            |
 ** 14-| ........................                                    .......            |
 ** 15-| ........................                                    .......            |
 ** 16-| ........................                                    .......            |
 ** 17-|------------------------------------------------------UP-DOWN-------------------|
 ** 18-| Relevant parameters for the current device                                     |
 ** 19-|                                                                                |
 ** 20-|                                                                                |
 ** 21-|--------------------------------------------------------------------------------|
 ** 22-| Help texts go here                                                             |
 ** 23-|                                                                                |
 **    +--------------------------------------------------------------------------------+
 **
 ** Help texts
 **
 ** On a collapsed comment :
 **
 ** [Enter] Expand device list      [z]   Expand all lists
 ** [TAB]   Change fields           [Q]   Save and Exit
 **
 ** On an expanded comment :
 ** 
 ** [Enter] Collapse device list    [Z]   Collapse all lists
 ** [TAB]   Change fields           [Q]   Save and Exit
 **
 ** On a comment with no followers
 **
 ** 
 ** [TAB]   Change fields           [Q]   Save and Exit
 **
 ** On a device in the active list
 **
 ** [Enter] Edit device parameters  [DEL] Disable device
 ** [TAB]   Change fields           [Q]   Save and Exit             [?] Help
 **
 ** On a device in the inactive list
 **
 ** [Enter] Enable device
 ** [TAB]   Change fields           [Q]   Save and Exit             [?] Help
 **
 ** While editing parameters
 **
 ** <parameter-specific help here>
 ** [TAB]   Change fields           [Q]   Save device parameters
 **/



/**
 **
 ** The base-level screen primitives :
 **
 ** bold()	- enter bold mode 		\E[1m     (md)
 ** inverse()   - enter inverse mode 		\E[7m     (so)
 ** normal()	- clear bold/inverse mode 	\E[m      (se)
 ** clear()	- clear the screen 		\E[H\E[J  (ce)
 ** move(x,y)	- move the cursor to x,y 	\E[y;xH:  (cm)
 **/

static void 
bold(void)
{
    kprintf("\033[1m");
}

static void 
inverse(void)
{
    kprintf("\033[7m");
}

static void 
normal(void)
{
    kprintf("\033[m");
}

static void 
clear(void)
{
    normal();
    kprintf("\033[H\033[J");
}

static void 
move(int x, int y)
{
    kprintf("\033[%d;%dH",y+1,x+1);
}


/**
 **
 ** High-level screen primitives :
 ** 
 ** putxyl(x,y,str,len)	- put (len) bytes of (str) at (x,y), supports embedded formatting
 ** putxy(x,y,str)	- put (str) at (x,y), supports embedded formatting
 ** erase(x,y,w,h)	- clear the box (x,y,w,h)
 ** txtbox(x,y,w,y,str) - put (str) in a region at (x,y,w,h)
 ** putmsg(str)		- put (str) in the message area
 ** puthelp(str)	- put (str) in the upper helpline
 ** pad(str,len)	- pad (str) to (len) with spaces
 ** drawline(row,detail,list,inverse,*dhelp)
 **			- draws a line for (*list) at (row) onscreen.  If (detail) is 
 **			  nonzero, include port, IRQ and maddr, if (inverse) is nonzero,
 **			  draw the line in inverse video, and display (*dhelp) on the
 **                       helpline.
 ** drawlist(row,num,detail,list)
 **			- draw (num) entries from (list) at (row) onscreen, passile (detail)
 **			  through to drawline().
 ** showparams(dev)	- displays the relevant parameters for (dev) below the lists onscreen.
 ** yesno(str)		- displays (str) in the message area, and returns nonzero on 'y' or 'Y'
 ** redraw();		- Redraws the entire screen layout, including the 
 **			- two list panels.
 **/

/** 
 ** putxy
 **   writes (str) at x,y onscreen
 ** putxyl
 **   writes up to (len) of (str) at x,y onscreen.
 **
 ** Supports embedded formatting :
 ** !i - inverse mode.
 ** !b - bold mode.
 ** !n - normal mode.
 **/
static void 
putxyl(int x, int y, char *str, int len)
{
    move(x,y);
    normal();

    while((*str) && (len--))
    {
	if (*str == '!')		/* format escape? */
	{
	    switch(*(str+1))		/* depending on the next character */
	    {
	    case 'i':
		inverse();
		str +=2;		/* skip formatting */
		len++;			/* doesn't count for length */
		break;
		
	    case 'b':
		bold();
		str  +=2;		/* skip formatting */
		len++;			/* doesn't count for length */
		break;

	    case 'n':
		normal();
		str +=2;		/* skip formatting */
		len++;			/* doesn't count for length */
		break;
		
	    default:
		kprintf("%c", *str++);	/* not an escape */
	    }
	}else{
	    kprintf("%c", *str++);		/* emit the character */
	}
    }
}

#define putxy(x,y,str)	putxyl(x,y,str,-1)


/**
 ** erase
 **
 ** Erases the region (x,y,w,h)
 **/
static void 
erase(int x, int y, int w, int h)
{
    int		i;

    normal();
    for (i = 0; i < h; i++)
	putxyl(x,y++,spaces,w);
}


/** 
 ** txtbox
 **
 ** Writes (str) into the region (x,y,w,h), supports embedded formatting using
 ** putxy.  Lines are not wrapped, newlines must be forced with \n.
 **/
static void 
txtbox(int x, int y, int w, int h, char *str)
{
    int		i = 0;

    h--;
    while((str[i]) && h)
    {
	if (str[i] == '\n')			/* newline */
	{
	    putxyl(x,y,str,(i<w)?i:w);		/* write lesser of i or w */
	    y++;				/* move down */
	    h--;				/* room for one less */
	    str += (i+1);			/* skip first newline */
	    i = 0;				/* zero offset */
	}else{
	    i++;				/* next character */
	}
    }
    if (h)					/* end of string, not region */
	putxyl(x,y,str,w);
}


/**
 ** putmsg
 **
 ** writes (msg) in the helptext area
 **/
static void 
putmsg(char *msg)
{
    erase(0,18,80,3);				/* clear area */
    txtbox(0,18,80,3,msg);
}


/**
 ** puthelp
 **
 ** Writes (msg) in the helpline area
 **/
static void 
puthelp(char *msg)
{
    erase(0,22,80,1);
    putxy(0,22,msg);
}


/**
 ** masterhelp
 **
 ** Draws the help message at the bottom of the screen
 **/
static void
masterhelp(char *msg)
{
    erase(0,23,80,1);
    putxy(0,23,msg);
}


/**
 ** pad 
 **
 ** space-pads a (str) to (len) characters
 **/
static void 
pad(char *str, int len)
{
    int		i;

    for (i = 0; str[i]; i++)			/* find the end of the string */
	;
    if (i >= len)				/* no padding needed */
	return;
    while(i < len)				/* pad */
	str[i++] = ' ';
    str[i] = '\0';
}
						   

/**
 ** drawline
 **
 ** Displays entry (ofs) of (list) in region at (row) onscreen, optionally displaying
 ** the port and IRQ fields if (detail) is nonzero.  If (inverse), in inverse video.
 **
 ** The text (dhelp) is displayed if the item is a normal device, otherwise
 ** help is shown for normal or zoomed comments
 **/
static void 
drawline(int row, int detail, DEV_LIST *list, int inverse, char *dhelp)
{
    char	lbuf[90],nb[70],db[20],ib[16],pb[16];
    
    if (list->comment == DEV_DEVICE)
    {
	nb[0] = ' ';
	strncpy(nb+1,list->name,57);
    }else{
	strncpy(nb,list->name,58);
	if ((list->comment == DEV_ZOOMED) && (list->next))
	    if (list->next->comment == DEV_DEVICE)	/* only mention if there's something hidden */
		strcat(nb,"  (Collapsed)");
    }
    nb[58] = '\0';
    pad(nb,60);
    if (list->conflicts)			/* device in conflict? */
    {
	if (inverse)
	{
	    strcpy(nb+54," !nCONF!i ");		/* tag conflict, careful of length */
	}else{
	    strcpy(nb+54," !iCONF!n ");		/* tag conflict, careful of length */
	}
    }
    if (list->comment == DEV_DEVICE)
    {
	ksprintf(db,"%s%d",list->dev,list->unit);
	pad(db,8);
    }else{
	strcpy(db,"        ");
    }
    if ((list->irq > 0) && detail && (list->comment == DEV_DEVICE))
    {
	ksprintf(ib," %d",list->irq);
	pad(ib,4);
    }else{
	strcpy(ib,"    ");
    }
    if ((list->iobase > 0) && detail && (list->comment == DEV_DEVICE))
    {
	ksprintf(pb,"0x%x",list->iobase);
	pad(pb,7);
    }else{
	strcpy(pb,"       ");
    }

    ksprintf(lbuf,"  %s%s%s%s%s",inverse?"!i":"",nb,db,ib,pb);

    putxyl(0,row,lbuf,80);
    if (dhelp)
    {
	switch(list->comment)
	{
	case DEV_DEVICE:	/* ordinary device */
	    puthelp(dhelp);
	    break;
	case DEV_COMMENT:
	    puthelp("");
	    if (list->next)
		if (list->next->comment == DEV_DEVICE)
		    puthelp("  [!bEnter!n] Collapse device list    [!bC!n]    Collapse all lists");
	    break;
	case DEV_ZOOMED:	
	    puthelp("");
	    if (list->next)
		if (list->next->comment == DEV_DEVICE)
		    puthelp("  [!bEnter!n] Expand device list      [!bX!n]    Expand all lists");
	    break;
	default:
	    puthelp("  WARNING: This list entry corrupted!");
	    break;
	}
    }
    move(0,row);				/* put the cursor somewhere relevant */
}


/**
 ** drawlist
 **
 ** Displays (num) lines of the contents of (list) at (row), optionally
 ** displaying the port and IRQ fields as well if (detail) is nonzero.
 **/
static void 
drawlist(int row, int num, int detail, DEV_LIST *list)
{
    int		ofs;

    for(ofs = 0; ofs < num; ofs++)
    {
	if (list)
	{
	    drawline(row+ofs,detail,list,0,NULL);	/* NULL -> don't draw empty help string */
	    list = nextent(list);			/* move down visible list */
	}else{
	    erase(0,row+ofs,80,1);
	}
    }
}


/**
 ** redrawactive
 **
 ** Redraws the active list 
 **/
static void
redrawactive(void)
{
    char	cbuf[16];

    if (conflicts)
    {
	ksprintf(cbuf,"!i%d conflict%s-",conflicts,(conflicts>1)?"s":"");
	putxy(45,0,cbuf);
    }else{
	putxyl(45,0,lines,16);
    }
    drawlist(1,8,1,alist);			/* draw device lists */
}

/**
 ** redrawinactive
 **
 ** Redraws the inactive list 
 **/
static void
redrawinactive(void)
{
    drawlist(10,7,0,ilist);			/* draw device lists */
}


/**
 ** redraw
 **
 ** Clear the screen and redraw the entire layout
 **/
static void 
redraw(void)
{
    clear();
    putxy(0,0,lines);
    putxy(3,0,"!bActive!n-!bDrivers");
    putxy(63,0,"!bDev!n---!bIRQ!n--!bPort");
    putxy(0,9,lines);
    putxy(3,9,"!bInactive!n-!bDrivers");
    putxy(63,9,"!bDev");
    putxy(0,17,lines);
    putxy(0,21,lines);
    masterhelp("  [!bTAB!n]   Change fields           [!bQ!n]   Save and Exit             [!b?!n] Help");

    redrawactive();
    redrawinactive();
}


/**
 ** yesnocancel
 **
 ** Put (str) in the message area, and return 1 if the user hits 'y' or 'Y',
 ** 2 if they hit 'c' or 'C',  or 0 for 'n' or 'N'.
 **/
static int
yesnocancel(char *str)
{

    putmsg(str);
    for(;;)
	switch(kgetchar())
	{
	case -1:
	case 'n':
	case 'N':
	    return(0);
	    
	case 'y':
	case 'Y':
	    return(1);
	    
	case 'c':
	case 'C':
	    return(2);
	}
}


/**
 ** showparams
 **
 ** Show device parameters in the region below the lists
 **
 **     0    5   10   15   20   25   30   35   40   45   50   55   60   67   70   75
 **     |....|....|....|....|....|....|....|....|....|....|....|....|....|....|....|....
 **    +--------------------------------------------------------------------------------+
 ** 17-|--------------------------------------------------------------------------------|
 ** 18-| Port address : 0x0000     Memory address : 0x00000   Conflict allowed          |
 ** 19-| IRQ number   : 00         Memory size    : 0x0000                              |
 ** 20-| Flags        : 0x0000     DRQ number     : 00                                  |
 ** 21-|--------------------------------------------------------------------------------|
 **/
static void 
showparams(DEV_LIST *dev)
{
    char	buf[80];

    erase(0,18,80,3);				/* clear area */
    if (!dev)
	return;
    if (dev->comment != DEV_DEVICE)
	return;


    if (dev->iobase > 0)
    {
	ksprintf(buf,"Port address : 0x%x",dev->iobase);
	putxy(1,18,buf);
    }
	    
    if (dev->irq > 0)
    {
	ksprintf(buf,"IRQ number   : %d",dev->irq);
	putxy(1,19,buf);
    }
    ksprintf(buf,"Flags        : 0x%x",dev->flags);
    putxy(1,20,buf);
    if (dev->maddr > 0)
    {
	ksprintf(buf,"Memory address : 0x%x",dev->maddr);
	putxy(26,18,buf);
    }
    if (dev->msize > 0)
    {
	ksprintf(buf,"Memory size    : 0x%x",dev->msize);
	putxy(26,19,buf);
    }

    if (dev->drq > 0)
    {
	ksprintf(buf,"DRQ number     : %d",dev->drq);
	putxy(26,20,buf);
    }
}


/**
 ** Editing functions for device parameters
 **
 ** editval(x,y,width,hex,min,max,val)	- Edit (*val) in a field (width) wide at (x,y)
 **					  onscreen.  Refuse values outsise (min) and (max).
 ** editparams(dev)			- Edit the parameters for (dev)
 **/


#define VetRet(code)							\
{									\
    if ((i >= min) && (i <= max))	/* legit? */			\
    {									\
	*val = i;							\
	ksprintf(buf,hex?"0x%x":"%d",i);				\
	putxy(hex?x-2:x,y,buf);						\
	return(code);			/* all done and exit */		\
    }									\
    i = *val;				/* restore original value */	\
    delta = 1;				/* restore other stuff */	\
}


/**
 ** editval
 **
 ** Edit (*val) at (x,y) in (hex)?hex:decimal mode, allowing values between (min) and (max)
 ** in a field (width) wide. (Allow one space)
 ** If (ro) is set, we're in "readonly" mode, so disallow edits.
 **
 ** Return KEY_TAB on \t, KEY_EXIT on 'q'
 **/
static int 
editval(int x, int y, int width, int hex, int min, int max, int *val, int ro)
{
    int		i = *val;			/* work with copy of the value */
    char	buf[2+11+1],tc[11+1];		/* display buffer, text copy */
    int		xp = 0;				/* cursor offset into text copy */
    int		delta = 1;			/* force redraw first time in */
    int		c;
    int		extended = 0;			/* stage counter for extended key sequences */

    if (hex)					/* we presume there's a leading 0x onscreen */
	putxy(x-2,y,"!i0x");			/* coz there sure is now */
    	
    for (;;)
    {
	if (delta)				/* only update if necessary */
	{
	    ksprintf(tc,hex?"%x":"%d",i);	/* make a text copy of the value */
	    ksprintf(buf,"!i%s",tc);		/* format for printing */
	    erase(x,y,width,1);			/* clear the area */
	    putxy(x,y,buf);			/* write */
	    xp = strlen(tc);			/* cursor always at end */
	    move(x+xp,y);			/* position the cursor */
	}

	c = kgetchar();

	switch(extended)			/* escape handling */
	{
	case 0:
	    if (c == 0x1b)			/* esc? */
	    {
		extended = 1;			/* flag and spin */
		continue;
	    }
	    extended = 0;
	    break;				/* nope, drop through */
	
	case 1:					/* there was an escape prefix */
	    if (c == '[' || c == 'O')		/* second character in sequence */
	    {
		extended = 2;
		continue;
	    }
	    if (c == 0x1b)
		return(KEY_EXIT);		/* double esc exits */
	    extended = 0;
	    break;				/* nup, not a sequence. */

	case 2:
	    extended = 0;
	    switch(c)				/* looks like the real McCoy */
	    {
	    case 'A':
		VetRet(KEY_UP);			/* leave if OK */
		continue;
	    case 'B':
		VetRet(KEY_DOWN);		/* leave if OK */
		continue;
	    case 'C':
		VetRet(KEY_RIGHT);		/* leave if OK */
		continue;
	    case 'D':
		VetRet(KEY_LEFT);		/* leave if OK */
		continue;
		
	    default:
		continue;
	    }
	}
    
	switch(c)
	{
	case '\t':				/* trying to tab off */
	    VetRet(KEY_TAB);			/* verify and maybe return */
	    break;

	case -1:
	case 'q':
	case 'Q':
	    VetRet(KEY_EXIT);
	    break;
	    
	case '\b':
	case '\177':				/* BS or DEL */
	    if (ro)				/* readonly? */
	    {
		puthelp(" !iThis value cannot be edited (Press ESC)");
		while(kgetchar() != 0x1b);	/* wait for key */
		return(KEY_NULL);		/* spin */
	    }
	    if (xp)				/* still something left to delete */
	    {
		i = (hex ? i/0x10u : i/10);	/* strip last digit */
		delta = 1;			/* force update */
	    }
	    break;

	case 588:
	    VetRet(KEY_UP);
	    break;

	case '\r':
	case '\n':
	case 596:
	    VetRet(KEY_DOWN);
	    break;

	case 591:
	    VetRet(KEY_LEFT);
	    break;

	case 593:
	    VetRet(KEY_RIGHT);
	    break;
		
	default:
	    if (ro)				/* readonly? */
	    {
		puthelp(" !iThis value cannot be edited (Press ESC)");
		while(kgetchar() != 0x1b);	/* wait for key */
		return(KEY_NULL);		/* spin */
	    }
	    if (xp >= width)			/* no room for more characters anyway */
		break;
	    if (hex)
	    {
		if ((c >= '0') && (c <= '9'))
		{
		    i = i*0x10 + (c-'0');	/* update value */
		    delta = 1;
		    break;
		}
		if ((c >= 'a') && (c <= 'f'))
		{
		    i = i*0x10 + (c-'a'+0xa);
		    delta = 1;
		    break;
		}
		if ((c >= 'A') && (c <= 'F'))
		{
		    i = i*0x10 + (c-'A'+0xa);
		    delta = 1;
		    break;
		}
	    }else{
		if ((c >= '0') && (c <= '9'))
		{
		    i = i*10 + (c-'0');		/* update value */
		    delta = 1;			/* force redraw */
		    break;
		}
	    }
	    break;
	}
    }
}


/**
 ** editparams
 **
 ** Edit the parameters for (dev)
 **
 ** Note that it's _always_ possible to edit the flags, otherwise it might be
 ** possible for this to spin in an endless loop...
 **     0    5   10   15   20   25   30   35   40   45   50   55   60   67   70   75
 **     |....|....|....|....|....|....|....|....|....|....|....|....|....|....|....|....
 **    +--------------------------------------------------------------------------------+
 ** 17-|--------------------------------------------------------------------------------|
 ** 18-| Port address : 0x0000     Memory address : 0x00000   Conflict allowed          |
 ** 19-| IRQ number   : 00         Memory size    : 0x0000                              |
 ** 20-| Flags        : 0x0000     DRQ number     : 00                                  |
 ** 21-|--------------------------------------------------------------------------------|
 **
 ** The "intelligence" in this function that hops around based on the directional
 ** returns from editval isn't very smart, and depends on the layout above.
 **/
static void 
editparams(DEV_LIST *dev)
{
    int		ret;
    char	buf[16];		/* needs to fit the device name */

    putxy(2,17,"!bParameters!n-!bfor!n-!bdevice!n-");
    ksprintf(buf,"!b%s",dev->dev);
    putxy(24,17,buf);

    erase(1,22,80,1);
    for (;;)
    {
    ep_iobase:
	if (dev->iobase > 0)
	{
	    puthelp("  IO Port address (Hexadecimal, 0x1-0xffff)");
	    ret = editval(18,18,5,1,0x1,0xffff,&(dev->iobase),(dev->attrib & FLG_FIXIOBASE));
	    switch(ret)
	    {
	    case KEY_EXIT:
		goto ep_exit;

	    case KEY_RIGHT:
		if (dev->maddr > 0)
		    goto ep_maddr;
		break;

	    case KEY_TAB:
	    case KEY_DOWN:
		goto ep_irq;
	    }
	    goto ep_iobase;
	}
    ep_irq:
	if (dev->irq > 0)
	{
	    puthelp("  Interrupt number (Decimal, 1-15)");
	    ret = editval(16,19,3,0,1,15,&(dev->irq),(dev->attrib & FLG_FIXIRQ));
	    switch(ret)
	    {
	    case KEY_EXIT:
		goto ep_exit;

	    case KEY_RIGHT:
		if (dev->msize > 0)
		    goto ep_msize;
		break;

	    case KEY_UP:
		if (dev->iobase > 0)
		    goto ep_iobase;
		break;

	    case KEY_TAB:
	    case KEY_DOWN:
		goto ep_flags;
	    }
	    goto ep_irq;
	}
    ep_flags:
	puthelp("  Device-specific flag values.");
	ret = editval(18,20,8,1,INT_MIN,INT_MAX,&(dev->flags),0);
	switch(ret)
	{
	case KEY_EXIT:
	    goto ep_exit;

	case KEY_RIGHT:
	    if (dev->drq > 0) 
		goto ep_drq;
	    break;

	case KEY_UP:
	    if (dev->irq > 0)
		goto ep_irq;
	    if (dev->iobase > 0)
		goto ep_iobase;
	    break;

	case KEY_DOWN:
	    if (dev->maddr > 0)
		goto ep_maddr;
	    if (dev->msize > 0)
		goto ep_msize;
	    if (dev->drq > 0)
		goto ep_drq;
	    break;

	case KEY_TAB:
	    goto ep_maddr;
	}
	goto ep_flags;
    ep_maddr:
	if (dev->maddr > 0)
	{
	    puthelp("  Device memory start address (Hexadecimal, 0x1-0xfffff)");
	    ret = editval(45,18,6,1,0x1,0xfffff,&(dev->maddr),(dev->attrib & FLG_FIXMADDR));
	    switch(ret)
	    {
	    case KEY_EXIT:
		goto ep_exit;

	    case KEY_LEFT:
		if (dev->iobase > 0)
		    goto ep_iobase;
		break;

	    case KEY_UP:
		goto ep_flags;

	    case KEY_DOWN:
		if (dev->msize > 0)
		    goto ep_msize;
		if (dev->drq > 0)
		    goto ep_drq;
		break;

	    case KEY_TAB:
		goto ep_msize;
	    }
	    goto ep_maddr;
	}
    ep_msize:
	if (dev->msize > 0)
	{
	    puthelp("  Device memory size (Hexadecimal, 0x1-0x10000)");
	    ret = editval(45,19,5,1,0x1,0x10000,&(dev->msize),(dev->attrib & FLG_FIXMSIZE));
	    switch(ret)
	    {
	    case KEY_EXIT:
		goto ep_exit;

	    case KEY_LEFT:
		if (dev->irq > 0)
		    goto ep_irq;
		break;

	    case KEY_UP:
		if (dev->maddr > 0)
		    goto ep_maddr;
		goto ep_flags;

	    case KEY_DOWN:
		if (dev->drq > 0)
		    goto ep_drq;
		break;

	    case KEY_TAB:
		goto ep_drq;
	    }
	    goto ep_msize;
	}
    ep_drq:
	if (dev->drq > 0)
	{
	    puthelp("  Device DMA request number (Decimal, 1-7)");
	    ret = editval(43,20,2,0,1,7,&(dev->drq),(dev->attrib & FLG_FIXDRQ));
	    switch(ret)
	    {
	    case KEY_EXIT:
		goto ep_exit;

	    case KEY_LEFT:
		goto ep_flags;

	    case KEY_UP:
		if (dev->msize > 0)
		    goto ep_msize;
		if (dev->maddr > 0)
		    goto ep_maddr;
		goto ep_flags;

	    case KEY_TAB:
		goto ep_iobase;
	    }
	    goto ep_drq;
	}
    }
    ep_exit:
    dev->changed = 1;					/* mark as changed */
}

static char *helptext[] =
{
    "                Using the UserConfig kernel settings editor",
    "                -------------------------------------------",
    "",
    "VISUAL MODE:",
    "",
    "- - Layout -",
    "",
    "The screen displays a list of available drivers, divided into two",
    "scrolling lists: Active Drivers, and Inactive Drivers.  Each list is",
    "by default collapsed and can be expanded to show all the drivers",
    "available in each category.  The parameters for the currently selected",
    "driver are shown at the bottom of the screen.",
    "",
    "- - Moving around -",
    "",
    "To move in the current list, use the UP and DOWN cursor keys to select",
    "an item (the selected item will be highlighted).  If the item is a",
    "category name, you may alternatively expand or collapse the list of",
    "drivers for that category by pressing [!bENTER!n].  Once the category is",
    "expanded, you can select each driver in the same manner and either:",
    "",
    "  - change its parameters using [!bENTER!n]",
    "  - move it to the Inactive list using [!bDEL!n]",
    "",
    "Use the [!bTAB!n] key to toggle between the Active and Inactive list; if",
    "you need to move a driver from the Inactive list back to the Active",
    "one, select it in the Inactive list, using [!bTAB!n] to change lists if",
    "necessary, and press [!bENTER!n] -- the device will be moved back to",
    "its place in the Active list.",
    "",
    "- - Altering the list/parameters -",
    "",
    "Any drivers for devices not installed in your system should be moved",
    "to the Inactive list, until there are no remaining parameter conflicts",
    "between the drivers, as indicated at the top.",
    "",
    "Once the list of Active drivers only contains entries for the devices",
    "present in your system, you can set their parameters (Interrupt, DMA",
    "channel, I/O addresses).  To do this, select the driver and press",
    "[!bENTER!n]: it is now possible to edit the settings at the",
    "bottom of the screen.  Use [!bTAB!n] to change fields, and when you are",
    "finished, use [!bQ!n] to return to the list.",
    "",
    "- - Saving changes -",
    "",
    "When all settings seem correct, and you wish to proceed with the",
    "kernel device probing and boot, press [!bQ!n] -- you will be asked to",
    "confirm your choice.",
    "",
    NULL
};


/**
 ** helpscreen
 **
 ** Displays help text onscreen for people that are confused, using a simple
 ** pager.
 **/
static void
helpscreen(void) 
{
    int		topline = 0;			/* where we are in the text */
    int		line = 0;			/* last line we displayed */
    int		c, delta = 1;
    char	prompt[80];

    for (;;)					/* loop until user quits */
    {
	/* display help text */
	if (delta) 
	{
	    clear();				/* remove everything else */
	    for (line = topline; 
		 (line < (topline + 24)) && (helptext[line]); 
		 line++)
		putxy(0,line-topline,helptext[line]);
	    delta = 0;
	}
	
	/* prompt */
	ksprintf(prompt,"!i --%s-- [U]p [D]own [Q]uit !n",helptext[line] ? "MORE" : "END");
	putxy(0,24,prompt);
	
	c = kgetchar();				/* so what do they say? */
	
	switch (c)
	{
	case 'u':
	case 'U':
	case 'b':
	case 'B':				/* wired into 'more' users' fingers */
	    if (topline > 0)			/* room to go up? */
	    {
		topline -= 24;
		if (topline < 0)		/* don't go too far */
		    topline = 0;
		delta = 1;
	    }
	    break;

	case 'd':
	case 'D':
	case ' ':				/* expected by most people */
	    if (helptext[line]) 		/* maybe more below? */
	    {
		topline += 24;
		delta = 1;
	    }
	    break;
	    
	case 'q':
	case 'Q':
	    redraw();				/* restore the screen */
	    return;
	}
    }
}


/** 
 ** High-level control functions
 **/


/**
 ** dolist
 **
 ** Handle user movement within (*list) in the region starting at (row) onscreen with
 ** (num) lines, starting at (*ofs) offset from row onscreen.
 ** Pass (detail) on to drawing routines.
 **
 ** If the user hits a key other than a cursor key, maybe return a code.
 **
 ** (*list) points to the device at the top line in the region, (*ofs) is the 
 ** position of the highlight within the region.  All routines below
 ** this take only a device and an absolute row : use ofsent() to find the 
 ** device, and add (*ofs) to (row) to find the absolute row.
 **/
static int 
dolist(int row, int num, int detail, int *ofs, DEV_LIST **list, char *dhelp)
{
    int		extended = 0;
    int		c;
    DEV_LIST	*lp;
    int		delta = 1;
    
    for(;;)
    {
	if (delta)
	{
	    showparams(ofsent(*ofs,*list));				/* show device parameters */
	    drawline(row+*ofs,detail,ofsent(*ofs,*list),1,dhelp);	/* highlight current line */
	    delta = 0;
	}

	c = kgetchar();				/* get a character */
	if ((extended == 2) || (c==588) || (c==596))	/* console gives "alternative" codes */
	{
	    extended = 0;			/* no longer */
	    switch(c)
	    {
	    case 588:				/* syscons' idea of 'up' */
	    case 'A':				/* up */
		if (*ofs)			/* just a move onscreen */
		{
		    drawline(row+*ofs,detail,ofsent(*ofs,*list),0,dhelp);/* unhighlight current line */
		    (*ofs)--;			/* move up */
		}else{
		    lp = prevent(*list);	/* can we go up? */
		    if (!lp)			/* no */
			break;
		    *list = lp;			/* yes, move up list */
		    drawlist(row,num,detail,*list);
		}
		delta = 1;
		break;

	    case 596:				/* dooby-do */
	    case 'B':				/* down */
		lp = ofsent(*ofs,*list);	/* get current item */
		if (!nextent(lp))
		    break;			/* nothing more to move to */
		drawline(row+*ofs,detail,ofsent(*ofs,*list),0,dhelp);	/* unhighlight current line */
		if (*ofs < (num-1))		/* room to move onscreen? */
		{
		    (*ofs)++;		    
		}else{
		    *list = nextent(*list);	/* scroll region down */
		    drawlist(row,num,detail,*list);
		}		
		delta = 1;
		break;
	    }
	}else{
	    switch(c)
	    {
	    case '\033':
		extended=1;
		break;
		    
	    case '[':				/* cheat : always preceeds cursor move */
	    case 'O':				/* ANSI application key mode */
		if (extended==1)
		    extended=2;
		else
		    extended=0;
		break;
		
	    case 'Q':
	    case 'q':
		return(KEY_EXIT);		/* user requests exit */

	    case '\r':				
	    case '\n':
		return(KEY_DO);			/* "do" something */

	    case '\b':
	    case '\177':
	    case 599:
		return(KEY_DEL);		/* "delete" response */

	    case 'X':
	    case 'x':
		return(KEY_UNZOOM);		/* expand everything */
		
	    case 'C':
	    case 'c':
		return(KEY_ZOOM);		/* collapse everything */

	    case '\t':
		drawline(row+*ofs,detail,ofsent(*ofs,*list),0,dhelp);	/* unhighlight current line */
		return(KEY_TAB);				/* "move" response */
		
	    case '\014':			/* ^L, redraw */
		return(KEY_REDRAW);
		
	    case '?':				/* helptext */
		return(KEY_HELP);
		
	    }
	}
    }		
}


/**
 ** visuserconfig
 ** 
 ** Do the fullscreen config thang
 **/
static int
visuserconfig(void)
{
    int	actofs = 0, inactofs = 0, mode = 0, ret = -1, i;
    DEV_LIST	*dp;
    
    initlist(&active);
    initlist(&inactive);
    alist = active;
    ilist = inactive;

    getdevs();

    conflicts = findconflict(active);		/* find conflicts in the active list only */

    redraw();

    for(;;)
    {
	switch(mode)
	{
	case 0:					/* active devices */
	    ret = dolist(1,8,1,&actofs,&alist,
			 "  [!bEnter!n] Edit device parameters  [!bDEL!n] Disable device");
	    switch(ret)
	    {
	    case KEY_TAB:
		mode = 1;			/* swap lists */
		break;

	    case KEY_REDRAW:
		redraw();
		break;

	    case KEY_ZOOM:
		alist = active;
		actofs = 0;
		expandlist(active);
		redrawactive();
		break;

	    case KEY_UNZOOM:
		alist = active;
		actofs = 0;
		collapselist(active);
		redrawactive();
		break;

	    case KEY_DEL:
		dp = ofsent(actofs,alist);	/* get current device */
		if (dp)				/* paranoia... */
		{
		    if (dp->attrib & FLG_MANDATORY)	/* can't be deleted */
			break;
		    if (dp == alist)		/* moving top item on list? */
		    {
			if (dp->next)
			{
			    alist = dp->next;	/* point list to non-moving item */
			}else{
			    alist = dp->prev;	/* end of list, go back instead */
			}
		    }else{
			if (!dp->next)		/* moving last item on list? */
			    actofs--;
		    }
		    dp->conflicts = 0;		/* no conflicts on the inactive list */
		    movedev(dp,inactive);	/* shift to inactive list */
		    conflicts = findconflict(active);	/* update conflict tags */
		    dp->changed = 1;
		    redrawactive();			/* redraw */
		    redrawinactive();
		}
		break;
		
	    case KEY_DO:			/* edit device parameters */
		dp = ofsent(actofs,alist);	/* get current device */
		if (dp)				/* paranoia... */
		{
		    if (dp->comment == DEV_DEVICE)	/* can't edit comments, zoom? */
		    {
			masterhelp("  [!bTAB!n]   Change fields           [!bQ!n]   Save device parameters");
			editparams(dp);
			masterhelp("  [!bTAB!n]   Change fields           [!bQ!n]   Save and Exit             [!b?!n] Help");
			putxy(0,17,lines);
			conflicts = findconflict(active);	/* update conflict tags */
		    }else{				/* DO on comment = zoom */
			switch(dp->comment)		/* Depends on current state */
			{
			case DEV_COMMENT:		/* not currently zoomed */
			    dp->comment = DEV_ZOOMED;
			    break;

			case DEV_ZOOMED:
			    dp->comment = DEV_COMMENT;
			    break;
			}
		    }
		    redrawactive();
		}
		break;
	    }
	    break;

	case 1:					/* inactive devices */
	    ret = dolist(10,7,0,&inactofs,&ilist,
			 "  [!bEnter!n] Enable device                                   ");
	    switch(ret)
	    {
	    case KEY_TAB:
		mode = 0;
		break;

	    case KEY_REDRAW:
		redraw();
		break;

	    case KEY_ZOOM:
		ilist = inactive;
		inactofs = 0;
		expandlist(inactive);
		redrawinactive();
		break;

	    case KEY_UNZOOM:
		ilist = inactive;
		inactofs = 0;
		collapselist(inactive);
		redrawinactive();
		break;

	    case KEY_DO:
		dp = ofsent(inactofs,ilist);	/* get current device */
		if (dp)				/* paranoia... */
		{
		    if (dp->comment == DEV_DEVICE)	/* can't move comments, zoom? */
		    {
			if (dp == ilist)		/* moving top of list? */
			{
			    if (dp->next)
			    {
				ilist = dp->next;	/* point list to non-moving item */
			    }else{
				ilist = dp->prev;	/* can't go down, go up instead */
			    }
			}else{
			    if (!dp->next)		/* last entry on list? */
				inactofs--;		/* shift cursor up one */
			}

			movedev(dp,active);		/* shift to active list */
			conflicts = findconflict(active);	/* update conflict tags */
			dp->changed = 1;
			alist = dp;			/* put at top and current */
			actofs = 0;
			while(dp->comment == DEV_DEVICE)
			    dp = dp->prev;		/* forcibly unzoom section */
			dp ->comment = DEV_COMMENT;
			mode = 0;			/* and swap modes to follow it */

		    }else{				/* DO on comment = zoom */
			switch(dp->comment)		/* Depends on current state */
			{
			case DEV_COMMENT:		/* not currently zoomed */
			    dp->comment = DEV_ZOOMED;
			    break;

			case DEV_ZOOMED:
			    dp->comment = DEV_COMMENT;
			    break;
			}
		    }
		    redrawactive();			/* redraw */
		    redrawinactive();
		}
		break;

	    default:				/* nothing else relevant here */
		break;
	    }
	    break;
	default:
	    mode = 0;				/* shouldn't happen... */
	}

	/* handle returns that are the same for both modes */
	switch (ret) {
	case KEY_HELP:
	    helpscreen();
	    break;
	    
	case KEY_EXIT:
	    i = yesnocancel(" Save these parameters before exiting? ([!bY!n]es/[!bN!n]o/[!bC!n]ancel) ");
	    switch(i)
	    {
	    case 2:				/* cancel */
		redraw();
		break;
		
	    case 1:				/* save and exit */
		savelist(active,1);
		savelist(inactive,0);

	    case 0:				/* exit */
		nukelist(active);		/* clean up after ourselves */
		nukelist(inactive);
		normal();
		clear();
		return(1);
	    }
	    break;
	}
    }
}
#endif /* VISUAL_USERCONFIG */

/*
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 Jordan K. Hubbard
 * All rights reserved.
 * Copyright (c) 1994 David Greenman
 * All rights reserved.
 *
 * Many additional changes by Bruce Evans
 *
 * This code is derived from software contributed by the
 * University of California Berkeley, Jordan K. Hubbard,
 * David Greenman and Bruce Evans.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/i386/userconfig.c,v 1.175.2.10 2002/10/05 18:31:48 scottl Exp $
 */

#define PARM_DEVSPEC	0x1
#define PARM_INT	0x2
#define PARM_ADDR	0x3
#define PARM_STRING	0x4

typedef struct _cmdparm {
    int type;
    union {
	struct uc_device *dparm;
	int iparm;
	union {
		void *aparm;
		const char *sparm;
	} u;
    } parm;
} CmdParm;

typedef int (*CmdFunc)(CmdParm *);

typedef struct _cmd {
    char *name;
    CmdFunc handler;
    CmdParm *parms;
} Cmd;


#if 0
static void lsscsi(void);
static int list_scsi(CmdParm *);
#endif

static int lsdevtab(struct uc_device *);
static struct uc_device *find_device(char *, int);
static struct uc_device *search_devtable(struct uc_device *, char *, int);
static void cngets(char *, int);
static Cmd *parse_cmd(char *);
static int parse_args(const char *, CmdParm *);
static int save_dev(struct uc_device *);

static int list_devices(CmdParm *);
static int set_device_ioaddr(CmdParm *);
static int set_device_irq(CmdParm *);
static int set_device_drq(CmdParm *);
static int set_device_iosize(CmdParm *);
static int set_device_mem(CmdParm *);
static int set_device_flags(CmdParm *);
static int set_device_enable(CmdParm *);
static int set_device_disable(CmdParm *);
static int quitfunc(CmdParm *);
static int helpfunc(CmdParm *);
static int introfunc(CmdParm *);

#if NPNP > 0
static int lspnp(void);
static int set_pnp_parms(CmdParm *);
#endif

static int lineno;

static CmdParm addr_parms[] = {
    { PARM_DEVSPEC, {} },
    { PARM_ADDR, {} },
    { -1, {} },
};

static CmdParm int_parms[] = {
    { PARM_DEVSPEC, {} },
    { PARM_INT, {} },
    { -1, {} },
};

static CmdParm dev_parms[] = {
    { PARM_DEVSPEC, {} },
    { -1, {} },
};

#if NPNP > 0
static CmdParm string_arg[] = {
    { PARM_STRING, {} },
    { -1, {} },
};
#endif

static Cmd CmdList[] = {
    { "?", 	helpfunc, 		NULL },		/* ? (help)	*/
    { "di",	set_device_disable,	dev_parms },	/* disable dev	*/
    { "dr",	set_device_drq,		int_parms },	/* drq dev #	*/
    { "en",	set_device_enable,	dev_parms },	/* enable dev	*/
    { "ex", 	quitfunc, 		NULL },		/* exit (quit)	*/
    { "f",	set_device_flags,	int_parms },	/* flags dev mask */
    { "h", 	helpfunc, 		NULL },		/* help		*/
    { "intro", 	introfunc, 		NULL },		/* intro screen	*/
    { "iom",	set_device_mem,		addr_parms },	/* iomem dev addr */
    { "ios",	set_device_iosize,	int_parms },	/* iosize dev size */
    { "ir",	set_device_irq,		int_parms },	/* irq dev #	*/
    { "l",	list_devices,		NULL },		/* ls, list	*/
#if NPNP > 0
    { "pn",	set_pnp_parms,		string_arg },	/* pnp ... */
#endif
    { "po",	set_device_ioaddr,	int_parms },	/* port dev addr */
    { "res",	(CmdFunc)cpu_reset,	NULL },		/* reset CPU	*/
    { "q", 	quitfunc, 		NULL },		/* quit		*/
#if 0
    { "s",	list_scsi,		NULL },		/* scsi */
#endif
#ifdef VISUAL_USERCONFIG
    { "v",	(CmdFunc)visuserconfig,	NULL },		/* visual mode */
#endif
    { NULL,	NULL,			NULL },
};

void
userconfig(void)
{
    static char banner = 1;
    char input[80];
    int rval;
    Cmd *cmd;

    load_devtab();
    init_config_script();
    while (1) {

	/* Only display signon banner if we are about to go interactive */
	if (!has_config_script()) {
	    if (!(boothowto & RB_CONFIG))
#ifdef INTRO_USERCONFIG
		banner = 0;
#else
		return;
#endif
	    if (banner) {
		banner = 0;
		kprintf("FreeBSD Kernel Configuration Utility - Version 1.2\n"
		       " Type \"help\" for help" 
#ifdef VISUAL_USERCONFIG
		       " or \"visual\" to go to the visual\n"
		       " configuration interface (requires MGA/VGA display or\n"
		       " serial terminal capable of displaying ANSI graphics)"
#endif
		       ".\n");
	    }
	}

	kprintf("config> ");
	cngets(input, 80);
	if (input[0] == '\0')
	    continue;
	cmd = parse_cmd(input);
	if (!cmd) {
	    kprintf("Invalid command or syntax.  Type `?' for help.\n");
	    continue;
	}
	rval = (*cmd->handler)(cmd->parms);
	if (rval) {
	    free_devtab();
	    return;
	}
    }
}

static Cmd *
parse_cmd(char *cmd)
{
    Cmd *cp;

    for (cp = CmdList; cp->name; cp++) {
	int len = strlen(cp->name);

	if (!strncmp(cp->name, cmd, len)) {
	    while (*cmd && *cmd != ' ' && *cmd != '\t')
		++cmd;
	    if (parse_args(cmd, cp->parms))
		return NULL;
	    else
		return cp;
	}
    }
    return NULL;
}

static int
parse_args(const char *cmd, CmdParm *parms)
{
    while (1) {
	char *ptr;

	if (*cmd == ' ' || *cmd == '\t') {
	    ++cmd;
	    continue;
	}
	if (parms == NULL || parms->type == -1) {
		if (*cmd == '\0')
			return 0;
		kprintf("Extra arg(s): %s\n", cmd);
		return 1;
	}
	if (parms->type == PARM_DEVSPEC) {
	    int i = 0;
	    char devname[64];
	    int unit = 0;

	    while (*cmd && !(*cmd == ' ' || *cmd == '\t' ||
	      (*cmd >= '0' && *cmd <= '9')))
		devname[i++] = *(cmd++);
	    devname[i] = '\0';
	    if (*cmd >= '0' && *cmd <= '9') {
		unit = strtoul(cmd, &ptr, 10);
		if (cmd == ptr) {
		    kprintf("Invalid device number\n");
		    /* XXX should print invalid token here and elsewhere. */
		    return 1;
		}
		/* XXX else should require end of token. */
		cmd = ptr;
	    }
	    if ((parms->parm.dparm = find_device(devname, unit)) == NULL) {
	        kprintf("No such device: %s%d\n", devname, unit);
		return 1;
	    }
	    ++parms;
	    continue;
	}
	if (parms->type == PARM_INT) {
	    parms->parm.iparm = strtoul(cmd, &ptr, 0);
	    if (cmd == ptr) {
	        kprintf("Invalid numeric argument\n");
		return 1;
	    }
	    cmd = ptr;
	    ++parms;
	    continue;
	}
	if (parms->type == PARM_ADDR) {
	    parms->parm.u.aparm = (void *)(uintptr_t)strtoul(cmd, &ptr, 0);
	    if (cmd == ptr) {
	        kprintf("Invalid address argument\n");
	        return 1;
	    }
	    cmd = ptr;
	    ++parms;
	    continue;
	}
	if (parms->type == PARM_STRING) {
	    parms->parm.u.sparm = cmd;
	    return 0;
	}
    }
    return 0;
}

static int
list_devices(CmdParm *parms)
{
    lineno = 0;
    if (lsdevtab(uc_devtab)) return 0;
#if NPNP > 0
    if (lspnp()) return 0;
#endif
    return 0;
}

static int
set_device_ioaddr(CmdParm *parms)
{
    parms[0].parm.dparm->id_iobase = parms[1].parm.iparm;
    save_dev(parms[0].parm.dparm);
    return 0;
}

static int
set_device_irq(CmdParm *parms)
{
    unsigned irq;

    irq = parms[1].parm.iparm;
    if (irq == 2) {
	kprintf("Warning: Remapping IRQ 2 to IRQ 9\n");
	irq = 9;
    }
    else if (irq != -1 && irq > 15) {
	kprintf("An IRQ > 15 would be invalid.\n");
	return 0;
    }
    parms[0].parm.dparm->id_irq = (irq < 16 ? 1 << irq : 0);
    save_dev(parms[0].parm.dparm);
    return 0;
}

static int
set_device_drq(CmdParm *parms)
{
    unsigned drq;

    /*
     * The bounds checking is just to ensure that the value can be printed
     * in 5 characters.  32768 gets converted to -32768 and doesn't fit.
     */
    drq = parms[1].parm.iparm;
    parms[0].parm.dparm->id_drq = (drq < 32768 ? drq : -1);
    save_dev(parms[0].parm.dparm);
    return 0;
}

static int
set_device_iosize(CmdParm *parms)
{
    parms[0].parm.dparm->id_msize = parms[1].parm.iparm;
    save_dev(parms[0].parm.dparm);
    return 0;
}

static int
set_device_mem(CmdParm *parms)
{
    parms[0].parm.dparm->id_maddr = parms[1].parm.u.aparm;
    save_dev(parms[0].parm.dparm);
    return 0;
}

static int
set_device_flags(CmdParm *parms)
{
    parms[0].parm.dparm->id_flags = parms[1].parm.iparm;
    save_dev(parms[0].parm.dparm);
    return 0;
}

static int
set_device_enable(CmdParm *parms)
{
    parms[0].parm.dparm->id_enabled = TRUE;
    save_dev(parms[0].parm.dparm);
    return 0;
}

static int
set_device_disable(CmdParm *parms)
{
    parms[0].parm.dparm->id_enabled = FALSE;
    save_dev(parms[0].parm.dparm);
    return 0;
}

#if NPNP > 0

static int
sysctl_machdep_uc_pnplist(SYSCTL_HANDLER_ARGS)
{
	int error=0;

	if(!req->oldptr) {
		/* Only sizing */
		return(SYSCTL_OUT(req,0,sizeof(struct pnp_cinfo)*MAX_PNP_LDN));
	} else {
		/*
		 * Output the pnp_ldn_overrides[] table.
		 */
		error=sysctl_handle_opaque(oidp,&pnp_ldn_overrides,
			sizeof(struct pnp_cinfo)*MAX_PNP_LDN,req);
		if(error) return(error);
		return(0);
	}
}

SYSCTL_PROC( _machdep, OID_AUTO, uc_pnplist, CTLFLAG_RD,
	0, 0, sysctl_machdep_uc_pnplist, "A",
	"List of PnP overrides changed in UserConfig");

/*
 * this function sets the kernel table to override bios PnP
 * configuration.
 */
static int      
set_pnp_parms(CmdParm *parms)      
{   
    u_long idx, val, ldn, csn;
    int i;
    char *q;
    const char *p = parms[0].parm.u.sparm;
    struct pnp_cinfo d;

    csn=strtoul(p,&q, 0);
    ldn=strtoul(q,&q, 0);
    for (p=q; *p && (*p==' ' || *p=='\t'); p++) ;
    if (csn < 1 || csn > MAX_PNP_CARDS || ldn >= MAX_PNP_LDN) {
	kprintf("bad csn/ldn %ld:%ld\n", csn, ldn);
	return 0;
    }
    for (i=0; i < MAX_PNP_LDN; i++) {
	if (pnp_ldn_overrides[i].csn == csn &&
	    pnp_ldn_overrides[i].ldn == ldn)
		break;
    }
    if (i==MAX_PNP_LDN) {
	for (i=0; i < MAX_PNP_LDN; i++) {
	    if (pnp_ldn_overrides[i].csn <1 ||
		 pnp_ldn_overrides[i].csn > MAX_PNP_CARDS)
		 break;
	}
    }
    if (i==MAX_PNP_LDN) {
	kprintf("sorry, no PnP entries available, try delete one\n");
	return 0 ;
    }
    d = pnp_ldn_overrides[i] ;
    d.csn = csn;
    d.ldn = ldn ;
    while (*p) {
	idx = 0;
	val = 0;
	if (!strncmp(p,"irq",3)) {
	    idx=strtoul(p+3,&q, 0);
	    val=strtoul(q,&q, 0);
	    if (idx >=0 && idx < 2) d.irq[idx] = val;
	} else if (!strncmp(p,"flags",5)) {
	    idx=strtoul(p+5,&q, 0);
	    d.flags = idx;
	} else if (!strncmp(p,"drq",3)) {
	    idx=strtoul(p+3,&q, 0);
	    val=strtoul(q,&q, 0);
	    if (idx >=0 && idx < 2) d.drq[idx] = val;
	} else if (!strncmp(p,"port",4)) {
	    idx=strtoul(p+4,&q, 0);
	    val=strtoul(q,&q, 0);
	    if (idx >=0 && idx < 8) d.port[idx] = val;
	} else if (!strncmp(p,"mem",3)) {
	    idx=strtoul(p+3,&q, 0);
	    val=strtoul(q,&q, 0);
	    if (idx >=0 && idx < 4) d.mem[idx].base = val;
	} else if (!strncmp(p,"bios",4)) {
	    q = p+ 4;
	    d.override = 0 ;
	} else if (!strncmp(p,"os",2)) {
	    q = p+2 ;
	    d.override = 1 ;
	} else if (!strncmp(p,"disable",7)) {
	    q = p+7 ;
	    d.enable = 0 ;
	} else if (!strncmp(p,"enable",6)) {
	    q = p+6;
	    d.enable = 1 ;
	} else if (!strncmp(p,"delete",6)) {
	    bzero(&pnp_ldn_overrides[i], sizeof (pnp_ldn_overrides[i]));
	    if (i==0) pnp_ldn_overrides[i].csn = 255;/* not reinit */
	    return 0;
	} else {
	    kprintf("unknown command <%s>\n", p);
	    break;
	}
	for (p=q; *p && (*p==' ' || *p=='\t'); p++) ;
    }
    pnp_ldn_overrides[i] = d ;
    return 0; 
}
#endif /* NPNP */

static int
quitfunc(CmdParm *parms)
{
    /*
     * If kernel config supplied, and we are parsing it, and -c also supplied,
     * ignore a quit command,  This provides a safety mechanism to allow
     * recovery from a damaged/buggy kernel config.
     */
    if ((boothowto & RB_CONFIG) && userconfig_boot_parsing)
	return 0;
    return 1;
}

static int
helpfunc(CmdParm *parms)
{
    kprintf(
    "Command\t\t\tDescription\n"
    "-------\t\t\t-----------\n"
    "ls\t\t\tList currently configured devices\n"
    "port <devname> <addr>\tSet device port (i/o address)\n"
    "irq <devname> <number>\tSet device irq\n"
    "drq <devname> <number>\tSet device drq\n"
    "iomem <devname> <addr>\tSet device maddr (memory address)\n"
    "iosize <devname> <size>\tSet device memory size\n"
    "flags <devname> <mask>\tSet device flags\n"
    "enable <devname>\tEnable device\n"
    "disable <devname>\tDisable device (will not be probed)\n");
#if NPNP > 0
    kprintf(
    "pnp <csn> <ldn> [enable|disable]\tenable/disable device\n"
    "pnp <csn> <ldn> [os|bios]\tset parameters using FreeBSD or BIOS\n"
    "pnp <csn> <ldn> [portX <addr>]\tset addr for port X (0..7)\n"
    "pnp <csn> <ldn> [memX <maddr>]\tset addr for memory range X (0..3)\n"
    "pnp <csn> <ldn> [irqX <number>]\tset irq X (0..1) to number, 0=unused\n"
    "pnp <csn> <ldn> [drqX <number>]\tset drq X (0..1) to number, 4=unused\n");
#endif
    kprintf(
    "quit\t\t\tExit this configuration utility\n"
    "reset\t\t\tReset CPU\n");
#ifdef VISUAL_USERCONFIG
    kprintf("visual\t\t\tGo to fullscreen mode.\n");
#endif
    kprintf(
    "help\t\t\tThis message\n\n"
    "Commands may be abbreviated to a unique prefix\n");
    return 0;
}

#if defined (VISUAL_USERCONFIG)
static void
center(int y, char *str)
{
    putxy((80 - strlen(str)) / 2, y, str);
}
#endif

static int
introfunc(CmdParm *parms)
{
#if defined (VISUAL_USERCONFIG)
    int curr_item, first_time, extended = 0;
    static char *choices[] = {
	" Skip kernel configuration and continue with installation ",
	" Start kernel configuration in full-screen visual mode    ",
	" Start kernel configuration in CLI mode                   ",
    };

    clear();
    center(2, "!bKernel Configuration Menu!n");

    curr_item = 0;
    first_time = 1;
    while (1) {
	char tmp[80];
	int c, i;

	if (!extended) { 
	    for (i = 0; i < 3; i++) {
		tmp[0] = '\0';
		if (curr_item == i)
		    strcpy(tmp, "!i");
		strcat(tmp, choices[i]);
		if (curr_item == i)
		    strcat(tmp, "!n");
		putxy(10, 5 + i, tmp);
	    }

	    if (first_time) {
		putxy(2, 10, "Here you have the chance to go into kernel configuration mode, making");
		putxy(2, 11, "any changes which may be necessary to properly adjust the kernel to");
		putxy(2, 12, "match your hardware configuration.");
		putxy(2, 14, "If you are installing FreeBSD for the first time, select Visual Mode");
		putxy(2, 15, "(press Down-Arrow then ENTER).");
		putxy(2, 17, "If you need to do more specialized kernel configuration and are an");
		putxy(2, 18, "experienced FreeBSD user, select CLI mode.");
		putxy(2, 20, "If you are !icertain!n that you do not need to configure your kernel");
		putxy(2, 21, "then simply press ENTER or Q now.");
		first_time = 0;
	    }
	    
	    move(0, 0);	/* move the cursor out of the way */
	}
	c = kgetchar();
	if ((extended == 2) || (c == 588) || (c == 596)) {	/* console gives "alternative" codes */
	    extended = 0;		/* no longer */
	    switch (c) {
	    case 588:
	    case 'A':				/* up */
		if (curr_item > 0)
		    --curr_item;
		break;

	    case 596:
	    case 'B':				/* down */
		if (curr_item < 2)
		    ++curr_item;
		break;
	    }
	}
	else {
	    switch(c) {
	    case '\033':
		extended = 1;
		break;
		    
	    case '[':				/* cheat : always preceeds cursor move */
	    case 'O':				/* ANSI application key mode */
		if (extended == 1)
		    extended = 2;
		else
		    extended = 0;
		break;
		
	    case -1:
	    case 'Q':
	    case 'q':
		clear();
		return 1;	/* user requests exit */

	    case '1':				/* select an item */
	    case 'S':
	    case 's':
		curr_item = 0;
		break;
	    case '2':
	    case 'V':
	    case 'v':
		curr_item = 1;
		break;
	    case '3':
	    case 'C':
	    case 'c':
		curr_item = 2;
		break;

	    case 'U':				/* up */
	    case 'u':
	    case 'P':
	    case 'p':
		if (curr_item > 0)
		    --curr_item;
		break;

	    case 'D':				/* down */
	    case 'd':
	    case 'N':
	    case 'n':
		if (curr_item < 2)
		    ++curr_item;
		break;

	    case '\r':				
	    case '\n':
		clear();
		if (!curr_item)
		    return 1;
		else if (curr_item == 1)
		    return visuserconfig();
		else {
		    putxy(0, 1, "Type \"help\" for help or \"quit\" to exit.");
		    /* enable quitfunc */
    		    userconfig_boot_parsing=0;
		    move (0, 3);
		    boothowto |= RB_CONFIG;	/* force -c */
		    return 0;
		}
		break;
	    }
	}
    }
#else	/* !VISUAL_USERCONFIG */
    return 0;
#endif	/* VISUAL_USERCONFIG */
}

#if NPNP > 0
static int
lspnp(void)
{
    struct pnp_cinfo *c;
    int i, first = 1;


    for (i=0; i< MAX_PNP_LDN; i++) {
	c = &pnp_ldn_overrides[i];
	if (c->csn >0 && c->csn != 255) {
	    int pmax, mmax;
	    static char pfmt[] =
		"port 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x ";
	    static char mfmt[] =
		"mem 0x%x 0x%x 0x%x 0x%x";
	    char buf[256];
	    if (lineno >= 23) {
		    if (!userconfig_boot_parsing) {
			    kprintf("<More> ");
			    if (kgetchar() == 'q') {
				    kprintf("quit\n");
				    return (1);
			    }
			    kprintf("\n");
		    }
		    lineno = 0;
	    }
	    if (lineno == 0 || first)
		kprintf("CSN LDN conf en irqs  drqs others (PnP devices)\n");
	    first = 0 ;
	    kprintf("%3d %3d %4s %2s %2d %-2d %2d %-2d ",
		c->csn, c->ldn,
		c->override ? "OS  ":"BIOS",
		c->enable ? "Y":"N",
		c->irq[0], c->irq[1], c->drq[0], c->drq[1]);
	    if (c->flags)
		kprintf("flags 0x%08lx ",c->flags);
	    for (pmax = 7; pmax >=0 ; pmax--)
		if (c->port[pmax]!=0) break;
	    for (mmax = 3; mmax >=0 ; mmax--)
		if (c->mem[mmax].base!=0) break;
	    if (pmax>=0) {
		strcpy(buf, pfmt);
		buf[10 + 5*pmax]='\0';
		kprintf(buf,
		    c->port[0], c->port[1], c->port[2], c->port[3],
		    c->port[4], c->port[5], c->port[6], c->port[7]);
	    }
	    if (mmax>=0) {
		strcpy(buf, mfmt);
		buf[8 + 5*mmax]='\0';
		kprintf(buf,
		    c->mem[0].base, c->mem[1].base,
		    c->mem[2].base, c->mem[3].base);
	    }
	    kprintf("\n");
	}
    }
    return 0 ;
}
#endif /* NPNP */
		
static int
lsdevtab(struct uc_device *dt)
{
    for (; dt->id_id != 0; dt++) {
	char dname[80];

	if (lineno >= 23) {
		kprintf("<More> ");
		if (!userconfig_boot_parsing) {
			if (kgetchar() == 'q') {
				kprintf("quit\n");
				return (1);
			}
			kprintf("\n");
		}
		lineno = 0;
	}
	if (lineno == 0) {
		kprintf(
"Device   port       irq   drq   iomem   iosize   unit  flags      enab\n"
		    );
		++lineno;
	}
	ksprintf(dname, "%s%d", dt->id_name, dt->id_unit);
	kprintf("%-9.9s%-#11x%-6d%-6d%-8p%-9d%-6d%-#11x%-5s\n",
	    dname, /* dt->id_id, dt->id_driver(by name), */ dt->id_iobase,
	    ffs(dt->id_irq) - 1, dt->id_drq, dt->id_maddr, dt->id_msize,
	    /* dt->id_intr(by name), */ dt->id_unit, dt->id_flags,
	    dt->id_enabled ? "Yes" : "No");
	++lineno;
    }
    return(0);
}

static void
load_devtab(void)
{
    int i, val;
    int count = resource_count();
    int id = 1;
    int dt;
    char *name;
    int unit;

    uc_devtab = kmalloc(sizeof(struct uc_device)*(count + 1), M_DEVL,
	M_WAITOK | M_ZERO);
    dt = 0;
    for (i = 0; i < count; i++) {
	name = resource_query_name(i);
	unit = resource_query_unit(i);
	if (unit < 0)
	    continue;	/* skip wildcards */
	uc_devtab[dt].id_id = id++;
	resource_int_value(name, unit, "port", &uc_devtab[dt].id_iobase);
	val = 0;
	resource_int_value(name, unit, "irq", &val);
	uc_devtab[dt].id_irq = (1 << val);
	resource_int_value(name, unit, "drq", &uc_devtab[dt].id_drq);
	resource_int_value(name, unit, "maddr",(int *)&uc_devtab[dt].id_maddr);
	resource_int_value(name, unit, "msize", &uc_devtab[dt].id_msize);
	uc_devtab[dt].id_unit = unit;
	resource_int_value(name, unit, "flags", &uc_devtab[dt].id_flags);
	val = 0;
	resource_int_value(name, unit, "disabled", &val);
	uc_devtab[dt].id_enabled = !val;
	uc_devtab[dt].id_name = kmalloc(strlen(name) + 1, M_DEVL,M_WAITOK);
	strcpy(uc_devtab[dt].id_name, name);
	dt++;
    }
}

static void
free_devtab(void)
{
    int i;
    int count = resource_count();

    for (i = 0; i < count; i++)
	if (uc_devtab[i].id_name)
	    kfree(uc_devtab[i].id_name, M_DEVL);
    kfree(uc_devtab, M_DEVL);
}
    
static struct uc_device *
find_device(char *devname, int unit)
{
    struct uc_device *ret;

    if ((ret = search_devtable(uc_devtab, devname, unit)) != NULL)
        return ret;
    return NULL;
}

static struct uc_device *
search_devtable(struct uc_device *dt, char *devname, int unit)
{
    for (; dt->id_id != 0; dt++)
        if (!strcmp(dt->id_name, devname) && dt->id_unit == unit)
	    return dt;
    return NULL;
}

static void
cngets(char *input, int maxin)
{
    int c, nchars = 0;

    while (1) {
	c = kgetchar();
	/* Treat ^H or ^? as backspace */
	if ((c == '\010' || c == '\177')) {
	    	if (nchars) {
			kprintf("\010 \010");
			*--input = '\0', --nchars;
		}
		continue;
	}
	/* Treat ^U or ^X as kill line */
	else if ((c == '\025' || c == '\030')) {
		while (nchars) {
			kprintf("\010 \010");
			*--input = '\0', --nchars;
		}
		continue;
	}
	kprintf("%c", c);
	if ((++nchars == maxin) || (c == '\n') || (c == '\r') || ( c == -1)) {
	    *input = '\0';
	    break;
	}
	*input++ = (u_char)c;
    }
}


#if 0
/* scsi: Support for displaying configured SCSI devices.
 * There is no way to edit them, and this is inconsistent
 * with the ISA method.  This is here as a basis for further work.
 */
static char *
type_text(char *name)	/* XXX: This is bogus */
{
	if (strcmp(name, "sd") == 0)
		return "disk";

	if (strcmp(name, "st") == 0)
		return "tape";

	return "device";
}

id_put(char *desc, int id)
{
    if (id != SCCONF_UNSPEC)
    {
    	if (desc)
	    kprintf("%s", desc);

    	if (id == SCCONF_ANY)
	    kprintf("?");
        else
	    kprintf("%d", id);
    }
}

static void
lsscsi(void)
{
    int i;

    kprintf("scsi: (can't be edited):\n");

    for (i = 0; scsi_cinit[i].driver; i++)
    {
	id_put("controller scbus", scsi_cinit[i].scbus);

	if (scsi_cinit[i].unit != -1)
	{
	    kprintf(" at ");
	    id_put(scsi_cinit[i].driver, scsi_cinit[i].unit);
	}

	kprintf("\n");
    }

    for (i = 0; scsi_dinit[i].name; i++)
    {
		kprintf("%s ", type_text(scsi_dinit[i].name));

		id_put(scsi_dinit[i].name, scsi_dinit[i].unit);
		id_put(" at scbus", scsi_dinit[i].cunit);
		id_put(" target ", scsi_dinit[i].target);
		id_put(" lun ", scsi_dinit[i].lun);

		if (scsi_dinit[i].flags)
	    	kprintf(" flags 0x%x\n", scsi_dinit[i].flags);

		kprintf("\n");
    }
}

static int
list_scsi(CmdParm *parms)
{
    lineno = 0;
    lsscsi();
    return 0;
}
#endif

static void
save_resource(struct uc_device *idev)
{
    char *name;
    int unit;

    name = idev->id_name;
    unit = idev->id_unit;
    resource_set_int(name, unit, "port", idev->id_iobase);
    resource_set_int(name, unit, "irq", ffs(idev->id_irq) - 1);
    resource_set_int(name, unit, "drq", idev->id_drq);
    resource_set_int(name, unit, "maddr", (int)idev->id_maddr);
    resource_set_int(name, unit, "msize", idev->id_msize);
    resource_set_int(name, unit, "flags", idev->id_flags);
    resource_set_int(name, unit, "disabled", !idev->id_enabled);
}

static int
save_dev(struct uc_device *idev)
{
	struct uc_device	*id_p,*id_pn;
	char *name = idev->id_name;

	for (id_p = uc_devlist; id_p; id_p = id_p->id_next) {
		if (id_p->id_id == idev->id_id) {
			id_pn = id_p->id_next;
			if (id_p->id_name)
				kfree(id_p->id_name, M_DEVL);
			bcopy(idev,id_p,sizeof(struct uc_device));
			save_resource(idev);
			id_p->id_name = kmalloc(strlen(name)+1, M_DEVL,M_WAITOK);
			strcpy(id_p->id_name, name);
			id_p->id_next = id_pn;
			return 1;
		}
	}
	id_pn = kmalloc(sizeof(struct uc_device),M_DEVL,M_WAITOK);
	bcopy(idev,id_pn,sizeof(struct uc_device));
	save_resource(idev);
	id_pn->id_name = kmalloc(strlen(name) + 1, M_DEVL,M_WAITOK);
	strcpy(id_pn->id_name, name);
	id_pn->id_next = uc_devlist;
	uc_devlist = id_pn;
	return 0;
}


