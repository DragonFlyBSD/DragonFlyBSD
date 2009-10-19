/* Handle SVR4 shared libraries for GDB, the GNU Debugger.

   Copyright (C) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1998, 1999, 2000,
   2001, 2003, 2004, 2005, 2006, 2007, 2008, 2009
   Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"

#include "elf/external.h"
#include "elf/common.h"
#include "elf/mips.h"

#include "symtab.h"
#include "bfd.h"
#include "symfile.h"
#include "objfiles.h"
#include "gdbcore.h"
#include "target.h"
#include "inferior.h"
#include "regcache.h"
#include "gdbthread.h"
#include "observer.h"

#include "gdb_assert.h"

#include "solist.h"
#include "solib.h"
#include "solib-svr4.h"

#include "bfd-target.h"
#include "elf-bfd.h"
#include "exec.h"
#include "auxv.h"
#include "exceptions.h"

static struct link_map_offsets *svr4_fetch_link_map_offsets (void);
static int svr4_have_link_map_offsets (void);

/* Link map info to include in an allocated so_list entry */

struct lm_info
  {
    /* Pointer to copy of link map from inferior.  The type is char *
       rather than void *, so that we may use byte offsets to find the
       various fields without the need for a cast.  */
    gdb_byte *lm;

    /* Amount by which addresses in the binary should be relocated to
       match the inferior.  This could most often be taken directly
       from lm, but when prelinking is involved and the prelink base
       address changes, we may need a different offset, we want to
       warn about the difference and compute it only once.  */
    CORE_ADDR l_addr;

    /* The target location of lm.  */
    CORE_ADDR lm_addr;
  };

/* On SVR4 systems, a list of symbols in the dynamic linker where
   GDB can try to place a breakpoint to monitor shared library
   events.

   If none of these symbols are found, or other errors occur, then
   SVR4 systems will fall back to using a symbol as the "startup
   mapping complete" breakpoint address.  */

static char *solib_break_names[] =
{
  "r_debug_state",
  "_r_debug_state",
  "_dl_debug_state",
  "rtld_db_dlactivity",
  "_rtld_debug_state",

  NULL
};

static char *bkpt_names[] =
{
  "_start",
  "__start",
  "main",
  NULL
};

static char *main_name_list[] =
{
  "main_$main",
  NULL
};

/* Return non-zero if GDB_SO_NAME and INFERIOR_SO_NAME represent
   the same shared library.  */

static int
svr4_same_1 (const char *gdb_so_name, const char *inferior_so_name)
{
  if (strcmp (gdb_so_name, inferior_so_name) == 0)
    return 1;

  /* On Solaris, when starting inferior we think that dynamic linker is
     /usr/lib/ld.so.1, but later on, the table of loaded shared libraries 
     contains /lib/ld.so.1.  Sometimes one file is a link to another, but 
     sometimes they have identical content, but are not linked to each
     other.  We don't restrict this check for Solaris, but the chances
     of running into this situation elsewhere are very low.  */
  if (strcmp (gdb_so_name, "/usr/lib/ld.so.1") == 0
      && strcmp (inferior_so_name, "/lib/ld.so.1") == 0)
    return 1;

  /* Similarly, we observed the same issue with sparc64, but with
     different locations.  */
  if (strcmp (gdb_so_name, "/usr/lib/sparcv9/ld.so.1") == 0
      && strcmp (inferior_so_name, "/lib/sparcv9/ld.so.1") == 0)
    return 1;

  return 0;
}

static int
svr4_same (struct so_list *gdb, struct so_list *inferior)
{
  return (svr4_same_1 (gdb->so_original_name, inferior->so_original_name));
}

/* link map access functions */

static CORE_ADDR
LM_ADDR_FROM_LINK_MAP (struct so_list *so)
{
  struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();
  struct type *ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;

  return extract_typed_address (so->lm_info->lm + lmo->l_addr_offset,
				ptr_type);
}

static int
HAS_LM_DYNAMIC_FROM_LINK_MAP (void)
{
  struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();

  return lmo->l_ld_offset >= 0;
}

static CORE_ADDR
LM_DYNAMIC_FROM_LINK_MAP (struct so_list *so)
{
  struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();
  struct type *ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;

  return extract_typed_address (so->lm_info->lm + lmo->l_ld_offset,
				ptr_type);
}

static CORE_ADDR
LM_ADDR_CHECK (struct so_list *so, bfd *abfd)
{
  if (so->lm_info->l_addr == (CORE_ADDR)-1)
    {
      struct bfd_section *dyninfo_sect;
      CORE_ADDR l_addr, l_dynaddr, dynaddr, align = 0x1000;

      l_addr = LM_ADDR_FROM_LINK_MAP (so);

      if (! abfd || ! HAS_LM_DYNAMIC_FROM_LINK_MAP ())
	goto set_addr;

      l_dynaddr = LM_DYNAMIC_FROM_LINK_MAP (so);

      dyninfo_sect = bfd_get_section_by_name (abfd, ".dynamic");
      if (dyninfo_sect == NULL)
	goto set_addr;

      dynaddr = bfd_section_vma (abfd, dyninfo_sect);

      if (dynaddr + l_addr != l_dynaddr)
	{
	  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour)
	    {
	      Elf_Internal_Ehdr *ehdr = elf_tdata (abfd)->elf_header;
	      Elf_Internal_Phdr *phdr = elf_tdata (abfd)->phdr;
	      int i;

	      align = 1;

	      for (i = 0; i < ehdr->e_phnum; i++)
		if (phdr[i].p_type == PT_LOAD && phdr[i].p_align > align)
		  align = phdr[i].p_align;
	    }

	  /* Turn it into a mask.  */
	  align--;

	  /* If the changes match the alignment requirements, we
	     assume we're using a core file that was generated by the
	     same binary, just prelinked with a different base offset.
	     If it doesn't match, we may have a different binary, the
	     same binary with the dynamic table loaded at an unrelated
	     location, or anything, really.  To avoid regressions,
	     don't adjust the base offset in the latter case, although
	     odds are that, if things really changed, debugging won't
	     quite work.  */
	  if ((l_addr & align) == ((l_dynaddr - dynaddr) & align))
	    {
	      l_addr = l_dynaddr - dynaddr;

	      warning (_(".dynamic section for \"%s\" "
		     "is not at the expected address"), so->so_name);
	      warning (_("difference appears to be caused by prelink, "
			 "adjusting expectations"));
	    }
	  else
	    warning (_(".dynamic section for \"%s\" "
		       "is not at the expected address "
		       "(wrong library or version mismatch?)"), so->so_name);
	}

    set_addr:
      so->lm_info->l_addr = l_addr;
    }

  return so->lm_info->l_addr;
}

static CORE_ADDR
LM_NEXT (struct so_list *so)
{
  struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();
  struct type *ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;

  return extract_typed_address (so->lm_info->lm + lmo->l_next_offset,
				ptr_type);
}

static CORE_ADDR
LM_NAME (struct so_list *so)
{
  struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();
  struct type *ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;

  return extract_typed_address (so->lm_info->lm + lmo->l_name_offset,
				ptr_type);
}

static int
IGNORE_FIRST_LINK_MAP_ENTRY (struct so_list *so)
{
  struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();
  struct type *ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;

  /* Assume that everything is a library if the dynamic loader was loaded
     late by a static executable.  */
  if (exec_bfd && bfd_get_section_by_name (exec_bfd, ".dynamic") == NULL)
    return 0;

  return extract_typed_address (so->lm_info->lm + lmo->l_prev_offset,
				ptr_type) == 0;
}

/* Per-inferior SVR4 specific data.  */

struct svr4_info
{
  int pid;

  CORE_ADDR debug_base;	/* Base of dynamic linker structures */

  /* Validity flag for debug_loader_offset.  */
  int debug_loader_offset_p;

  /* Load address for the dynamic linker, inferred.  */
  CORE_ADDR debug_loader_offset;

  /* Name of the dynamic linker, valid if debug_loader_offset_p.  */
  char *debug_loader_name;

  /* Load map address for the main executable.  */
  CORE_ADDR main_lm_addr;
};

/* List of known processes using solib-svr4 shared libraries, storing
   the required bookkeeping for each.  */

typedef struct svr4_info *svr4_info_p;
DEF_VEC_P(svr4_info_p);
VEC(svr4_info_p) *svr4_info = NULL;

/* Get svr4 data for inferior PID (target id).  If none is found yet,
   add it now.  This function always returns a valid object.  */

struct svr4_info *
get_svr4_info (int pid)
{
  int ix;
  struct svr4_info *it;

  gdb_assert (pid != 0);

  for (ix = 0; VEC_iterate (svr4_info_p, svr4_info, ix, it); ++ix)
    {
      if (it->pid == pid)
	return it;
    }

  it = XZALLOC (struct svr4_info);
  it->pid = pid;

  VEC_safe_push (svr4_info_p, svr4_info, it);

  return it;
}

/* Get rid of any svr4 related bookkeeping for inferior PID (target
   id).  */

static void
remove_svr4_info (int pid)
{
  int ix;
  struct svr4_info *it;

  for (ix = 0; VEC_iterate (svr4_info_p, svr4_info, ix, it); ++ix)
    {
      if (it->pid == pid)
	{
	  VEC_unordered_remove (svr4_info_p, svr4_info, ix);
	  return;
	}
    }
}

/* This is an "inferior_exit" observer.  Inferior PID (target id) is
   being removed from the inferior list, because it exited, was
   killed, detached, or we just dropped the connection to the debug
   interface --- discard any solib-svr4 related bookkeeping for this
   inferior.  */

static void
solib_svr4_inferior_exit (int pid)
{
  remove_svr4_info (pid);
}

/* Local function prototypes */

static int match_main (char *);

static CORE_ADDR bfd_lookup_symbol (bfd *, char *);

/*

   LOCAL FUNCTION

   bfd_lookup_symbol -- lookup the value for a specific symbol

   SYNOPSIS

   CORE_ADDR bfd_lookup_symbol (bfd *abfd, char *symname)

   DESCRIPTION

   An expensive way to lookup the value of a single symbol for
   bfd's that are only temporary anyway.  This is used by the
   shared library support to find the address of the debugger
   notification routine in the shared library.

   The returned symbol may be in a code or data section; functions
   will normally be in a code section, but may be in a data section
   if this architecture uses function descriptors.

   Note that 0 is specifically allowed as an error return (no
   such symbol).
 */

static CORE_ADDR
bfd_lookup_symbol (bfd *abfd, char *symname)
{
  long storage_needed;
  asymbol *sym;
  asymbol **symbol_table;
  unsigned int number_of_symbols;
  unsigned int i;
  struct cleanup *back_to;
  CORE_ADDR symaddr = 0;

  storage_needed = bfd_get_symtab_upper_bound (abfd);

  if (storage_needed > 0)
    {
      symbol_table = (asymbol **) xmalloc (storage_needed);
      back_to = make_cleanup (xfree, symbol_table);
      number_of_symbols = bfd_canonicalize_symtab (abfd, symbol_table);

      for (i = 0; i < number_of_symbols; i++)
	{
	  sym = *symbol_table++;
	  if (strcmp (sym->name, symname) == 0
              && (sym->section->flags & (SEC_CODE | SEC_DATA)) != 0)
	    {
	      /* BFD symbols are section relative.  */
	      symaddr = sym->value + sym->section->vma;
	      break;
	    }
	}
      do_cleanups (back_to);
    }

  if (symaddr)
    return symaddr;

  /* On FreeBSD, the dynamic linker is stripped by default.  So we'll
     have to check the dynamic string table too.  */

  storage_needed = bfd_get_dynamic_symtab_upper_bound (abfd);

  if (storage_needed > 0)
    {
      symbol_table = (asymbol **) xmalloc (storage_needed);
      back_to = make_cleanup (xfree, symbol_table);
      number_of_symbols = bfd_canonicalize_dynamic_symtab (abfd, symbol_table);

      for (i = 0; i < number_of_symbols; i++)
	{
	  sym = *symbol_table++;

	  if (strcmp (sym->name, symname) == 0
              && (sym->section->flags & (SEC_CODE | SEC_DATA)) != 0)
	    {
	      /* BFD symbols are section relative.  */
	      symaddr = sym->value + sym->section->vma;
	      break;
	    }
	}
      do_cleanups (back_to);
    }

  return symaddr;
}


/* Read program header TYPE from inferior memory.  The header is found
   by scanning the OS auxillary vector.

   Return a pointer to allocated memory holding the program header contents,
   or NULL on failure.  If sucessful, and unless P_SECT_SIZE is NULL, the
   size of those contents is returned to P_SECT_SIZE.  Likewise, the target
   architecture size (32-bit or 64-bit) is returned to P_ARCH_SIZE.  */

static gdb_byte *
read_program_header (int type, int *p_sect_size, int *p_arch_size)
{
  enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  CORE_ADDR at_phdr, at_phent, at_phnum;
  int arch_size, sect_size;
  CORE_ADDR sect_addr;
  gdb_byte *buf;

  /* Get required auxv elements from target.  */
  if (target_auxv_search (&current_target, AT_PHDR, &at_phdr) <= 0)
    return 0;
  if (target_auxv_search (&current_target, AT_PHENT, &at_phent) <= 0)
    return 0;
  if (target_auxv_search (&current_target, AT_PHNUM, &at_phnum) <= 0)
    return 0;
  if (!at_phdr || !at_phnum)
    return 0;

  /* Determine ELF architecture type.  */
  if (at_phent == sizeof (Elf32_External_Phdr))
    arch_size = 32;
  else if (at_phent == sizeof (Elf64_External_Phdr))
    arch_size = 64;
  else
    return 0;

  /* Find .dynamic section via the PT_DYNAMIC PHDR.  */
  if (arch_size == 32)
    {
      Elf32_External_Phdr phdr;
      int i;

      /* Search for requested PHDR.  */
      for (i = 0; i < at_phnum; i++)
	{
	  if (target_read_memory (at_phdr + i * sizeof (phdr),
				  (gdb_byte *)&phdr, sizeof (phdr)))
	    return 0;

	  if (extract_unsigned_integer ((gdb_byte *)phdr.p_type,
					4, byte_order) == type)
	    break;
	}

      if (i == at_phnum)
	return 0;

      /* Retrieve address and size.  */
      sect_addr = extract_unsigned_integer ((gdb_byte *)phdr.p_vaddr,
					    4, byte_order);
      sect_size = extract_unsigned_integer ((gdb_byte *)phdr.p_memsz,
					    4, byte_order);
    }
  else
    {
      Elf64_External_Phdr phdr;
      int i;

      /* Search for requested PHDR.  */
      for (i = 0; i < at_phnum; i++)
	{
	  if (target_read_memory (at_phdr + i * sizeof (phdr),
				  (gdb_byte *)&phdr, sizeof (phdr)))
	    return 0;

	  if (extract_unsigned_integer ((gdb_byte *)phdr.p_type,
					4, byte_order) == type)
	    break;
	}

      if (i == at_phnum)
	return 0;

      /* Retrieve address and size.  */
      sect_addr = extract_unsigned_integer ((gdb_byte *)phdr.p_vaddr,
					    8, byte_order);
      sect_size = extract_unsigned_integer ((gdb_byte *)phdr.p_memsz,
					    8, byte_order);
    }

  /* Read in requested program header.  */
  buf = xmalloc (sect_size);
  if (target_read_memory (sect_addr, buf, sect_size))
    {
      xfree (buf);
      return NULL;
    }

  if (p_arch_size)
    *p_arch_size = arch_size;
  if (p_sect_size)
    *p_sect_size = sect_size;

  return buf;
}


/* Return program interpreter string.  */
static gdb_byte *
find_program_interpreter (void)
{
  gdb_byte *buf = NULL;

  /* If we have an exec_bfd, use its section table.  */
  if (exec_bfd
      && bfd_get_flavour (exec_bfd) == bfd_target_elf_flavour)
   {
     struct bfd_section *interp_sect;

     interp_sect = bfd_get_section_by_name (exec_bfd, ".interp");
     if (interp_sect != NULL)
      {
	CORE_ADDR sect_addr = bfd_section_vma (exec_bfd, interp_sect);
	int sect_size = bfd_section_size (exec_bfd, interp_sect);

	buf = xmalloc (sect_size);
	bfd_get_section_contents (exec_bfd, interp_sect, buf, 0, sect_size);
      }
   }

  /* If we didn't find it, use the target auxillary vector.  */
  if (!buf)
    buf = read_program_header (PT_INTERP, NULL, NULL);

  return buf;
}


/* Scan for DYNTAG in .dynamic section of ABFD. If DYNTAG is found 1 is
   returned and the corresponding PTR is set.  */

static int
scan_dyntag (int dyntag, bfd *abfd, CORE_ADDR *ptr)
{
  int arch_size, step, sect_size;
  long dyn_tag;
  CORE_ADDR dyn_ptr, dyn_addr;
  gdb_byte *bufend, *bufstart, *buf;
  Elf32_External_Dyn *x_dynp_32;
  Elf64_External_Dyn *x_dynp_64;
  struct bfd_section *sect;

  if (abfd == NULL)
    return 0;

  if (bfd_get_flavour (abfd) != bfd_target_elf_flavour)
    return 0;

  arch_size = bfd_get_arch_size (abfd);
  if (arch_size == -1)
    return 0;

  /* Find the start address of the .dynamic section.  */
  sect = bfd_get_section_by_name (abfd, ".dynamic");
  if (sect == NULL)
    return 0;
  dyn_addr = bfd_section_vma (abfd, sect);

  /* Read in .dynamic from the BFD.  We will get the actual value
     from memory later.  */
  sect_size = bfd_section_size (abfd, sect);
  buf = bufstart = alloca (sect_size);
  if (!bfd_get_section_contents (abfd, sect,
				 buf, 0, sect_size))
    return 0;

  /* Iterate over BUF and scan for DYNTAG.  If found, set PTR and return.  */
  step = (arch_size == 32) ? sizeof (Elf32_External_Dyn)
			   : sizeof (Elf64_External_Dyn);
  for (bufend = buf + sect_size;
       buf < bufend;
       buf += step)
  {
    if (arch_size == 32)
      {
	x_dynp_32 = (Elf32_External_Dyn *) buf;
	dyn_tag = bfd_h_get_32 (abfd, (bfd_byte *) x_dynp_32->d_tag);
	dyn_ptr = bfd_h_get_32 (abfd, (bfd_byte *) x_dynp_32->d_un.d_ptr);
      }
    else
      {
	x_dynp_64 = (Elf64_External_Dyn *) buf;
	dyn_tag = bfd_h_get_64 (abfd, (bfd_byte *) x_dynp_64->d_tag);
	dyn_ptr = bfd_h_get_64 (abfd, (bfd_byte *) x_dynp_64->d_un.d_ptr);
      }
     if (dyn_tag == DT_NULL)
       return 0;
     if (dyn_tag == dyntag)
       {
	 /* If requested, try to read the runtime value of this .dynamic
	    entry.  */
	 if (ptr)
	   {
	     struct type *ptr_type;
	     gdb_byte ptr_buf[8];
	     CORE_ADDR ptr_addr;

	     ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;
	     ptr_addr = dyn_addr + (buf - bufstart) + arch_size / 8;
	     if (target_read_memory (ptr_addr, ptr_buf, arch_size / 8) == 0)
	       dyn_ptr = extract_typed_address (ptr_buf, ptr_type);
	     *ptr = dyn_ptr;
	   }
	 return 1;
       }
  }

  return 0;
}

/* Scan for DYNTAG in .dynamic section of the target's main executable,
   found by consulting the OS auxillary vector.  If DYNTAG is found 1 is
   returned and the corresponding PTR is set.  */

static int
scan_dyntag_auxv (int dyntag, CORE_ADDR *ptr)
{
  enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  int sect_size, arch_size, step;
  long dyn_tag;
  CORE_ADDR dyn_ptr;
  gdb_byte *bufend, *bufstart, *buf;

  /* Read in .dynamic section.  */
  buf = bufstart = read_program_header (PT_DYNAMIC, &sect_size, &arch_size);
  if (!buf)
    return 0;

  /* Iterate over BUF and scan for DYNTAG.  If found, set PTR and return.  */
  step = (arch_size == 32) ? sizeof (Elf32_External_Dyn)
			   : sizeof (Elf64_External_Dyn);
  for (bufend = buf + sect_size;
       buf < bufend;
       buf += step)
  {
    if (arch_size == 32)
      {
	Elf32_External_Dyn *dynp = (Elf32_External_Dyn *) buf;
	dyn_tag = extract_unsigned_integer ((gdb_byte *) dynp->d_tag,
					    4, byte_order);
	dyn_ptr = extract_unsigned_integer ((gdb_byte *) dynp->d_un.d_ptr,
					    4, byte_order);
      }
    else
      {
	Elf64_External_Dyn *dynp = (Elf64_External_Dyn *) buf;
	dyn_tag = extract_unsigned_integer ((gdb_byte *) dynp->d_tag,
					    8, byte_order);
	dyn_ptr = extract_unsigned_integer ((gdb_byte *) dynp->d_un.d_ptr,
					    8, byte_order);
      }
    if (dyn_tag == DT_NULL)
      break;

    if (dyn_tag == dyntag)
      {
	if (ptr)
	  *ptr = dyn_ptr;

	xfree (bufstart);
	return 1;
      }
  }

  xfree (bufstart);
  return 0;
}


/*

   LOCAL FUNCTION

   elf_locate_base -- locate the base address of dynamic linker structs
   for SVR4 elf targets.

   SYNOPSIS

   CORE_ADDR elf_locate_base (void)

   DESCRIPTION

   For SVR4 elf targets the address of the dynamic linker's runtime
   structure is contained within the dynamic info section in the
   executable file.  The dynamic section is also mapped into the
   inferior address space.  Because the runtime loader fills in the
   real address before starting the inferior, we have to read in the
   dynamic info section from the inferior address space.
   If there are any errors while trying to find the address, we
   silently return 0, otherwise the found address is returned.

 */

static CORE_ADDR
elf_locate_base (void)
{
  struct minimal_symbol *msymbol;
  CORE_ADDR dyn_ptr;

  /* Look for DT_MIPS_RLD_MAP first.  MIPS executables use this
     instead of DT_DEBUG, although they sometimes contain an unused
     DT_DEBUG.  */
  if (scan_dyntag (DT_MIPS_RLD_MAP, exec_bfd, &dyn_ptr)
      || scan_dyntag_auxv (DT_MIPS_RLD_MAP, &dyn_ptr))
    {
      struct type *ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;
      gdb_byte *pbuf;
      int pbuf_size = TYPE_LENGTH (ptr_type);
      pbuf = alloca (pbuf_size);
      /* DT_MIPS_RLD_MAP contains a pointer to the address
	 of the dynamic link structure.  */
      if (target_read_memory (dyn_ptr, pbuf, pbuf_size))
	return 0;
      return extract_typed_address (pbuf, ptr_type);
    }

  /* Find DT_DEBUG.  */
  if (scan_dyntag (DT_DEBUG, exec_bfd, &dyn_ptr)
      || scan_dyntag_auxv (DT_DEBUG, &dyn_ptr))
    return dyn_ptr;

  /* This may be a static executable.  Look for the symbol
     conventionally named _r_debug, as a last resort.  */
  msymbol = lookup_minimal_symbol ("_r_debug", NULL, symfile_objfile);
  if (msymbol != NULL)
    return SYMBOL_VALUE_ADDRESS (msymbol);

  /* DT_DEBUG entry not found.  */
  return 0;
}

/*

   LOCAL FUNCTION

   locate_base -- locate the base address of dynamic linker structs

   SYNOPSIS

   CORE_ADDR locate_base (struct svr4_info *)

   DESCRIPTION

   For both the SunOS and SVR4 shared library implementations, if the
   inferior executable has been linked dynamically, there is a single
   address somewhere in the inferior's data space which is the key to
   locating all of the dynamic linker's runtime structures.  This
   address is the value of the debug base symbol.  The job of this
   function is to find and return that address, or to return 0 if there
   is no such address (the executable is statically linked for example).

   For SunOS, the job is almost trivial, since the dynamic linker and
   all of it's structures are statically linked to the executable at
   link time.  Thus the symbol for the address we are looking for has
   already been added to the minimal symbol table for the executable's
   objfile at the time the symbol file's symbols were read, and all we
   have to do is look it up there.  Note that we explicitly do NOT want
   to find the copies in the shared library.

   The SVR4 version is a bit more complicated because the address
   is contained somewhere in the dynamic info section.  We have to go
   to a lot more work to discover the address of the debug base symbol.
   Because of this complexity, we cache the value we find and return that
   value on subsequent invocations.  Note there is no copy in the
   executable symbol tables.

 */

static CORE_ADDR
locate_base (struct svr4_info *info)
{
  /* Check to see if we have a currently valid address, and if so, avoid
     doing all this work again and just return the cached address.  If
     we have no cached address, try to locate it in the dynamic info
     section for ELF executables.  There's no point in doing any of this
     though if we don't have some link map offsets to work with.  */

  if (info->debug_base == 0 && svr4_have_link_map_offsets ())
    info->debug_base = elf_locate_base ();
  return info->debug_base;
}

/* Find the first element in the inferior's dynamic link map, and
   return its address in the inferior.

   FIXME: Perhaps we should validate the info somehow, perhaps by
   checking r_version for a known version number, or r_state for
   RT_CONSISTENT.  */

static CORE_ADDR
solib_svr4_r_map (struct svr4_info *info)
{
  struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();
  struct type *ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;

  return read_memory_typed_address (info->debug_base + lmo->r_map_offset,
				    ptr_type);
}

/* Find r_brk from the inferior's debug base.  */

static CORE_ADDR
solib_svr4_r_brk (struct svr4_info *info)
{
  struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();
  struct type *ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;

  return read_memory_typed_address (info->debug_base + lmo->r_brk_offset,
				    ptr_type);
}

/* Find the link map for the dynamic linker (if it is not in the
   normal list of loaded shared objects).  */

static CORE_ADDR
solib_svr4_r_ldsomap (struct svr4_info *info)
{
  struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();
  struct type *ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;
  enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch);
  ULONGEST version;

  /* Check version, and return zero if `struct r_debug' doesn't have
     the r_ldsomap member.  */
  version
    = read_memory_unsigned_integer (info->debug_base + lmo->r_version_offset,
				    lmo->r_version_size, byte_order);
  if (version < 2 || lmo->r_ldsomap_offset == -1)
    return 0;

  return read_memory_typed_address (info->debug_base + lmo->r_ldsomap_offset,
				    ptr_type);
}

/*

  LOCAL FUNCTION

  open_symbol_file_object

  SYNOPSIS

  void open_symbol_file_object (void *from_tty)

  DESCRIPTION

  If no open symbol file, attempt to locate and open the main symbol
  file.  On SVR4 systems, this is the first link map entry.  If its
  name is here, we can open it.  Useful when attaching to a process
  without first loading its symbol file.

  If FROM_TTYP dereferences to a non-zero integer, allow messages to
  be printed.  This parameter is a pointer rather than an int because
  open_symbol_file_object() is called via catch_errors() and
  catch_errors() requires a pointer argument. */

static int
open_symbol_file_object (void *from_ttyp)
{
  CORE_ADDR lm, l_name;
  char *filename;
  int errcode;
  int from_tty = *(int *)from_ttyp;
  struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();
  struct type *ptr_type = builtin_type (target_gdbarch)->builtin_data_ptr;
  int l_name_size = TYPE_LENGTH (ptr_type);
  gdb_byte *l_name_buf = xmalloc (l_name_size);
  struct cleanup *cleanups = make_cleanup (xfree, l_name_buf);
  struct svr4_info *info = get_svr4_info (PIDGET (inferior_ptid));

  if (symfile_objfile)
    if (!query (_("Attempt to reload symbols from process? ")))
      return 0;

  /* Always locate the debug struct, in case it has moved.  */
  info->debug_base = 0;
  if (locate_base (info) == 0)
    return 0;	/* failed somehow... */

  /* First link map member should be the executable.  */
  lm = solib_svr4_r_map (info);
  if (lm == 0)
    return 0;	/* failed somehow... */

  /* Read address of name from target memory to GDB.  */
  read_memory (lm + lmo->l_name_offset, l_name_buf, l_name_size);

  /* Convert the address to host format.  */
  l_name = extract_typed_address (l_name_buf, ptr_type);

  /* Free l_name_buf.  */
  do_cleanups (cleanups);

  if (l_name == 0)
    return 0;		/* No filename.  */

  /* Now fetch the filename from target memory.  */
  target_read_string (l_name, &filename, SO_NAME_MAX_PATH_SIZE - 1, &errcode);
  make_cleanup (xfree, filename);

  if (errcode)
    {
      warning (_("failed to read exec filename from attached file: %s"),
	       safe_strerror (errcode));
      return 0;
    }

  /* Have a pathname: read the symbol file.  */
  symbol_file_add_main (filename, from_tty);

  return 1;
}

/* If no shared library information is available from the dynamic
   linker, build a fallback list from other sources.  */

static struct so_list *
svr4_default_sos (void)
{
  struct inferior *inf = current_inferior ();
  struct svr4_info *info = get_svr4_info (inf->pid);

  struct so_list *head = NULL;
  struct so_list **link_ptr = &head;

  if (info->debug_loader_offset_p)
    {
      struct so_list *new = XZALLOC (struct so_list);

      new->lm_info = xmalloc (sizeof (struct lm_info));

      /* Nothing will ever check the cached copy of the link
	 map if we set l_addr.  */
      new->lm_info->l_addr = info->debug_loader_offset;
      new->lm_info->lm_addr = 0;
      new->lm_info->lm = NULL;

      strncpy (new->so_name, info->debug_loader_name,
	       SO_NAME_MAX_PATH_SIZE - 1);
      new->so_name[SO_NAME_MAX_PATH_SIZE - 1] = '\0';
      strcpy (new->so_original_name, new->so_name);

      *link_ptr = new;
      link_ptr = &new->next;
    }

  return head;
}

/* LOCAL FUNCTION

   current_sos -- build a list of currently loaded shared objects

   SYNOPSIS

   struct so_list *current_sos ()

   DESCRIPTION

   Build a list of `struct so_list' objects describing the shared
   objects currently loaded in the inferior.  This list does not
   include an entry for the main executable file.

   Note that we only gather information directly available from the
   inferior --- we don't examine any of the shared library files
   themselves.  The declaration of `struct so_list' says which fields
   we provide values for.  */

static struct so_list *
svr4_current_sos (void)
{
  CORE_ADDR lm;
  struct so_list *head = 0;
  struct so_list **link_ptr = &head;
  CORE_ADDR ldsomap = 0;
  struct inferior *inf;
  struct svr4_info *info;

  if (ptid_equal (inferior_ptid, null_ptid))
    return NULL;

  inf = current_inferior ();
  info = get_svr4_info (inf->pid);

  /* Always locate the debug struct, in case it has moved.  */
  info->debug_base = 0;
  locate_base (info);

  /* If we can't find the dynamic linker's base structure, this
     must not be a dynamically linked executable.  Hmm.  */
  if (! info->debug_base)
    return svr4_default_sos ();

  /* Walk the inferior's link map list, and build our list of
     `struct so_list' nodes.  */
  lm = solib_svr4_r_map (info);

  while (lm)
    {
      struct link_map_offsets *lmo = svr4_fetch_link_map_offsets ();
      struct so_list *new = XZALLOC (struct so_list);
      struct cleanup *old_chain = make_cleanup (xfree, new);

      new->lm_info = xmalloc (sizeof (struct lm_info));
      make_cleanup (xfree, new->lm_info);

      new->lm_info->l_addr = (CORE_ADDR)-1;
      new->lm_info->lm_addr = lm;
      new->lm_info->lm = xzalloc (lmo->link_map_size);
      make_cleanup (xfree, new->lm_info->lm);

      read_memory (lm, new->lm_info->lm, lmo->link_map_size);

      lm = LM_NEXT (new);

      /* For SVR4 versions, the first entry in the link map is for the
         inferior executable, so we must ignore it.  For some versions of
         SVR4, it has no name.  For others (Solaris 2.3 for example), it
         does have a name, so we can no longer use a missing name to
         decide when to ignore it. */
      if (IGNORE_FIRST_LINK_MAP_ENTRY (new) && ldsomap == 0)
	{
	  info->main_lm_addr = new->lm_info->lm_addr;
	  free_so (new);
	}
      else
	{
	  int errcode;
	  char *buffer;

	  /* Extract this shared object's name.  */
	  target_read_string (LM_NAME (new), &buffer,
			      SO_NAME_MAX_PATH_SIZE - 1, &errcode);
	  if (errcode != 0)
	    warning (_("Can't read pathname for load map: %s."),
		     safe_strerror (errcode));
	  else
	    {
	      strncpy (new->so_name, buffer, SO_NAME_MAX_PATH_SIZE - 1);
	      new->so_name[SO_NAME_MAX_PATH_SIZE - 1] = '\0';
	      strcpy (new->so_original_name, new->so_name);
	    }
	  xfree (buffer);

	  /* If this entry has no name, or its name matches the name
	     for the main executable, don't include it in the list.  */
	  if (! new->so_name[0]
	      || match_main (new->so_name))
	    free_so (new);
	  else
	    {
	      new->next = 0;
	      *link_ptr = new;
	      link_ptr = &new->next;
	    }
	}

      /* On Solaris, the dynamic linker is not in the normal list of
	 shared objects, so make sure we pick it up too.  Having
	 symbol information for the dynamic linker is quite crucial
	 for skipping dynamic linker resolver code.  */
      if (lm == 0 && ldsomap == 0)
	lm = ldsomap = solib_svr4_r_ldsomap (info);

      discard_cleanups (old_chain);
    }

  if (head == NULL)
    return svr4_default_sos ();

  return head;
}

/* Get the address of the link_map for a given OBJFILE.  */

CORE_ADDR
svr4_fetch_objfile_link_map (struct objfile *objfile)
{
  struct so_list *so;
  struct svr4_info *info = get_svr4_info (PIDGET (inferior_ptid));

  /* Cause svr4_current_sos() to be run if it hasn't been already.  */
  if (info->main_lm_addr == 0)
    solib_add (NULL, 0, &current_target, auto_solib_add);

  /* svr4_current_sos() will set main_lm_addr for the main executable.  */
  if (objfile == symfile_objfile)
    return info->main_lm_addr;

  /* The other link map addresses may be found by examining the list
     of shared libraries.  */
  for (so = master_so_list (); so; so = so->next)
    if (so->objfile == objfile)
      return so->lm_info->lm_addr;

  /* Not found!  */
  return 0;
}

/* On some systems, the only way to recognize the link map entry for
   the main executable file is by looking at its name.  Return
   non-zero iff SONAME matches one of the known main executable names.  */

static int
match_main (char *soname)
{
  char **mainp;

  for (mainp = main_name_list; *mainp != NULL; mainp++)
    {
      if (strcmp (soname, *mainp) == 0)
	return (1);
    }

  return (0);
}

/* Return 1 if PC lies in the dynamic symbol resolution code of the
   SVR4 run time loader.  */
static CORE_ADDR interp_text_sect_low;
static CORE_ADDR interp_text_sect_high;
static CORE_ADDR interp_plt_sect_low;
static CORE_ADDR interp_plt_sect_high;

int
svr4_in_dynsym_resolve_code (CORE_ADDR pc)
{
  return ((pc >= interp_text_sect_low && pc < interp_text_sect_high)
	  || (pc >= interp_plt_sect_low && pc < interp_plt_sect_high)
	  || in_plt_section (pc, NULL));
}

/* Given an executable's ABFD and target, compute the entry-point
   address.  */

static CORE_ADDR
exec_entry_point (struct bfd *abfd, struct target_ops *targ)
{
  /* KevinB wrote ... for most targets, the address returned by
     bfd_get_start_address() is the entry point for the start
     function.  But, for some targets, bfd_get_start_address() returns
     the address of a function descriptor from which the entry point
     address may be extracted.  This address is extracted by
     gdbarch_convert_from_func_ptr_addr().  The method
     gdbarch_convert_from_func_ptr_addr() is the merely the identify
     function for targets which don't use function descriptors.  */
  return gdbarch_convert_from_func_ptr_addr (target_gdbarch,
					     bfd_get_start_address (abfd),
					     targ);
}

/*

   LOCAL FUNCTION

   enable_break -- arrange for dynamic linker to hit breakpoint

   SYNOPSIS

   int enable_break (void)

   DESCRIPTION

   Both the SunOS and the SVR4 dynamic linkers have, as part of their
   debugger interface, support for arranging for the inferior to hit
   a breakpoint after mapping in the shared libraries.  This function
   enables that breakpoint.

   For SunOS, there is a special flag location (in_debugger) which we
   set to 1.  When the dynamic linker sees this flag set, it will set
   a breakpoint at a location known only to itself, after saving the
   original contents of that place and the breakpoint address itself,
   in it's own internal structures.  When we resume the inferior, it
   will eventually take a SIGTRAP when it runs into the breakpoint.
   We handle this (in a different place) by restoring the contents of
   the breakpointed location (which is only known after it stops),
   chasing around to locate the shared libraries that have been
   loaded, then resuming.

   For SVR4, the debugger interface structure contains a member (r_brk)
   which is statically initialized at the time the shared library is
   built, to the offset of a function (_r_debug_state) which is guaran-
   teed to be called once before mapping in a library, and again when
   the mapping is complete.  At the time we are examining this member,
   it contains only the unrelocated offset of the function, so we have
   to do our own relocation.  Later, when the dynamic linker actually
   runs, it relocates r_brk to be the actual address of _r_debug_state().

   The debugger interface structure also contains an enumeration which
   is set to either RT_ADD or RT_DELETE prior to changing the mapping,
   depending upon whether or not the library is being mapped or unmapped,
   and then set to RT_CONSISTENT after the library is mapped/unmapped.
 */

static int
enable_break (struct svr4_info *info)
{
  struct minimal_symbol *msymbol;
  char **bkpt_namep;
  asection *interp_sect;
  gdb_byte *interp_name;
  CORE_ADDR sym_addr;
  struct inferior *inf = current_inferior ();

  /* First, remove all the solib event breakpoints.  Their addresses
     may have changed since the last time we ran the program.  */
  remove_solib_event_breakpoints ();

  interp_text_sect_low = interp_text_sect_high = 0;
  interp_plt_sect_low = interp_plt_sect_high = 0;

  /* If we already have a shared library list in the target, and
     r_debug contains r_brk, set the breakpoint there - this should
     mean r_brk has already been relocated.  Assume the dynamic linker
     is the object containing r_brk.  */

  solib_add (NULL, 0, &current_target, auto_solib_add);
  sym_addr = 0;
  if (info->debug_base && solib_svr4_r_map (info) != 0)
    sym_addr = solib_svr4_r_brk (info);

  if (sym_addr != 0)
    {
      struct obj_section *os;

      sym_addr = gdbarch_addr_bits_remove
	(target_gdbarch, gdbarch_convert_from_func_ptr_addr (target_gdbarch,
							      sym_addr,
							      &current_target));

      os = find_pc_section (sym_addr);
      if (os != NULL)
	{
	  /* Record the relocated start and end address of the dynamic linker
	     text and plt section for svr4_in_dynsym_resolve_code.  */
	  bfd *tmp_bfd;
	  CORE_ADDR load_addr;

	  tmp_bfd = os->objfile->obfd;
	  load_addr = ANOFFSET (os->objfile->section_offsets,
				os->objfile->sect_index_text);

	  interp_sect = bfd_get_section_by_name (tmp_bfd, ".text");
	  if (interp_sect)
	    {
	      interp_text_sect_low =
		bfd_section_vma (tmp_bfd, interp_sect) + load_addr;
	      interp_text_sect_high =
		interp_text_sect_low + bfd_section_size (tmp_bfd, interp_sect);
	    }
	  interp_sect = bfd_get_section_by_name (tmp_bfd, ".plt");
	  if (interp_sect)
	    {
	      interp_plt_sect_low =
		bfd_section_vma (tmp_bfd, interp_sect) + load_addr;
	      interp_plt_sect_high =
		interp_plt_sect_low + bfd_section_size (tmp_bfd, interp_sect);
	    }

	  create_solib_event_breakpoint (target_gdbarch, sym_addr);
	  return 1;
	}
    }

  /* Find the program interpreter; if not found, warn the user and drop
     into the old breakpoint at symbol code.  */
  interp_name = find_program_interpreter ();
  if (interp_name)
    {
      CORE_ADDR load_addr = 0;
      int load_addr_found = 0;
      int loader_found_in_list = 0;
      struct so_list *so;
      bfd *tmp_bfd = NULL;
      struct target_ops *tmp_bfd_target;
      volatile struct gdb_exception ex;

      sym_addr = 0;

      /* Now we need to figure out where the dynamic linker was
         loaded so that we can load its symbols and place a breakpoint
         in the dynamic linker itself.

         This address is stored on the stack.  However, I've been unable
         to find any magic formula to find it for Solaris (appears to
         be trivial on GNU/Linux).  Therefore, we have to try an alternate
         mechanism to find the dynamic linker's base address.  */

      TRY_CATCH (ex, RETURN_MASK_ALL)
        {
	  tmp_bfd = solib_bfd_open (interp_name);
	}
      if (tmp_bfd == NULL)
	goto bkpt_at_symbol;

      /* Now convert the TMP_BFD into a target.  That way target, as
         well as BFD operations can be used.  Note that closing the
         target will also close the underlying bfd.  */
      tmp_bfd_target = target_bfd_reopen (tmp_bfd);

      /* On a running target, we can get the dynamic linker's base
         address from the shared library table.  */
      so = master_so_list ();
      while (so)
	{
	  if (svr4_same_1 (interp_name, so->so_original_name))
	    {
	      load_addr_found = 1;
	      loader_found_in_list = 1;
	      load_addr = LM_ADDR_CHECK (so, tmp_bfd);
	      break;
	    }
	  so = so->next;
	}

      /* If we were not able to find the base address of the loader
         from our so_list, then try using the AT_BASE auxilliary entry.  */
      if (!load_addr_found)
        if (target_auxv_search (&current_target, AT_BASE, &load_addr) > 0)
          load_addr_found = 1;

      /* Otherwise we find the dynamic linker's base address by examining
	 the current pc (which should point at the entry point for the
	 dynamic linker) and subtracting the offset of the entry point.

         This is more fragile than the previous approaches, but is a good
         fallback method because it has actually been working well in
         most cases.  */
      if (!load_addr_found)
	{
	  struct regcache *regcache
	    = get_thread_arch_regcache (inferior_ptid, target_gdbarch);
	  load_addr = (regcache_read_pc (regcache)
		       - exec_entry_point (tmp_bfd, tmp_bfd_target));
	}

      if (!loader_found_in_list)
	{
	  info->debug_loader_name = xstrdup (interp_name);
	  info->debug_loader_offset_p = 1;
	  info->debug_loader_offset = load_addr;
	  solib_add (NULL, 0, &current_target, auto_solib_add);
	}

      /* Record the relocated start and end address of the dynamic linker
         text and plt section for svr4_in_dynsym_resolve_code.  */
      interp_sect = bfd_get_section_by_name (tmp_bfd, ".text");
      if (interp_sect)
	{
	  interp_text_sect_low =
	    bfd_section_vma (tmp_bfd, interp_sect) + load_addr;
	  interp_text_sect_high =
	    interp_text_sect_low + bfd_section_size (tmp_bfd, interp_sect);
	}
      interp_sect = bfd_get_section_by_name (tmp_bfd, ".plt");
      if (interp_sect)
	{
	  interp_plt_sect_low =
	    bfd_section_vma (tmp_bfd, interp_sect) + load_addr;
	  interp_plt_sect_high =
	    interp_plt_sect_low + bfd_section_size (tmp_bfd, interp_sect);
	}

      /* Now try to set a breakpoint in the dynamic linker.  */
      for (bkpt_namep = solib_break_names; *bkpt_namep != NULL; bkpt_namep++)
	{
	  sym_addr = bfd_lookup_symbol (tmp_bfd, *bkpt_namep);
	  if (sym_addr != 0)
	    break;
	}

      if (sym_addr != 0)
	/* Convert 'sym_addr' from a function pointer to an address.
	   Because we pass tmp_bfd_target instead of the current
	   target, this will always produce an unrelocated value.  */
	sym_addr = gdbarch_convert_from_func_ptr_addr (target_gdbarch,
						       sym_addr,
						       tmp_bfd_target);

      /* We're done with both the temporary bfd and target.  Remember,
         closing the target closes the underlying bfd.  */
      target_close (tmp_bfd_target, 0);

      if (sym_addr != 0)
	{
	  create_solib_event_breakpoint (target_gdbarch, load_addr + sym_addr);
	  xfree (interp_name);
	  return 1;
	}

      /* For whatever reason we couldn't set a breakpoint in the dynamic
         linker.  Warn and drop into the old code.  */
    bkpt_at_symbol:
      xfree (interp_name);
      warning (_("Unable to find dynamic linker breakpoint function.\n"
               "GDB will be unable to debug shared library initializers\n"
               "and track explicitly loaded dynamic code."));
    }

  /* Scan through the lists of symbols, trying to look up the symbol and
     set a breakpoint there.  Terminate loop when we/if we succeed.  */

  for (bkpt_namep = solib_break_names; *bkpt_namep != NULL; bkpt_namep++)
    {
      msymbol = lookup_minimal_symbol (*bkpt_namep, NULL, symfile_objfile);
      if ((msymbol != NULL) && (SYMBOL_VALUE_ADDRESS (msymbol) != 0))
	{
	  create_solib_event_breakpoint (target_gdbarch,
					 SYMBOL_VALUE_ADDRESS (msymbol));
	  return 1;
	}
    }

  for (bkpt_namep = bkpt_names; *bkpt_namep != NULL; bkpt_namep++)
    {
      msymbol = lookup_minimal_symbol (*bkpt_namep, NULL, symfile_objfile);
      if ((msymbol != NULL) && (SYMBOL_VALUE_ADDRESS (msymbol) != 0))
	{
	  create_solib_event_breakpoint (target_gdbarch,
					 SYMBOL_VALUE_ADDRESS (msymbol));
	  return 1;
	}
    }
  return 0;
}

/*

   LOCAL FUNCTION

   special_symbol_handling -- additional shared library symbol handling

   SYNOPSIS

   void special_symbol_handling ()

   DESCRIPTION

   Once the symbols from a shared object have been loaded in the usual
   way, we are called to do any system specific symbol handling that 
   is needed.

   For SunOS4, this consisted of grunging around in the dynamic
   linkers structures to find symbol definitions for "common" symbols
   and adding them to the minimal symbol table for the runtime common
   objfile.

   However, for SVR4, there's nothing to do.

 */

static void
svr4_special_symbol_handling (void)
{
}

/* Relocate the main executable.  This function should be called upon
   stopping the inferior process at the entry point to the program. 
   The entry point from BFD is compared to the PC and if they are
   different, the main executable is relocated by the proper amount. 
   
   As written it will only attempt to relocate executables which
   lack interpreter sections.  It seems likely that only dynamic
   linker executables will get relocated, though it should work
   properly for a position-independent static executable as well.  */

static void
svr4_relocate_main_executable (void)
{
  asection *interp_sect;
  struct regcache *regcache
    = get_thread_arch_regcache (inferior_ptid, target_gdbarch);
  CORE_ADDR pc = regcache_read_pc (regcache);

  /* Decide if the objfile needs to be relocated.  As indicated above,
     we will only be here when execution is stopped at the beginning
     of the program.  Relocation is necessary if the address at which
     we are presently stopped differs from the start address stored in
     the executable AND there's no interpreter section.  The condition
     regarding the interpreter section is very important because if
     there *is* an interpreter section, execution will begin there
     instead.  When there is an interpreter section, the start address
     is (presumably) used by the interpreter at some point to start
     execution of the program.

     If there is an interpreter, it is normal for it to be set to an
     arbitrary address at the outset.  The job of finding it is
     handled in enable_break().

     So, to summarize, relocations are necessary when there is no
     interpreter section and the start address obtained from the
     executable is different from the address at which GDB is
     currently stopped.
     
     [ The astute reader will note that we also test to make sure that
       the executable in question has the DYNAMIC flag set.  It is my
       opinion that this test is unnecessary (undesirable even).  It
       was added to avoid inadvertent relocation of an executable
       whose e_type member in the ELF header is not ET_DYN.  There may
       be a time in the future when it is desirable to do relocations
       on other types of files as well in which case this condition
       should either be removed or modified to accomodate the new file
       type.  (E.g, an ET_EXEC executable which has been built to be
       position-independent could safely be relocated by the OS if
       desired.  It is true that this violates the ABI, but the ABI
       has been known to be bent from time to time.)  - Kevin, Nov 2000. ]
     */

  interp_sect = bfd_get_section_by_name (exec_bfd, ".interp");
  if (interp_sect == NULL 
      && (bfd_get_file_flags (exec_bfd) & DYNAMIC) != 0
      && (exec_entry_point (exec_bfd, &exec_ops) != pc))
    {
      struct cleanup *old_chain;
      struct section_offsets *new_offsets;
      int i, changed;
      CORE_ADDR displacement;
      
      /* It is necessary to relocate the objfile.  The amount to
	 relocate by is simply the address at which we are stopped
	 minus the starting address from the executable.

	 We relocate all of the sections by the same amount.  This
	 behavior is mandated by recent editions of the System V ABI. 
	 According to the System V Application Binary Interface,
	 Edition 4.1, page 5-5:

	   ...  Though the system chooses virtual addresses for
	   individual processes, it maintains the segments' relative
	   positions.  Because position-independent code uses relative
	   addressesing between segments, the difference between
	   virtual addresses in memory must match the difference
	   between virtual addresses in the file.  The difference
	   between the virtual address of any segment in memory and
	   the corresponding virtual address in the file is thus a
	   single constant value for any one executable or shared
	   object in a given process.  This difference is the base
	   address.  One use of the base address is to relocate the
	   memory image of the program during dynamic linking.

	 The same language also appears in Edition 4.0 of the System V
	 ABI and is left unspecified in some of the earlier editions.  */

      displacement = pc - exec_entry_point (exec_bfd, &exec_ops);
      changed = 0;

      new_offsets = xcalloc (symfile_objfile->num_sections,
			     sizeof (struct section_offsets));
      old_chain = make_cleanup (xfree, new_offsets);

      for (i = 0; i < symfile_objfile->num_sections; i++)
	{
	  if (displacement != ANOFFSET (symfile_objfile->section_offsets, i))
	    changed = 1;
	  new_offsets->offsets[i] = displacement;
	}

      if (changed)
	objfile_relocate (symfile_objfile, new_offsets);

      do_cleanups (old_chain);
    }
}

/*

   GLOBAL FUNCTION

   svr4_solib_create_inferior_hook -- shared library startup support

   SYNOPSIS

   void svr4_solib_create_inferior_hook ()

   DESCRIPTION

   When gdb starts up the inferior, it nurses it along (through the
   shell) until it is ready to execute it's first instruction.  At this
   point, this function gets called via expansion of the macro
   SOLIB_CREATE_INFERIOR_HOOK.

   For SunOS executables, this first instruction is typically the
   one at "_start", or a similar text label, regardless of whether
   the executable is statically or dynamically linked.  The runtime
   startup code takes care of dynamically linking in any shared
   libraries, once gdb allows the inferior to continue.

   For SVR4 executables, this first instruction is either the first
   instruction in the dynamic linker (for dynamically linked
   executables) or the instruction at "start" for statically linked
   executables.  For dynamically linked executables, the system
   first exec's /lib/libc.so.N, which contains the dynamic linker,
   and starts it running.  The dynamic linker maps in any needed
   shared libraries, maps in the actual user executable, and then
   jumps to "start" in the user executable.

   For both SunOS shared libraries, and SVR4 shared libraries, we
   can arrange to cooperate with the dynamic linker to discover the
   names of shared libraries that are dynamically linked, and the
   base addresses to which they are linked.

   This function is responsible for discovering those names and
   addresses, and saving sufficient information about them to allow
   their symbols to be read at a later time.

   FIXME

   Between enable_break() and disable_break(), this code does not
   properly handle hitting breakpoints which the user might have
   set in the startup code or in the dynamic linker itself.  Proper
   handling will probably have to wait until the implementation is
   changed to use the "breakpoint handler function" method.

   Also, what if child has exit()ed?  Must exit loop somehow.
 */

static void
svr4_solib_create_inferior_hook (void)
{
  struct inferior *inf;
  struct thread_info *tp;
  struct svr4_info *info;

  info = get_svr4_info (PIDGET (inferior_ptid));

  /* Relocate the main executable if necessary.  */
  svr4_relocate_main_executable ();

  if (!svr4_have_link_map_offsets ())
    return;

  if (!enable_break (info))
    return;

#if defined(_SCO_DS)
  /* SCO needs the loop below, other systems should be using the
     special shared library breakpoints and the shared library breakpoint
     service routine.

     Now run the target.  It will eventually hit the breakpoint, at
     which point all of the libraries will have been mapped in and we
     can go groveling around in the dynamic linker structures to find
     out what we need to know about them. */

  inf = current_inferior ();
  tp = inferior_thread ();

  clear_proceed_status ();
  inf->stop_soon = STOP_QUIETLY;
  tp->stop_signal = TARGET_SIGNAL_0;
  do
    {
      target_resume (pid_to_ptid (-1), 0, tp->stop_signal);
      wait_for_inferior (0);
    }
  while (tp->stop_signal != TARGET_SIGNAL_TRAP);
  inf->stop_soon = NO_STOP_QUIETLY;
#endif /* defined(_SCO_DS) */
}

static void
svr4_clear_solib (void)
{
  remove_svr4_info (PIDGET (inferior_ptid));
}

static void
svr4_free_so (struct so_list *so)
{
  xfree (so->lm_info->lm);
  xfree (so->lm_info);
}


/* Clear any bits of ADDR that wouldn't fit in a target-format
   data pointer.  "Data pointer" here refers to whatever sort of
   address the dynamic linker uses to manage its sections.  At the
   moment, we don't support shared libraries on any processors where
   code and data pointers are different sizes.

   This isn't really the right solution.  What we really need here is
   a way to do arithmetic on CORE_ADDR values that respects the
   natural pointer/address correspondence.  (For example, on the MIPS,
   converting a 32-bit pointer to a 64-bit CORE_ADDR requires you to
   sign-extend the value.  There, simply truncating the bits above
   gdbarch_ptr_bit, as we do below, is no good.)  This should probably
   be a new gdbarch method or something.  */
static CORE_ADDR
svr4_truncate_ptr (CORE_ADDR addr)
{
  if (gdbarch_ptr_bit (target_gdbarch) == sizeof (CORE_ADDR) * 8)
    /* We don't need to truncate anything, and the bit twiddling below
       will fail due to overflow problems.  */
    return addr;
  else
    return addr & (((CORE_ADDR) 1 << gdbarch_ptr_bit (target_gdbarch)) - 1);
}


static void
svr4_relocate_section_addresses (struct so_list *so,
                                 struct target_section *sec)
{
  sec->addr    = svr4_truncate_ptr (sec->addr    + LM_ADDR_CHECK (so,
								  sec->bfd));
  sec->endaddr = svr4_truncate_ptr (sec->endaddr + LM_ADDR_CHECK (so,
								  sec->bfd));
}


/* Architecture-specific operations.  */

/* Per-architecture data key.  */
static struct gdbarch_data *solib_svr4_data;

struct solib_svr4_ops
{
  /* Return a description of the layout of `struct link_map'.  */
  struct link_map_offsets *(*fetch_link_map_offsets)(void);
};

/* Return a default for the architecture-specific operations.  */

static void *
solib_svr4_init (struct obstack *obstack)
{
  struct solib_svr4_ops *ops;

  ops = OBSTACK_ZALLOC (obstack, struct solib_svr4_ops);
  ops->fetch_link_map_offsets = NULL;
  return ops;
}

/* Set the architecture-specific `struct link_map_offsets' fetcher for
   GDBARCH to FLMO.  Also, install SVR4 solib_ops into GDBARCH.  */

void
set_solib_svr4_fetch_link_map_offsets (struct gdbarch *gdbarch,
                                       struct link_map_offsets *(*flmo) (void))
{
  struct solib_svr4_ops *ops = gdbarch_data (gdbarch, solib_svr4_data);

  ops->fetch_link_map_offsets = flmo;

  set_solib_ops (gdbarch, &svr4_so_ops);
}

/* Fetch a link_map_offsets structure using the architecture-specific
   `struct link_map_offsets' fetcher.  */

static struct link_map_offsets *
svr4_fetch_link_map_offsets (void)
{
  struct solib_svr4_ops *ops = gdbarch_data (target_gdbarch, solib_svr4_data);

  gdb_assert (ops->fetch_link_map_offsets);
  return ops->fetch_link_map_offsets ();
}

/* Return 1 if a link map offset fetcher has been defined, 0 otherwise.  */

static int
svr4_have_link_map_offsets (void)
{
  struct solib_svr4_ops *ops = gdbarch_data (target_gdbarch, solib_svr4_data);
  return (ops->fetch_link_map_offsets != NULL);
}


/* Most OS'es that have SVR4-style ELF dynamic libraries define a
   `struct r_debug' and a `struct link_map' that are binary compatible
   with the origional SVR4 implementation.  */

/* Fetch (and possibly build) an appropriate `struct link_map_offsets'
   for an ILP32 SVR4 system.  */
  
struct link_map_offsets *
svr4_ilp32_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      lmo.r_version_offset = 0;
      lmo.r_version_size = 4;
      lmo.r_map_offset = 4;
      lmo.r_brk_offset = 8;
      lmo.r_ldsomap_offset = 20;

      /* Everything we need is in the first 20 bytes.  */
      lmo.link_map_size = 20;
      lmo.l_addr_offset = 0;
      lmo.l_name_offset = 4;
      lmo.l_ld_offset = 8;
      lmo.l_next_offset = 12;
      lmo.l_prev_offset = 16;
    }

  return lmp;
}

/* Fetch (and possibly build) an appropriate `struct link_map_offsets'
   for an LP64 SVR4 system.  */
  
struct link_map_offsets *
svr4_lp64_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      lmo.r_version_offset = 0;
      lmo.r_version_size = 4;
      lmo.r_map_offset = 8;
      lmo.r_brk_offset = 16;
      lmo.r_ldsomap_offset = 40;

      /* Everything we need is in the first 40 bytes.  */
      lmo.link_map_size = 40;
      lmo.l_addr_offset = 0;
      lmo.l_name_offset = 8;
      lmo.l_ld_offset = 16;
      lmo.l_next_offset = 24;
      lmo.l_prev_offset = 32;
    }

  return lmp;
}


struct target_so_ops svr4_so_ops;

/* Lookup global symbol for ELF DSOs linked with -Bsymbolic. Those DSOs have a
   different rule for symbol lookup.  The lookup begins here in the DSO, not in
   the main executable.  */

static struct symbol *
elf_lookup_lib_symbol (const struct objfile *objfile,
		       const char *name,
		       const char *linkage_name,
		       const domain_enum domain)
{
  if (objfile->obfd == NULL
     || scan_dyntag (DT_SYMBOLIC, objfile->obfd, NULL) != 1)
    return NULL;

  return lookup_global_symbol_from_objfile
		(objfile, name, linkage_name, domain);
}

extern initialize_file_ftype _initialize_svr4_solib; /* -Wmissing-prototypes */

void
_initialize_svr4_solib (void)
{
  solib_svr4_data = gdbarch_data_register_pre_init (solib_svr4_init);

  svr4_so_ops.relocate_section_addresses = svr4_relocate_section_addresses;
  svr4_so_ops.free_so = svr4_free_so;
  svr4_so_ops.clear_solib = svr4_clear_solib;
  svr4_so_ops.solib_create_inferior_hook = svr4_solib_create_inferior_hook;
  svr4_so_ops.special_symbol_handling = svr4_special_symbol_handling;
  svr4_so_ops.current_sos = svr4_current_sos;
  svr4_so_ops.open_symbol_file_object = open_symbol_file_object;
  svr4_so_ops.in_dynsym_resolve_code = svr4_in_dynsym_resolve_code;
  svr4_so_ops.bfd_open = solib_bfd_open;
  svr4_so_ops.lookup_lib_global_symbol = elf_lookup_lib_symbol;
  svr4_so_ops.same = svr4_same;

  observer_attach_inferior_exit (solib_svr4_inferior_exit);
}
