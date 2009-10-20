/* Caching code for GDB, the GNU debugger.

   Copyright (C) 1992, 1993, 1995, 1996, 1998, 1999, 2000, 2001, 2003, 2007,
   2008, 2009 Free Software Foundation, Inc.

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
#include "dcache.h"
#include "gdbcmd.h"
#include "gdb_string.h"
#include "gdbcore.h"
#include "target.h"
#include "inferior.h"
#include "splay-tree.h"

/* The data cache could lead to incorrect results because it doesn't
   know about volatile variables, thus making it impossible to debug
   functions which use memory mapped I/O devices.  Set the nocache
   memory region attribute in those cases.

   In general the dcache speeds up performance.  Some speed improvement
   comes from the actual caching mechanism, but the major gain is in
   the reduction of the remote protocol overhead; instead of reading
   or writing a large area of memory in 4 byte requests, the cache
   bundles up the requests into LINE_SIZE chunks, reducing overhead
   significantly.  This is most useful when accessing a large amount
   of data, such as when performing a backtrace.

   The cache is a splay tree along with a linked list for replacement.
   Each block caches a LINE_SIZE area of memory.  Wtihin each line we remember
   the address of the line (which must be a multiple of LINE_SIZE) and the
   actual data block.

   Lines are only allocated as needed, so DCACHE_SIZE really specifies the
   *maximum* number of lines in the cache.

   At present, the cache is write-through rather than writeback: as soon
   as data is written to the cache, it is also immediately written to
   the target.  Therefore, cache lines are never "dirty".  Whether a given
   line is valid or not depends on where it is stored in the dcache_struct;
   there is no per-block valid flag.  */

/* NOTE: Interaction of dcache and memory region attributes

   As there is no requirement that memory region attributes be aligned
   to or be a multiple of the dcache page size, dcache_read_line() and
   dcache_write_line() must break up the page by memory region.  If a
   chunk does not have the cache attribute set, an invalid memory type
   is set, etc., then the chunk is skipped.  Those chunks are handled
   in target_xfer_memory() (or target_xfer_memory_partial()).

   This doesn't occur very often.  The most common occurance is when
   the last bit of the .text segment and the first bit of the .data
   segment fall within the same dcache page with a ro/cacheable memory
   region defined for the .text segment and a rw/non-cacheable memory
   region defined for the .data segment.  */

/* The maximum number of lines stored.  The total size of the cache is
   equal to DCACHE_SIZE times LINE_SIZE.  */
#define DCACHE_SIZE 4096

/* The size of a cache line.  Smaller values reduce the time taken to
   read a single byte and make the cache more granular, but increase
   overhead and reduce the effectiveness of the cache as a prefetcher.  */
#define LINE_SIZE_POWER 6
#define LINE_SIZE (1 << LINE_SIZE_POWER)

/* Each cache block holds LINE_SIZE bytes of data
   starting at a multiple-of-LINE_SIZE address.  */

#define LINE_SIZE_MASK  ((LINE_SIZE - 1))
#define XFORM(x) 	((x) & LINE_SIZE_MASK)
#define MASK(x)         ((x) & ~LINE_SIZE_MASK)

struct dcache_block
{
  struct dcache_block *newer;	/* for LRU and free list */
  CORE_ADDR addr;		/* address of data */
  gdb_byte data[LINE_SIZE];	/* bytes at given address */
  int refs;			/* # hits */
};

struct dcache_struct
{
  splay_tree tree;
  struct dcache_block *oldest;
  struct dcache_block *newest;

  struct dcache_block *freelist;

  /* The number of in-use lines in the cache.  */
  int size;

  /* The ptid of last inferior to use cache or null_ptid.  */
  ptid_t ptid;
};

static struct dcache_block *dcache_hit (DCACHE *dcache, CORE_ADDR addr);

static int dcache_write_line (DCACHE *dcache, struct dcache_block *db);

static int dcache_read_line (DCACHE *dcache, struct dcache_block *db);

static struct dcache_block *dcache_alloc (DCACHE *dcache, CORE_ADDR addr);

static void dcache_info (char *exp, int tty);

void _initialize_dcache (void);

static int dcache_enabled_p = 0; /* OBSOLETE */

static void
show_dcache_enabled_p (struct ui_file *file, int from_tty,
		       struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("Deprecated remotecache flag is %s.\n"), value);
}

static DCACHE *last_cache; /* Used by info dcache */

/* Free all the data cache blocks, thus discarding all cached data.  */

void
dcache_invalidate (DCACHE *dcache)
{
  struct dcache_block *block, *next;

  block = dcache->oldest;

  while (block)
    {
      splay_tree_remove (dcache->tree, (splay_tree_key) block->addr);
      next = block->newer;

      block->newer = dcache->freelist;
      dcache->freelist = block;

      block = next;
    }

  dcache->oldest = NULL;
  dcache->newest = NULL;
  dcache->size = 0;
  dcache->ptid = null_ptid;
}

/* Invalidate the line associated with ADDR.  */

static void
dcache_invalidate_line (DCACHE *dcache, CORE_ADDR addr)
{
  struct dcache_block *db = dcache_hit (dcache, addr);

  if (db)
    {
      splay_tree_remove (dcache->tree, (splay_tree_key) db->addr);
      db->newer = dcache->freelist;
      dcache->freelist = db;
      --dcache->size;
    }
}

/* If addr is present in the dcache, return the address of the block
   containing it.  */

static struct dcache_block *
dcache_hit (DCACHE *dcache, CORE_ADDR addr)
{
  struct dcache_block *db;

  splay_tree_node node = splay_tree_lookup (dcache->tree,
					    (splay_tree_key) MASK (addr));

  if (!node)
    return NULL;

  db = (struct dcache_block *) node->value;
  db->refs++;
  return db;
}

/* Fill a cache line from target memory.  */

static int
dcache_read_line (DCACHE *dcache, struct dcache_block *db)
{
  CORE_ADDR memaddr;
  gdb_byte *myaddr;
  int len;
  int res;
  int reg_len;
  struct mem_region *region;

  len = LINE_SIZE;
  memaddr = db->addr;
  myaddr  = db->data;

  while (len > 0)
    {
      /* Don't overrun if this block is right at the end of the region.  */
      region = lookup_mem_region (memaddr);
      if (region->hi == 0 || memaddr + len < region->hi)
	reg_len = len;
      else
	reg_len = region->hi - memaddr;

      /* Skip non-readable regions.  The cache attribute can be ignored,
         since we may be loading this for a stack access.  */
      if (region->attrib.mode == MEM_WO)
	{
	  memaddr += reg_len;
	  myaddr  += reg_len;
	  len     -= reg_len;
	  continue;
	}
      
      res = target_read (&current_target, TARGET_OBJECT_RAW_MEMORY,
			 NULL, myaddr, memaddr, reg_len);
      if (res < reg_len)
	return 0;

      memaddr += res;
      myaddr += res;
      len -= res;
    }

  return 1;
}

/* Get a free cache block, put or keep it on the valid list,
   and return its address.  */

static struct dcache_block *
dcache_alloc (DCACHE *dcache, CORE_ADDR addr)
{
  struct dcache_block *db;

  if (dcache->size >= DCACHE_SIZE)
    {
      /* Evict the least recently used line.  */
      db = dcache->oldest;
      dcache->oldest = db->newer;

      splay_tree_remove (dcache->tree, (splay_tree_key) db->addr);
    }
  else
    {
      db = dcache->freelist;
      if (db)
        dcache->freelist = db->newer;
      else
	db = xmalloc (sizeof (struct dcache_block));

      dcache->size++;
    }

  db->addr = MASK (addr);
  db->newer = NULL;
  db->refs = 0;

  if (dcache->newest)
    dcache->newest->newer = db;

  dcache->newest = db;

  if (!dcache->oldest)
    dcache->oldest = db;

  splay_tree_insert (dcache->tree, (splay_tree_key) db->addr,
		     (splay_tree_value) db);

  return db;
}

/* Using the data cache DCACHE return the contents of the byte at
   address ADDR in the remote machine.  

   Returns 1 for success, 0 for error.  */

static int
dcache_peek_byte (DCACHE *dcache, CORE_ADDR addr, gdb_byte *ptr)
{
  struct dcache_block *db = dcache_hit (dcache, addr);

  if (!db)
    {
      db = dcache_alloc (dcache, addr);

      if (!dcache_read_line (dcache, db))
         return 0;
    }

  *ptr = db->data[XFORM (addr)];
  return 1;
}

/* Write the byte at PTR into ADDR in the data cache.

   The caller is responsible for also promptly writing the data
   through to target memory.

   If addr is not in cache, this function does nothing; writing to
   an area of memory which wasn't present in the cache doesn't cause
   it to be loaded in.

   Always return 1 (meaning success) to simplify dcache_xfer_memory.  */

static int
dcache_poke_byte (DCACHE *dcache, CORE_ADDR addr, gdb_byte *ptr)
{
  struct dcache_block *db = dcache_hit (dcache, addr);

  if (db)
    db->data[XFORM (addr)] = *ptr;

  return 1;
}

static int
dcache_splay_tree_compare (splay_tree_key a, splay_tree_key b)
{
  if (a > b)
    return 1;
  else if (a == b)
    return 0;
  else
    return -1;
}

/* Initialize the data cache.  */

DCACHE *
dcache_init (void)
{
  DCACHE *dcache;
  int i;

  dcache = (DCACHE *) xmalloc (sizeof (*dcache));

  dcache->tree = splay_tree_new (dcache_splay_tree_compare,
				 NULL,
				 NULL);

  dcache->oldest = NULL;
  dcache->newest = NULL;
  dcache->freelist = NULL;
  dcache->size = 0;
  dcache->ptid = null_ptid;
  last_cache = dcache;

  return dcache;
}

/* Free a data cache.  */

void
dcache_free (DCACHE *dcache)
{
  struct dcache_block *db, *next;

  if (last_cache == dcache)
    last_cache = NULL;

  splay_tree_delete (dcache->tree);
  for (db = dcache->freelist; db != NULL; db = next)
    {
      next = db->newer;
      xfree (db);
    }
  xfree (dcache);
}

/* Read or write LEN bytes from inferior memory at MEMADDR, transferring
   to or from debugger address MYADDR.  Write to inferior if SHOULD_WRITE is
   nonzero. 

   The meaning of the result is the same as for target_write.  */

int
dcache_xfer_memory (struct target_ops *ops, DCACHE *dcache,
		    CORE_ADDR memaddr, gdb_byte *myaddr,
		    int len, int should_write)
{
  int i;
  int res;
  int (*xfunc) (DCACHE *dcache, CORE_ADDR addr, gdb_byte *ptr);
  xfunc = should_write ? dcache_poke_byte : dcache_peek_byte;

  /* If this is a different inferior from what we've recorded,
     flush the cache.  */

  if (! ptid_equal (inferior_ptid, dcache->ptid))
    {
      dcache_invalidate (dcache);
      dcache->ptid = inferior_ptid;
    }

  /* Do write-through first, so that if it fails, we don't write to
     the cache at all.  */

  if (should_write)
    {
      res = target_write (ops, TARGET_OBJECT_RAW_MEMORY,
			  NULL, myaddr, memaddr, len);
      if (res <= 0)
	return res;
      /* Update LEN to what was actually written.  */
      len = res;
    }
      
  for (i = 0; i < len; i++)
    {
      if (!xfunc (dcache, memaddr + i, myaddr + i))
	{
	  /* That failed.  Discard its cache line so we don't have a
	     partially read line.  */
	  dcache_invalidate_line (dcache, memaddr + i);
	  /* If we're writing, we still wrote LEN bytes.  */
	  if (should_write)
	    return len;
	  else
	    return i;
	}
    }
    
  return len;
}

/* FIXME: There would be some benefit to making the cache write-back and
   moving the writeback operation to a higher layer, as it could occur
   after a sequence of smaller writes have been completed (as when a stack
   frame is constructed for an inferior function call).  Note that only
   moving it up one level to target_xfer_memory[_partial]() is not
   sufficient since we want to coalesce memory transfers that are
   "logically" connected but not actually a single call to one of the
   memory transfer functions.  */

/* Just update any cache lines which are already present.  This is called
   by memory_xfer_partial in cases where the access would otherwise not go
   through the cache.  */

void
dcache_update (DCACHE *dcache, CORE_ADDR memaddr, gdb_byte *myaddr, int len)
{
  int i;
  for (i = 0; i < len; i++)
    dcache_poke_byte (dcache, memaddr + i, myaddr + i);
}

static void
dcache_print_line (int index)
{
  splay_tree_node n;
  struct dcache_block *db;
  int i, j;

  if (!last_cache)
    {
      printf_filtered (_("No data cache available.\n"));
      return;
    }

  n = splay_tree_min (last_cache->tree);

  for (i = index; i > 0; --i)
    {
      if (!n)
	break;
      n = splay_tree_successor (last_cache->tree, n->key);
    }

  if (!n)
    {
      printf_filtered (_("No such cache line exists.\n"));
      return;
    }
    
  db = (struct dcache_block *) n->value;

  printf_filtered (_("Line %d: address %s [%d hits]\n"),
		   index, paddress (target_gdbarch, db->addr), db->refs);

  for (j = 0; j < LINE_SIZE; j++)
    {
      printf_filtered ("%02x ", db->data[j]);

      /* Print a newline every 16 bytes (48 characters) */
      if ((j % 16 == 15) && (j != LINE_SIZE - 1))
	printf_filtered ("\n");
    }
  printf_filtered ("\n");
}

static void
dcache_info (char *exp, int tty)
{
  splay_tree_node n;
  int i, refcount, lineno;

  if (exp)
    {
      char *linestart;
      i = strtol (exp, &linestart, 10);
      if (linestart == exp || i < 0)
	{
	  printf_filtered (_("Usage: info dcache [linenumber]\n"));
          return;
	}

      dcache_print_line (i);
      return;
    }

  printf_filtered (_("Dcache line width %d, maximum size %d\n"),
		   LINE_SIZE, DCACHE_SIZE);

  if (!last_cache || ptid_equal (last_cache->ptid, null_ptid))
    {
      printf_filtered (_("No data cache available.\n"));
      return;
    }

  printf_filtered (_("Contains data for %s\n"),
		   target_pid_to_str (last_cache->ptid));

  refcount = 0;

  n = splay_tree_min (last_cache->tree);
  i = 0;

  while (n)
    {
      struct dcache_block *db = (struct dcache_block *) n->value;

      printf_filtered (_("Line %d: address %s [%d hits]\n"),
		       i, paddress (target_gdbarch, db->addr), db->refs);
      i++;
      refcount += db->refs;

      n = splay_tree_successor (last_cache->tree, n->key);
    }

  printf_filtered (_("Cache state: %d active lines, %d hits\n"), i, refcount);
}

void
_initialize_dcache (void)
{
  add_setshow_boolean_cmd ("remotecache", class_support,
			   &dcache_enabled_p, _("\
Set cache use for remote targets."), _("\
Show cache use for remote targets."), _("\
This used to enable the data cache for remote targets.  The cache\n\
functionality is now controlled by the memory region system and the\n\
\"stack-cache\" flag; \"remotecache\" now does nothing and\n\
exists only for compatibility reasons."),
			   NULL,
			   show_dcache_enabled_p,
			   &setlist, &showlist);

  add_info ("dcache", dcache_info,
	    _("\
Print information on the dcache performance.\n\
With no arguments, this command prints the cache configuration and a\n\
summary of each line in the cache.  Use \"info dcache <lineno> to dump\"\n\
the contents of a given line."));
}
