/*-
 * Copyright (c) 1998 Michael Smith
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/subr_module.c,v 1.6 1999/10/11 15:19:10 peter Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/linker.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>

#include <machine/metadata.h>

/*
 * Preloaded module support
 */

caddr_t	preload_metadata;

/*
 * Search for the preloaded module (name)
 */
caddr_t
preload_search_by_name(const char *name)
{
    caddr_t	curp;
    u_int32_t	*hdr;
    int		next;
    int		i;
    char	*scanname;

    if (preload_metadata == NULL)
	return(NULL);

    curp = preload_metadata;
    for (;;) {
	hdr = (u_int32_t *)curp;
	if (hdr[0] == 0 && hdr[1] == 0)
	    break;

	/*
	 * Search for a MODINFO_NAME field.  the boot loader really
	 * ought to strip the path names
	 */
	if (hdr[0] == MODINFO_NAME) {
	    scanname = curp + sizeof(u_int32_t) * 2;
	    i = strlen(scanname);
	    while (i > 0 && scanname[i-1] != '/')
		--i;
	    if (strcmp(name, scanname) == 0)
		return(curp);
	    if (strcmp(name, scanname + i) == 0)
		return(curp);
	}
	/* skip to next field */
	next = sizeof(u_int32_t) * 2 + hdr[1];
	next = roundup(next, sizeof(u_long));
	curp += next;
    }
    return(NULL);
}

/*
 * Search for the first preloaded module of (type)
 */
caddr_t
preload_search_by_type(const char *type)
{
    caddr_t	curp, lname;
    u_int32_t	*hdr;
    int		next;

    if (preload_metadata != NULL) {

	curp = preload_metadata;
	lname = NULL;
	for (;;) {
	    hdr = (u_int32_t *)curp;
	    if (hdr[0] == 0 && hdr[1] == 0)
		break;

	    /* remember the start of each record */
	    if (hdr[0] == MODINFO_NAME)
		lname = curp;

	    /* Search for a MODINFO_TYPE field */
	    if ((hdr[0] == MODINFO_TYPE) &&
		!strcmp(type, curp + sizeof(u_int32_t) * 2))
		return(lname);

	    /* skip to next field */
	    next = sizeof(u_int32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	}
    }
    return(NULL);
}

/*
 * Walk through the preloaded module list
 */
caddr_t
preload_search_next_name(caddr_t base)
{
    caddr_t	curp;
    u_int32_t	*hdr;
    int		next;

    if (preload_metadata != NULL) {

	/* Pick up where we left off last time */
	if (base) {
	    /* skip to next field */
	    curp = base;
	    hdr = (u_int32_t *)curp;
	    next = sizeof(u_int32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	} else
	    curp = preload_metadata;

	for (;;) {
	    hdr = (u_int32_t *)curp;
	    if (hdr[0] == 0 && hdr[1] == 0)
		break;

	    /* Found a new record? */
	    if (hdr[0] == MODINFO_NAME)
		return curp;

	    /* skip to next field */
	    next = sizeof(u_int32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	}
    }
    return(NULL);
}

/*
 * Given a preloaded module handle (mod), return a pointer
 * to the data for the attribute (inf).
 */
caddr_t
preload_search_info(caddr_t mod, int inf)
{
    caddr_t	curp;
    u_int32_t	*hdr;
    u_int32_t	type = 0;
    int		next;

    curp = mod;
    for (;;) {
	hdr = (u_int32_t *)curp;
	/* end of module data? */
	if (hdr[0] == 0 && hdr[1] == 0)
	    break;
	/*
	 * We give up once we've looped back to what we were looking at
	 * first - this should normally be a MODINFO_NAME field.
	 */
	if (type == 0) {
	    type = hdr[0];
	} else {
	    if (hdr[0] == type)
		break;
	}

	/*
	 * Attribute match? Return pointer to data.
	 * Consumer may safely assume that size value preceeds
	 * data.
	 */
	if (hdr[0] == inf)
	    return(curp + (sizeof(u_int32_t) * 2));

	/* skip to next field */
	next = sizeof(u_int32_t) * 2 + hdr[1];
	next = roundup(next, sizeof(u_long));
	curp += next;
    }
    return(NULL);
}

/*
 * Delete a preload record by path name.
 */
void
preload_delete_name(const char *name)
{
    caddr_t	curp;
    u_int32_t	*hdr;
    int		next;
    int		clearing;

    if (preload_metadata != NULL) {
	clearing = 0;
	curp = preload_metadata;
	for (;;) {
	    hdr = (u_int32_t *)curp;
	    if (hdr[0] == 0 && hdr[1] == 0)
		break;

	    /* Search for a MODINFO_NAME field */
	    if (hdr[0] == MODINFO_NAME) {
		if (strcmp(name, curp + sizeof(u_int32_t) * 2) == 0)
		    clearing = 1;	/* start clearing */
		else if (clearing)
		    clearing = 0;	/* at next module now, stop clearing */
	    }
	    if (clearing)
		hdr[0] = MODINFO_EMPTY;

	    /* skip to next field */
	    next = sizeof(u_int32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	}
    }
}

/* Called from hammer_time() on pc64.  Convert physical pointers to kvm. Sigh. */
void
preload_bootstrap_relocate(vm_offset_t offset)
{
    caddr_t	curp;
    u_int32_t	*hdr;
    vm_offset_t	*ptr;
    int		next;

    if (preload_metadata != NULL) {

	curp = preload_metadata;
	for (;;) {
	    hdr = (u_int32_t *)curp;
	    if (hdr[0] == 0 && hdr[1] == 0)
		break;

	    /* Deal with the ones that we know we have to fix */
	    switch (hdr[0]) {
	    case MODINFO_ADDR:
	    case MODINFO_METADATA|MODINFOMD_SSYM:
	    case MODINFO_METADATA|MODINFOMD_ESYM:
		ptr = (vm_offset_t *)(curp + (sizeof(u_int32_t) * 2));
		*ptr += offset;
		break;
	    }
	    /* The rest is beyond us for now */

	    /* skip to next field */
	    next = sizeof(u_int32_t) * 2 + hdr[1];
	    next = roundup(next, sizeof(u_long));
	    curp += next;
	}
    }
}

/*
 * Parse the modinfo type and append to the sbuf.
 */
static void
preload_modinfo_type(struct sbuf *sbp, int type)
{
    if ((type & MODINFO_METADATA) == 0) {
	switch (type) {
	case MODINFO_END:
	    sbuf_cat(sbp, "MODINFO_END");
	    break;
	case MODINFO_NAME:
	    sbuf_cat(sbp, "MODINFO_NAME");
	    break;
	case MODINFO_TYPE:
	    sbuf_cat(sbp, "MODINFO_TYPE");
	    break;
	case MODINFO_ADDR:
	    sbuf_cat(sbp, "MODINFO_ADDR");
	    break;
	case MODINFO_SIZE:
	    sbuf_cat(sbp, "MODINFO_SIZE");
	    break;
	case MODINFO_EMPTY:
	    sbuf_cat(sbp, "MODINFO_EMPTY");
	    break;
	case MODINFO_ARGS:
	    sbuf_cat(sbp, "MODINFO_ARGS");
	    break;
	default:
	    sbuf_cat(sbp, "unrecognized modinfo attribute");
	}

	return;
    }

    sbuf_cat(sbp, "MODINFO_METADATA | ");
    switch (type & ~MODINFO_METADATA) {
    case MODINFOMD_ELFHDR:
	sbuf_cat(sbp, "MODINFOMD_ELFHDR");
	break;
    case MODINFOMD_SSYM:
	sbuf_cat(sbp, "MODINFOMD_SSYM");
	break;
    case MODINFOMD_ESYM:
	sbuf_cat(sbp, "MODINFOMD_ESYM");
	break;
    case MODINFOMD_DYNAMIC:
	sbuf_cat(sbp, "MODINFOMD_DYNAMIC");
	break;
    case MODINFOMD_ENVP:
	sbuf_cat(sbp, "MODINFOMD_ENVP");
	break;
    case MODINFOMD_HOWTO:
	sbuf_cat(sbp, "MODINFOMD_HOWTO");
	break;
    case MODINFOMD_KERNEND:
	sbuf_cat(sbp, "MODINFOMD_KERNEND");
	break;
    case MODINFOMD_SHDR:
	sbuf_cat(sbp, "MODINFOMD_SHDR");
	break;
    case MODINFOMD_FW_HANDLE:
	sbuf_cat(sbp, "MODINFOMD_FW_HANDLE");
	break;
    case MODINFOMD_SMAP:
	sbuf_cat(sbp, "MODINFOMD_SMAP");
	break;
    case MODINFOMD_EFI_MAP:
	sbuf_cat(sbp, "MODINFOMD_EFI_MAP");
	break;
    case MODINFOMD_EFI_FB:
	sbuf_cat(sbp, "MODINFOMD_EFI_FB");
	break;
    default:
	sbuf_cat(sbp, "unrecognized metadata type");
    }
}

/*
 * Print the modinfo value, depending on type.
 */
static void
preload_modinfo_value(struct sbuf *sbp, uint32_t *bptr, int type, int len)
{
    switch (type) {
    case MODINFO_NAME:
    case MODINFO_TYPE:
    case MODINFO_ARGS:
	sbuf_printf(sbp, "%s", (char *)bptr);
	break;
    case MODINFO_SIZE:
	sbuf_printf(sbp, "%lu", *(u_long *)bptr);
	break;
    case MODINFO_ADDR:
    case MODINFO_METADATA | MODINFOMD_SSYM:
    case MODINFO_METADATA | MODINFOMD_ESYM:
    case MODINFO_METADATA | MODINFOMD_DYNAMIC:
    case MODINFO_METADATA | MODINFOMD_KERNEND:
    case MODINFO_METADATA | MODINFOMD_ENVP:
    case MODINFO_METADATA | MODINFOMD_SMAP:
    case MODINFO_METADATA | MODINFOMD_EFI_FB:
	sbuf_printf(sbp, "0x%016lx", *(vm_offset_t *)bptr);
	break;
    case MODINFO_METADATA | MODINFOMD_HOWTO:
	sbuf_printf(sbp, "0x%08x", *bptr);
	break;
    case MODINFO_METADATA | MODINFOMD_SHDR:
    case MODINFO_METADATA | MODINFOMD_ELFHDR:
    case MODINFO_METADATA | MODINFOMD_FW_HANDLE:
    case MODINFO_METADATA | MODINFOMD_EFI_MAP:
	/* Don't print data buffers. */
	sbuf_cat(sbp, "buffer contents omitted");
	break;
    default:
	break;
    }
}

static void
preload_dump_internal(struct sbuf *sbp)
{
    uint32_t *bptr, type, len;

    sbuf_putc(sbp, '\n');

    /* Iterate through the TLV-encoded sections. */
    bptr = (uint32_t *)preload_metadata;
    while (bptr[0] != MODINFO_END) {
	sbuf_printf(sbp, " %p:\n", bptr);

	type = *bptr++;
	sbuf_printf(sbp, "\ttype:\t(%#04x) ", type);
	preload_modinfo_type(sbp, type);
	sbuf_putc(sbp, '\n');

	len = *bptr++;
	sbuf_printf(sbp, "\tlen:\t%u\n", len);

	sbuf_cat(sbp, "\tvalue:\t");
	preload_modinfo_value(sbp, bptr, type, len);
	sbuf_putc(sbp, '\n');

	bptr += roundup(len, sizeof(u_long)) / sizeof(uint32_t);
    }
}

static int
sysctl_preload_dump(SYSCTL_HANDLER_ARGS)
{
    struct sbuf sb;
    int error;

    if (preload_metadata == NULL)
	return (EINVAL);

    sbuf_new_for_sysctl(&sb, NULL, 512, req);
    preload_dump_internal(&sb);

    error = sbuf_finish(&sb);
    sbuf_delete(&sb);

    return (error);
}

SYSCTL_PROC(_debug, OID_AUTO, dump_modinfo,
	    CTLTYPE_STRING | CTLFLAG_RD,
	    NULL, 0, sysctl_preload_dump, "A",
	    "pretty-print the bootloader metadata");
