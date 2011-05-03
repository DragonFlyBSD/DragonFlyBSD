/* Low level packing and unpacking of values for GDB, the GNU Debugger.

   Copyright (C) 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2002, 2003, 2004, 2005, 2006, 2007, 2008,
   2009, 2010 Free Software Foundation, Inc.

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
#include "arch-utils.h"
#include "gdb_string.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "value.h"
#include "gdbcore.h"
#include "command.h"
#include "gdbcmd.h"
#include "target.h"
#include "language.h"
#include "demangle.h"
#include "doublest.h"
#include "gdb_assert.h"
#include "regcache.h"
#include "block.h"
#include "dfp.h"
#include "objfiles.h"
#include "valprint.h"
#include "cli/cli-decode.h"

#include "python/python.h"

/* Prototypes for exported functions. */

void _initialize_values (void);

/* Definition of a user function.  */
struct internal_function
{
  /* The name of the function.  It is a bit odd to have this in the
     function itself -- the user might use a differently-named
     convenience variable to hold the function.  */
  char *name;

  /* The handler.  */
  internal_function_fn handler;

  /* User data for the handler.  */
  void *cookie;
};

static struct cmd_list_element *functionlist;

struct value
{
  /* Type of value; either not an lval, or one of the various
     different possible kinds of lval.  */
  enum lval_type lval;

  /* Is it modifiable?  Only relevant if lval != not_lval.  */
  int modifiable;

  /* Location of value (if lval).  */
  union
  {
    /* If lval == lval_memory, this is the address in the inferior.
       If lval == lval_register, this is the byte offset into the
       registers structure.  */
    CORE_ADDR address;

    /* Pointer to internal variable.  */
    struct internalvar *internalvar;

    /* If lval == lval_computed, this is a set of function pointers
       to use to access and describe the value, and a closure pointer
       for them to use.  */
    struct
    {
      struct lval_funcs *funcs; /* Functions to call.  */
      void *closure;            /* Closure for those functions to use.  */
    } computed;
  } location;

  /* Describes offset of a value within lval of a structure in bytes.
     If lval == lval_memory, this is an offset to the address.  If
     lval == lval_register, this is a further offset from
     location.address within the registers structure.  Note also the
     member embedded_offset below.  */
  int offset;

  /* Only used for bitfields; number of bits contained in them.  */
  int bitsize;

  /* Only used for bitfields; position of start of field.  For
     gdbarch_bits_big_endian=0 targets, it is the position of the LSB.  For
     gdbarch_bits_big_endian=1 targets, it is the position of the MSB. */
  int bitpos;

  /* Only used for bitfields; the containing value.  This allows a
     single read from the target when displaying multiple
     bitfields.  */
  struct value *parent;

  /* Frame register value is relative to.  This will be described in
     the lval enum above as "lval_register".  */
  struct frame_id frame_id;

  /* Type of the value.  */
  struct type *type;

  /* If a value represents a C++ object, then the `type' field gives
     the object's compile-time type.  If the object actually belongs
     to some class derived from `type', perhaps with other base
     classes and additional members, then `type' is just a subobject
     of the real thing, and the full object is probably larger than
     `type' would suggest.

     If `type' is a dynamic class (i.e. one with a vtable), then GDB
     can actually determine the object's run-time type by looking at
     the run-time type information in the vtable.  When this
     information is available, we may elect to read in the entire
     object, for several reasons:

     - When printing the value, the user would probably rather see the
     full object, not just the limited portion apparent from the
     compile-time type.

     - If `type' has virtual base classes, then even printing `type'
     alone may require reaching outside the `type' portion of the
     object to wherever the virtual base class has been stored.

     When we store the entire object, `enclosing_type' is the run-time
     type -- the complete object -- and `embedded_offset' is the
     offset of `type' within that larger type, in bytes.  The
     value_contents() macro takes `embedded_offset' into account, so
     most GDB code continues to see the `type' portion of the value,
     just as the inferior would.

     If `type' is a pointer to an object, then `enclosing_type' is a
     pointer to the object's run-time type, and `pointed_to_offset' is
     the offset in bytes from the full object to the pointed-to object
     -- that is, the value `embedded_offset' would have if we followed
     the pointer and fetched the complete object.  (I don't really see
     the point.  Why not just determine the run-time type when you
     indirect, and avoid the special case?  The contents don't matter
     until you indirect anyway.)

     If we're not doing anything fancy, `enclosing_type' is equal to
     `type', and `embedded_offset' is zero, so everything works
     normally.  */
  struct type *enclosing_type;
  int embedded_offset;
  int pointed_to_offset;

  /* Values are stored in a chain, so that they can be deleted easily
     over calls to the inferior.  Values assigned to internal
     variables, put into the value history or exposed to Python are
     taken off this list.  */
  struct value *next;

  /* Register number if the value is from a register.  */
  short regnum;

  /* If zero, contents of this value are in the contents field.  If
     nonzero, contents are in inferior.  If the lval field is lval_memory,
     the contents are in inferior memory at location.address plus offset.
     The lval field may also be lval_register.

     WARNING: This field is used by the code which handles watchpoints
     (see breakpoint.c) to decide whether a particular value can be
     watched by hardware watchpoints.  If the lazy flag is set for
     some member of a value chain, it is assumed that this member of
     the chain doesn't need to be watched as part of watching the
     value itself.  This is how GDB avoids watching the entire struct
     or array when the user wants to watch a single struct member or
     array element.  If you ever change the way lazy flag is set and
     reset, be sure to consider this use as well!  */
  char lazy;

  /* If nonzero, this is the value of a variable which does not
     actually exist in the program.  */
  char optimized_out;

  /* If value is a variable, is it initialized or not.  */
  int initialized;

  /* If value is from the stack.  If this is set, read_stack will be
     used instead of read_memory to enable extra caching.  */
  int stack;

  /* Actual contents of the value.  Target byte-order.  NULL or not
     valid if lazy is nonzero.  */
  gdb_byte *contents;

  /* The number of references to this value.  When a value is created,
     the value chain holds a reference, so REFERENCE_COUNT is 1.  If
     release_value is called, this value is removed from the chain but
     the caller of release_value now has a reference to this value.
     The caller must arrange for a call to value_free later.  */
  int reference_count;
};

/* Prototypes for local functions. */

static void show_values (char *, int);

static void show_convenience (char *, int);


/* The value-history records all the values printed
   by print commands during this session.  Each chunk
   records 60 consecutive values.  The first chunk on
   the chain records the most recent values.
   The total number of values is in value_history_count.  */

#define VALUE_HISTORY_CHUNK 60

struct value_history_chunk
  {
    struct value_history_chunk *next;
    struct value *values[VALUE_HISTORY_CHUNK];
  };

/* Chain of chunks now in use.  */

static struct value_history_chunk *value_history_chain;

static int value_history_count;	/* Abs number of last entry stored */


/* List of all value objects currently allocated
   (except for those released by calls to release_value)
   This is so they can be freed after each command.  */

static struct value *all_values;

/* Allocate a lazy value for type TYPE.  Its actual content is
   "lazily" allocated too: the content field of the return value is
   NULL; it will be allocated when it is fetched from the target.  */

struct value *
allocate_value_lazy (struct type *type)
{
  struct value *val;

  /* Call check_typedef on our type to make sure that, if TYPE
     is a TYPE_CODE_TYPEDEF, its length is set to the length
     of the target type instead of zero.  However, we do not
     replace the typedef type by the target type, because we want
     to keep the typedef in order to be able to set the VAL's type
     description correctly.  */
  check_typedef (type);

  val = (struct value *) xzalloc (sizeof (struct value));
  val->contents = NULL;
  val->next = all_values;
  all_values = val;
  val->type = type;
  val->enclosing_type = type;
  VALUE_LVAL (val) = not_lval;
  val->location.address = 0;
  VALUE_FRAME_ID (val) = null_frame_id;
  val->offset = 0;
  val->bitpos = 0;
  val->bitsize = 0;
  VALUE_REGNUM (val) = -1;
  val->lazy = 1;
  val->optimized_out = 0;
  val->embedded_offset = 0;
  val->pointed_to_offset = 0;
  val->modifiable = 1;
  val->initialized = 1;  /* Default to initialized.  */

  /* Values start out on the all_values chain.  */
  val->reference_count = 1;

  return val;
}

/* Allocate the contents of VAL if it has not been allocated yet.  */

void
allocate_value_contents (struct value *val)
{
  if (!val->contents)
    val->contents = (gdb_byte *) xzalloc (TYPE_LENGTH (val->enclosing_type));
}

/* Allocate a  value  and its contents for type TYPE.  */

struct value *
allocate_value (struct type *type)
{
  struct value *val = allocate_value_lazy (type);

  allocate_value_contents (val);
  val->lazy = 0;
  return val;
}

/* Allocate a  value  that has the correct length
   for COUNT repetitions of type TYPE.  */

struct value *
allocate_repeat_value (struct type *type, int count)
{
  int low_bound = current_language->string_lower_bound;		/* ??? */
  /* FIXME-type-allocation: need a way to free this type when we are
     done with it.  */
  struct type *array_type
    = lookup_array_range_type (type, low_bound, count + low_bound - 1);

  return allocate_value (array_type);
}

struct value *
allocate_computed_value (struct type *type,
                         struct lval_funcs *funcs,
                         void *closure)
{
  struct value *v = allocate_value (type);

  VALUE_LVAL (v) = lval_computed;
  v->location.computed.funcs = funcs;
  v->location.computed.closure = closure;
  set_value_lazy (v, 1);

  return v;
}

/* Accessor methods.  */

struct value *
value_next (struct value *value)
{
  return value->next;
}

struct type *
value_type (const struct value *value)
{
  return value->type;
}
void
deprecated_set_value_type (struct value *value, struct type *type)
{
  value->type = type;
}

int
value_offset (const struct value *value)
{
  return value->offset;
}
void
set_value_offset (struct value *value, int offset)
{
  value->offset = offset;
}

int
value_bitpos (const struct value *value)
{
  return value->bitpos;
}
void
set_value_bitpos (struct value *value, int bit)
{
  value->bitpos = bit;
}

int
value_bitsize (const struct value *value)
{
  return value->bitsize;
}
void
set_value_bitsize (struct value *value, int bit)
{
  value->bitsize = bit;
}

struct value *
value_parent (struct value *value)
{
  return value->parent;
}

gdb_byte *
value_contents_raw (struct value *value)
{
  allocate_value_contents (value);
  return value->contents + value->embedded_offset;
}

gdb_byte *
value_contents_all_raw (struct value *value)
{
  allocate_value_contents (value);
  return value->contents;
}

struct type *
value_enclosing_type (struct value *value)
{
  return value->enclosing_type;
}

static void
require_not_optimized_out (struct value *value)
{
  if (value->optimized_out)
    error (_("value has been optimized out"));
}

const gdb_byte *
value_contents_for_printing (struct value *value)
{
  if (value->lazy)
    value_fetch_lazy (value);
  return value->contents;
}

const gdb_byte *
value_contents_all (struct value *value)
{
  const gdb_byte *result = value_contents_for_printing (value);
  require_not_optimized_out (value);
  return result;
}

int
value_lazy (struct value *value)
{
  return value->lazy;
}

void
set_value_lazy (struct value *value, int val)
{
  value->lazy = val;
}

int
value_stack (struct value *value)
{
  return value->stack;
}

void
set_value_stack (struct value *value, int val)
{
  value->stack = val;
}

const gdb_byte *
value_contents (struct value *value)
{
  const gdb_byte *result = value_contents_writeable (value);
  require_not_optimized_out (value);
  return result;
}

gdb_byte *
value_contents_writeable (struct value *value)
{
  if (value->lazy)
    value_fetch_lazy (value);
  return value_contents_raw (value);
}

/* Return non-zero if VAL1 and VAL2 have the same contents.  Note that
   this function is different from value_equal; in C the operator ==
   can return 0 even if the two values being compared are equal.  */

int
value_contents_equal (struct value *val1, struct value *val2)
{
  struct type *type1;
  struct type *type2;
  int len;

  type1 = check_typedef (value_type (val1));
  type2 = check_typedef (value_type (val2));
  len = TYPE_LENGTH (type1);
  if (len != TYPE_LENGTH (type2))
    return 0;

  return (memcmp (value_contents (val1), value_contents (val2), len) == 0);
}

int
value_optimized_out (struct value *value)
{
  return value->optimized_out;
}

void
set_value_optimized_out (struct value *value, int val)
{
  value->optimized_out = val;
}

int
value_entirely_optimized_out (const struct value *value)
{
  if (!value->optimized_out)
    return 0;
  if (value->lval != lval_computed
      || !value->location.computed.funcs->check_validity)
    return 1;
  return !value->location.computed.funcs->check_any_valid (value);
}

int
value_bits_valid (const struct value *value, int offset, int length)
{
  if (value == NULL || !value->optimized_out)
    return 1;
  if (value->lval != lval_computed
      || !value->location.computed.funcs->check_validity)
    return 0;
  return value->location.computed.funcs->check_validity (value, offset,
							 length);
}

int
value_embedded_offset (struct value *value)
{
  return value->embedded_offset;
}

void
set_value_embedded_offset (struct value *value, int val)
{
  value->embedded_offset = val;
}

int
value_pointed_to_offset (struct value *value)
{
  return value->pointed_to_offset;
}

void
set_value_pointed_to_offset (struct value *value, int val)
{
  value->pointed_to_offset = val;
}

struct lval_funcs *
value_computed_funcs (struct value *v)
{
  gdb_assert (VALUE_LVAL (v) == lval_computed);

  return v->location.computed.funcs;
}

void *
value_computed_closure (const struct value *v)
{
  gdb_assert (v->lval == lval_computed);

  return v->location.computed.closure;
}

enum lval_type *
deprecated_value_lval_hack (struct value *value)
{
  return &value->lval;
}

CORE_ADDR
value_address (struct value *value)
{
  if (value->lval == lval_internalvar
      || value->lval == lval_internalvar_component)
    return 0;
  return value->location.address + value->offset;
}

CORE_ADDR
value_raw_address (struct value *value)
{
  if (value->lval == lval_internalvar
      || value->lval == lval_internalvar_component)
    return 0;
  return value->location.address;
}

void
set_value_address (struct value *value, CORE_ADDR addr)
{
  gdb_assert (value->lval != lval_internalvar
	      && value->lval != lval_internalvar_component);
  value->location.address = addr;
}

struct internalvar **
deprecated_value_internalvar_hack (struct value *value)
{
  return &value->location.internalvar;
}

struct frame_id *
deprecated_value_frame_id_hack (struct value *value)
{
  return &value->frame_id;
}

short *
deprecated_value_regnum_hack (struct value *value)
{
  return &value->regnum;
}

int
deprecated_value_modifiable (struct value *value)
{
  return value->modifiable;
}
void
deprecated_set_value_modifiable (struct value *value, int modifiable)
{
  value->modifiable = modifiable;
}

/* Return a mark in the value chain.  All values allocated after the
   mark is obtained (except for those released) are subject to being freed
   if a subsequent value_free_to_mark is passed the mark.  */
struct value *
value_mark (void)
{
  return all_values;
}

/* Take a reference to VAL.  VAL will not be deallocated until all
   references are released.  */

void
value_incref (struct value *val)
{
  val->reference_count++;
}

/* Release a reference to VAL, which was acquired with value_incref.
   This function is also called to deallocate values from the value
   chain.  */

void
value_free (struct value *val)
{
  if (val)
    {
      gdb_assert (val->reference_count > 0);
      val->reference_count--;
      if (val->reference_count > 0)
	return;

      /* If there's an associated parent value, drop our reference to
	 it.  */
      if (val->parent != NULL)
	value_free (val->parent);

      if (VALUE_LVAL (val) == lval_computed)
	{
	  struct lval_funcs *funcs = val->location.computed.funcs;

	  if (funcs->free_closure)
	    funcs->free_closure (val);
	}

      xfree (val->contents);
    }
  xfree (val);
}

/* Free all values allocated since MARK was obtained by value_mark
   (except for those released).  */
void
value_free_to_mark (struct value *mark)
{
  struct value *val;
  struct value *next;

  for (val = all_values; val && val != mark; val = next)
    {
      next = val->next;
      value_free (val);
    }
  all_values = val;
}

/* Free all the values that have been allocated (except for those released).
   Call after each command, successful or not.
   In practice this is called before each command, which is sufficient.  */

void
free_all_values (void)
{
  struct value *val;
  struct value *next;

  for (val = all_values; val; val = next)
    {
      next = val->next;
      value_free (val);
    }

  all_values = 0;
}

/* Frees all the elements in a chain of values.  */

void
free_value_chain (struct value *v)
{
  struct value *next;

  for (; v; v = next)
    {
      next = value_next (v);
      value_free (v);
    }
}

/* Remove VAL from the chain all_values
   so it will not be freed automatically.  */

void
release_value (struct value *val)
{
  struct value *v;

  if (all_values == val)
    {
      all_values = val->next;
      return;
    }

  for (v = all_values; v; v = v->next)
    {
      if (v->next == val)
	{
	  v->next = val->next;
	  break;
	}
    }
}

/* Release all values up to mark  */
struct value *
value_release_to_mark (struct value *mark)
{
  struct value *val;
  struct value *next;

  for (val = next = all_values; next; next = next->next)
    if (next->next == mark)
      {
	all_values = next->next;
	next->next = NULL;
	return val;
      }
  all_values = 0;
  return val;
}

/* Return a copy of the value ARG.
   It contains the same contents, for same memory address,
   but it's a different block of storage.  */

struct value *
value_copy (struct value *arg)
{
  struct type *encl_type = value_enclosing_type (arg);
  struct value *val;

  if (value_lazy (arg))
    val = allocate_value_lazy (encl_type);
  else
    val = allocate_value (encl_type);
  val->type = arg->type;
  VALUE_LVAL (val) = VALUE_LVAL (arg);
  val->location = arg->location;
  val->offset = arg->offset;
  val->bitpos = arg->bitpos;
  val->bitsize = arg->bitsize;
  VALUE_FRAME_ID (val) = VALUE_FRAME_ID (arg);
  VALUE_REGNUM (val) = VALUE_REGNUM (arg);
  val->lazy = arg->lazy;
  val->optimized_out = arg->optimized_out;
  val->embedded_offset = value_embedded_offset (arg);
  val->pointed_to_offset = arg->pointed_to_offset;
  val->modifiable = arg->modifiable;
  if (!value_lazy (val))
    {
      memcpy (value_contents_all_raw (val), value_contents_all_raw (arg),
	      TYPE_LENGTH (value_enclosing_type (arg)));

    }
  val->parent = arg->parent;
  if (val->parent)
    value_incref (val->parent);
  if (VALUE_LVAL (val) == lval_computed)
    {
      struct lval_funcs *funcs = val->location.computed.funcs;

      if (funcs->copy_closure)
        val->location.computed.closure = funcs->copy_closure (val);
    }
  return val;
}

void
set_value_component_location (struct value *component,
			      const struct value *whole)
{
  if (whole->lval == lval_internalvar)
    VALUE_LVAL (component) = lval_internalvar_component;
  else
    VALUE_LVAL (component) = whole->lval;

  component->location = whole->location;
  if (whole->lval == lval_computed)
    {
      struct lval_funcs *funcs = whole->location.computed.funcs;

      if (funcs->copy_closure)
        component->location.computed.closure = funcs->copy_closure (whole);
    }
}


/* Access to the value history.  */

/* Record a new value in the value history.
   Returns the absolute history index of the entry.
   Result of -1 indicates the value was not saved; otherwise it is the
   value history index of this new item.  */

int
record_latest_value (struct value *val)
{
  int i;

  /* We don't want this value to have anything to do with the inferior anymore.
     In particular, "set $1 = 50" should not affect the variable from which
     the value was taken, and fast watchpoints should be able to assume that
     a value on the value history never changes.  */
  if (value_lazy (val))
    value_fetch_lazy (val);
  /* We preserve VALUE_LVAL so that the user can find out where it was fetched
     from.  This is a bit dubious, because then *&$1 does not just return $1
     but the current contents of that location.  c'est la vie...  */
  val->modifiable = 0;
  release_value (val);

  /* Here we treat value_history_count as origin-zero
     and applying to the value being stored now.  */

  i = value_history_count % VALUE_HISTORY_CHUNK;
  if (i == 0)
    {
      struct value_history_chunk *new
	= (struct value_history_chunk *)

      xmalloc (sizeof (struct value_history_chunk));
      memset (new->values, 0, sizeof new->values);
      new->next = value_history_chain;
      value_history_chain = new;
    }

  value_history_chain->values[i] = val;

  /* Now we regard value_history_count as origin-one
     and applying to the value just stored.  */

  return ++value_history_count;
}

/* Return a copy of the value in the history with sequence number NUM.  */

struct value *
access_value_history (int num)
{
  struct value_history_chunk *chunk;
  int i;
  int absnum = num;

  if (absnum <= 0)
    absnum += value_history_count;

  if (absnum <= 0)
    {
      if (num == 0)
	error (_("The history is empty."));
      else if (num == 1)
	error (_("There is only one value in the history."));
      else
	error (_("History does not go back to $$%d."), -num);
    }
  if (absnum > value_history_count)
    error (_("History has not yet reached $%d."), absnum);

  absnum--;

  /* Now absnum is always absolute and origin zero.  */

  chunk = value_history_chain;
  for (i = (value_history_count - 1) / VALUE_HISTORY_CHUNK - absnum / VALUE_HISTORY_CHUNK;
       i > 0; i--)
    chunk = chunk->next;

  return value_copy (chunk->values[absnum % VALUE_HISTORY_CHUNK]);
}

static void
show_values (char *num_exp, int from_tty)
{
  int i;
  struct value *val;
  static int num = 1;

  if (num_exp)
    {
      /* "show values +" should print from the stored position.
         "show values <exp>" should print around value number <exp>.  */
      if (num_exp[0] != '+' || num_exp[1] != '\0')
	num = parse_and_eval_long (num_exp) - 5;
    }
  else
    {
      /* "show values" means print the last 10 values.  */
      num = value_history_count - 9;
    }

  if (num <= 0)
    num = 1;

  for (i = num; i < num + 10 && i <= value_history_count; i++)
    {
      struct value_print_options opts;

      val = access_value_history (i);
      printf_filtered (("$%d = "), i);
      get_user_print_options (&opts);
      value_print (val, gdb_stdout, &opts);
      printf_filtered (("\n"));
    }

  /* The next "show values +" should start after what we just printed.  */
  num += 10;

  /* Hitting just return after this command should do the same thing as
     "show values +".  If num_exp is null, this is unnecessary, since
     "show values +" is not useful after "show values".  */
  if (from_tty && num_exp)
    {
      num_exp[0] = '+';
      num_exp[1] = '\0';
    }
}

/* Internal variables.  These are variables within the debugger
   that hold values assigned by debugger commands.
   The user refers to them with a '$' prefix
   that does not appear in the variable names stored internally.  */

struct internalvar
{
  struct internalvar *next;
  char *name;

  /* We support various different kinds of content of an internal variable.
     enum internalvar_kind specifies the kind, and union internalvar_data
     provides the data associated with this particular kind.  */

  enum internalvar_kind
    {
      /* The internal variable is empty.  */
      INTERNALVAR_VOID,

      /* The value of the internal variable is provided directly as
	 a GDB value object.  */
      INTERNALVAR_VALUE,

      /* A fresh value is computed via a call-back routine on every
	 access to the internal variable.  */
      INTERNALVAR_MAKE_VALUE,

      /* The internal variable holds a GDB internal convenience function.  */
      INTERNALVAR_FUNCTION,

      /* The variable holds an integer value.  */
      INTERNALVAR_INTEGER,

      /* The variable holds a pointer value.  */
      INTERNALVAR_POINTER,

      /* The variable holds a GDB-provided string.  */
      INTERNALVAR_STRING,

    } kind;

  union internalvar_data
    {
      /* A value object used with INTERNALVAR_VALUE.  */
      struct value *value;

      /* The call-back routine used with INTERNALVAR_MAKE_VALUE.  */
      internalvar_make_value make_value;

      /* The internal function used with INTERNALVAR_FUNCTION.  */
      struct
	{
	  struct internal_function *function;
	  /* True if this is the canonical name for the function.  */
	  int canonical;
	} fn;

      /* An integer value used with INTERNALVAR_INTEGER.  */
      struct
        {
	  /* If type is non-NULL, it will be used as the type to generate
	     a value for this internal variable.  If type is NULL, a default
	     integer type for the architecture is used.  */
	  struct type *type;
	  LONGEST val;
        } integer;

      /* A pointer value used with INTERNALVAR_POINTER.  */
      struct
        {
	  struct type *type;
	  CORE_ADDR val;
        } pointer;

      /* A string value used with INTERNALVAR_STRING.  */
      char *string;
    } u;
};

static struct internalvar *internalvars;

/* If the variable does not already exist create it and give it the value given.
   If no value is given then the default is zero.  */
static void
init_if_undefined_command (char* args, int from_tty)
{
  struct internalvar* intvar;

  /* Parse the expression - this is taken from set_command().  */
  struct expression *expr = parse_expression (args);
  register struct cleanup *old_chain =
    make_cleanup (free_current_contents, &expr);

  /* Validate the expression.
     Was the expression an assignment?
     Or even an expression at all?  */
  if (expr->nelts == 0 || expr->elts[0].opcode != BINOP_ASSIGN)
    error (_("Init-if-undefined requires an assignment expression."));

  /* Extract the variable from the parsed expression.
     In the case of an assign the lvalue will be in elts[1] and elts[2].  */
  if (expr->elts[1].opcode != OP_INTERNALVAR)
    error (_("The first parameter to init-if-undefined should be a GDB variable."));
  intvar = expr->elts[2].internalvar;

  /* Only evaluate the expression if the lvalue is void.
     This may still fail if the expresssion is invalid.  */
  if (intvar->kind == INTERNALVAR_VOID)
    evaluate_expression (expr);

  do_cleanups (old_chain);
}


/* Look up an internal variable with name NAME.  NAME should not
   normally include a dollar sign.

   If the specified internal variable does not exist,
   the return value is NULL.  */

struct internalvar *
lookup_only_internalvar (const char *name)
{
  struct internalvar *var;

  for (var = internalvars; var; var = var->next)
    if (strcmp (var->name, name) == 0)
      return var;

  return NULL;
}


/* Create an internal variable with name NAME and with a void value.
   NAME should not normally include a dollar sign.  */

struct internalvar *
create_internalvar (const char *name)
{
  struct internalvar *var;

  var = (struct internalvar *) xmalloc (sizeof (struct internalvar));
  var->name = concat (name, (char *)NULL);
  var->kind = INTERNALVAR_VOID;
  var->next = internalvars;
  internalvars = var;
  return var;
}

/* Create an internal variable with name NAME and register FUN as the
   function that value_of_internalvar uses to create a value whenever
   this variable is referenced.  NAME should not normally include a
   dollar sign.  */

struct internalvar *
create_internalvar_type_lazy (char *name, internalvar_make_value fun)
{
  struct internalvar *var = create_internalvar (name);

  var->kind = INTERNALVAR_MAKE_VALUE;
  var->u.make_value = fun;
  return var;
}

/* Look up an internal variable with name NAME.  NAME should not
   normally include a dollar sign.

   If the specified internal variable does not exist,
   one is created, with a void value.  */

struct internalvar *
lookup_internalvar (const char *name)
{
  struct internalvar *var;

  var = lookup_only_internalvar (name);
  if (var)
    return var;

  return create_internalvar (name);
}

/* Return current value of internal variable VAR.  For variables that
   are not inherently typed, use a value type appropriate for GDBARCH.  */

struct value *
value_of_internalvar (struct gdbarch *gdbarch, struct internalvar *var)
{
  struct value *val;

  switch (var->kind)
    {
    case INTERNALVAR_VOID:
      val = allocate_value (builtin_type (gdbarch)->builtin_void);
      break;

    case INTERNALVAR_FUNCTION:
      val = allocate_value (builtin_type (gdbarch)->internal_fn);
      break;

    case INTERNALVAR_INTEGER:
      if (!var->u.integer.type)
	val = value_from_longest (builtin_type (gdbarch)->builtin_int,
				  var->u.integer.val);
      else
	val = value_from_longest (var->u.integer.type, var->u.integer.val);
      break;

    case INTERNALVAR_POINTER:
      val = value_from_pointer (var->u.pointer.type, var->u.pointer.val);
      break;

    case INTERNALVAR_STRING:
      val = value_cstring (var->u.string, strlen (var->u.string),
			   builtin_type (gdbarch)->builtin_char);
      break;

    case INTERNALVAR_VALUE:
      val = value_copy (var->u.value);
      if (value_lazy (val))
	value_fetch_lazy (val);
      break;

    case INTERNALVAR_MAKE_VALUE:
      val = (*var->u.make_value) (gdbarch, var);
      break;

    default:
      internal_error (__FILE__, __LINE__, "bad kind");
    }

  /* Change the VALUE_LVAL to lval_internalvar so that future operations
     on this value go back to affect the original internal variable.

     Do not do this for INTERNALVAR_MAKE_VALUE variables, as those have
     no underlying modifyable state in the internal variable.

     Likewise, if the variable's value is a computed lvalue, we want
     references to it to produce another computed lvalue, where
     references and assignments actually operate through the
     computed value's functions.

     This means that internal variables with computed values
     behave a little differently from other internal variables:
     assignments to them don't just replace the previous value
     altogether.  At the moment, this seems like the behavior we
     want.  */

  if (var->kind != INTERNALVAR_MAKE_VALUE
      && val->lval != lval_computed)
    {
      VALUE_LVAL (val) = lval_internalvar;
      VALUE_INTERNALVAR (val) = var;
    }

  return val;
}

int
get_internalvar_integer (struct internalvar *var, LONGEST *result)
{
  switch (var->kind)
    {
    case INTERNALVAR_INTEGER:
      *result = var->u.integer.val;
      return 1;

    default:
      return 0;
    }
}

static int
get_internalvar_function (struct internalvar *var,
			  struct internal_function **result)
{
  switch (var->kind)
    {
    case INTERNALVAR_FUNCTION:
      *result = var->u.fn.function;
      return 1;

    default:
      return 0;
    }
}

void
set_internalvar_component (struct internalvar *var, int offset, int bitpos,
			   int bitsize, struct value *newval)
{
  gdb_byte *addr;

  switch (var->kind)
    {
    case INTERNALVAR_VALUE:
      addr = value_contents_writeable (var->u.value);

      if (bitsize)
	modify_field (value_type (var->u.value), addr + offset,
		      value_as_long (newval), bitpos, bitsize);
      else
	memcpy (addr + offset, value_contents (newval),
		TYPE_LENGTH (value_type (newval)));
      break;

    default:
      /* We can never get a component of any other kind.  */
      internal_error (__FILE__, __LINE__, "set_internalvar_component");
    }
}

void
set_internalvar (struct internalvar *var, struct value *val)
{
  enum internalvar_kind new_kind;
  union internalvar_data new_data = { 0 };

  if (var->kind == INTERNALVAR_FUNCTION && var->u.fn.canonical)
    error (_("Cannot overwrite convenience function %s"), var->name);

  /* Prepare new contents.  */
  switch (TYPE_CODE (check_typedef (value_type (val))))
    {
    case TYPE_CODE_VOID:
      new_kind = INTERNALVAR_VOID;
      break;

    case TYPE_CODE_INTERNAL_FUNCTION:
      gdb_assert (VALUE_LVAL (val) == lval_internalvar);
      new_kind = INTERNALVAR_FUNCTION;
      get_internalvar_function (VALUE_INTERNALVAR (val),
				&new_data.fn.function);
      /* Copies created here are never canonical.  */
      break;

    case TYPE_CODE_INT:
      new_kind = INTERNALVAR_INTEGER;
      new_data.integer.type = value_type (val);
      new_data.integer.val = value_as_long (val);
      break;

    case TYPE_CODE_PTR:
      new_kind = INTERNALVAR_POINTER;
      new_data.pointer.type = value_type (val);
      new_data.pointer.val = value_as_address (val);
      break;

    default:
      new_kind = INTERNALVAR_VALUE;
      new_data.value = value_copy (val);
      new_data.value->modifiable = 1;

      /* Force the value to be fetched from the target now, to avoid problems
	 later when this internalvar is referenced and the target is gone or
	 has changed.  */
      if (value_lazy (new_data.value))
       value_fetch_lazy (new_data.value);

      /* Release the value from the value chain to prevent it from being
	 deleted by free_all_values.  From here on this function should not
	 call error () until new_data is installed into the var->u to avoid
	 leaking memory.  */
      release_value (new_data.value);
      break;
    }

  /* Clean up old contents.  */
  clear_internalvar (var);

  /* Switch over.  */
  var->kind = new_kind;
  var->u = new_data;
  /* End code which must not call error().  */
}

void
set_internalvar_integer (struct internalvar *var, LONGEST l)
{
  /* Clean up old contents.  */
  clear_internalvar (var);

  var->kind = INTERNALVAR_INTEGER;
  var->u.integer.type = NULL;
  var->u.integer.val = l;
}

void
set_internalvar_string (struct internalvar *var, const char *string)
{
  /* Clean up old contents.  */
  clear_internalvar (var);

  var->kind = INTERNALVAR_STRING;
  var->u.string = xstrdup (string);
}

static void
set_internalvar_function (struct internalvar *var, struct internal_function *f)
{
  /* Clean up old contents.  */
  clear_internalvar (var);

  var->kind = INTERNALVAR_FUNCTION;
  var->u.fn.function = f;
  var->u.fn.canonical = 1;
  /* Variables installed here are always the canonical version.  */
}

void
clear_internalvar (struct internalvar *var)
{
  /* Clean up old contents.  */
  switch (var->kind)
    {
    case INTERNALVAR_VALUE:
      value_free (var->u.value);
      break;

    case INTERNALVAR_STRING:
      xfree (var->u.string);
      break;

    default:
      break;
    }

  /* Reset to void kind.  */
  var->kind = INTERNALVAR_VOID;
}

char *
internalvar_name (struct internalvar *var)
{
  return var->name;
}

static struct internal_function *
create_internal_function (const char *name,
			  internal_function_fn handler, void *cookie)
{
  struct internal_function *ifn = XNEW (struct internal_function);

  ifn->name = xstrdup (name);
  ifn->handler = handler;
  ifn->cookie = cookie;
  return ifn;
}

char *
value_internal_function_name (struct value *val)
{
  struct internal_function *ifn;
  int result;

  gdb_assert (VALUE_LVAL (val) == lval_internalvar);
  result = get_internalvar_function (VALUE_INTERNALVAR (val), &ifn);
  gdb_assert (result);

  return ifn->name;
}

struct value *
call_internal_function (struct gdbarch *gdbarch,
			const struct language_defn *language,
			struct value *func, int argc, struct value **argv)
{
  struct internal_function *ifn;
  int result;

  gdb_assert (VALUE_LVAL (func) == lval_internalvar);
  result = get_internalvar_function (VALUE_INTERNALVAR (func), &ifn);
  gdb_assert (result);

  return (*ifn->handler) (gdbarch, language, ifn->cookie, argc, argv);
}

/* The 'function' command.  This does nothing -- it is just a
   placeholder to let "help function NAME" work.  This is also used as
   the implementation of the sub-command that is created when
   registering an internal function.  */
static void
function_command (char *command, int from_tty)
{
  /* Do nothing.  */
}

/* Clean up if an internal function's command is destroyed.  */
static void
function_destroyer (struct cmd_list_element *self, void *ignore)
{
  xfree (self->name);
  xfree (self->doc);
}

/* Add a new internal function.  NAME is the name of the function; DOC
   is a documentation string describing the function.  HANDLER is
   called when the function is invoked.  COOKIE is an arbitrary
   pointer which is passed to HANDLER and is intended for "user
   data".  */
void
add_internal_function (const char *name, const char *doc,
		       internal_function_fn handler, void *cookie)
{
  struct cmd_list_element *cmd;
  struct internal_function *ifn;
  struct internalvar *var = lookup_internalvar (name);

  ifn = create_internal_function (name, handler, cookie);
  set_internalvar_function (var, ifn);

  cmd = add_cmd (xstrdup (name), no_class, function_command, (char *) doc,
		 &functionlist);
  cmd->destroyer = function_destroyer;
}

/* Update VALUE before discarding OBJFILE.  COPIED_TYPES is used to
   prevent cycles / duplicates.  */

void
preserve_one_value (struct value *value, struct objfile *objfile,
		    htab_t copied_types)
{
  if (TYPE_OBJFILE (value->type) == objfile)
    value->type = copy_type_recursive (objfile, value->type, copied_types);

  if (TYPE_OBJFILE (value->enclosing_type) == objfile)
    value->enclosing_type = copy_type_recursive (objfile,
						 value->enclosing_type,
						 copied_types);
}

/* Likewise for internal variable VAR.  */

static void
preserve_one_internalvar (struct internalvar *var, struct objfile *objfile,
			  htab_t copied_types)
{
  switch (var->kind)
    {
    case INTERNALVAR_INTEGER:
      if (var->u.integer.type && TYPE_OBJFILE (var->u.integer.type) == objfile)
	var->u.integer.type
	  = copy_type_recursive (objfile, var->u.integer.type, copied_types);
      break;

    case INTERNALVAR_POINTER:
      if (TYPE_OBJFILE (var->u.pointer.type) == objfile)
	var->u.pointer.type
	  = copy_type_recursive (objfile, var->u.pointer.type, copied_types);
      break;

    case INTERNALVAR_VALUE:
      preserve_one_value (var->u.value, objfile, copied_types);
      break;
    }
}

/* Update the internal variables and value history when OBJFILE is
   discarded; we must copy the types out of the objfile.  New global types
   will be created for every convenience variable which currently points to
   this objfile's types, and the convenience variables will be adjusted to
   use the new global types.  */

void
preserve_values (struct objfile *objfile)
{
  htab_t copied_types;
  struct value_history_chunk *cur;
  struct internalvar *var;
  int i;

  /* Create the hash table.  We allocate on the objfile's obstack, since
     it is soon to be deleted.  */
  copied_types = create_copied_types_hash (objfile);

  for (cur = value_history_chain; cur; cur = cur->next)
    for (i = 0; i < VALUE_HISTORY_CHUNK; i++)
      if (cur->values[i])
	preserve_one_value (cur->values[i], objfile, copied_types);

  for (var = internalvars; var; var = var->next)
    preserve_one_internalvar (var, objfile, copied_types);

  preserve_python_values (objfile, copied_types);

  htab_delete (copied_types);
}

static void
show_convenience (char *ignore, int from_tty)
{
  struct gdbarch *gdbarch = get_current_arch ();
  struct internalvar *var;
  int varseen = 0;
  struct value_print_options opts;

  get_user_print_options (&opts);
  for (var = internalvars; var; var = var->next)
    {
      if (!varseen)
	{
	  varseen = 1;
	}
      printf_filtered (("$%s = "), var->name);
      value_print (value_of_internalvar (gdbarch, var), gdb_stdout,
		   &opts);
      printf_filtered (("\n"));
    }
  if (!varseen)
    printf_unfiltered (_("\
No debugger convenience variables now defined.\n\
Convenience variables have names starting with \"$\";\n\
use \"set\" as in \"set $foo = 5\" to define them.\n"));
}

/* Extract a value as a C number (either long or double).
   Knows how to convert fixed values to double, or
   floating values to long.
   Does not deallocate the value.  */

LONGEST
value_as_long (struct value *val)
{
  /* This coerces arrays and functions, which is necessary (e.g.
     in disassemble_command).  It also dereferences references, which
     I suspect is the most logical thing to do.  */
  val = coerce_array (val);
  return unpack_long (value_type (val), value_contents (val));
}

DOUBLEST
value_as_double (struct value *val)
{
  DOUBLEST foo;
  int inv;

  foo = unpack_double (value_type (val), value_contents (val), &inv);
  if (inv)
    error (_("Invalid floating value found in program."));
  return foo;
}

/* Extract a value as a C pointer. Does not deallocate the value.  
   Note that val's type may not actually be a pointer; value_as_long
   handles all the cases.  */
CORE_ADDR
value_as_address (struct value *val)
{
  struct gdbarch *gdbarch = get_type_arch (value_type (val));

  /* Assume a CORE_ADDR can fit in a LONGEST (for now).  Not sure
     whether we want this to be true eventually.  */
#if 0
  /* gdbarch_addr_bits_remove is wrong if we are being called for a
     non-address (e.g. argument to "signal", "info break", etc.), or
     for pointers to char, in which the low bits *are* significant.  */
  return gdbarch_addr_bits_remove (gdbarch, value_as_long (val));
#else

  /* There are several targets (IA-64, PowerPC, and others) which
     don't represent pointers to functions as simply the address of
     the function's entry point.  For example, on the IA-64, a
     function pointer points to a two-word descriptor, generated by
     the linker, which contains the function's entry point, and the
     value the IA-64 "global pointer" register should have --- to
     support position-independent code.  The linker generates
     descriptors only for those functions whose addresses are taken.

     On such targets, it's difficult for GDB to convert an arbitrary
     function address into a function pointer; it has to either find
     an existing descriptor for that function, or call malloc and
     build its own.  On some targets, it is impossible for GDB to
     build a descriptor at all: the descriptor must contain a jump
     instruction; data memory cannot be executed; and code memory
     cannot be modified.

     Upon entry to this function, if VAL is a value of type `function'
     (that is, TYPE_CODE (VALUE_TYPE (val)) == TYPE_CODE_FUNC), then
     value_address (val) is the address of the function.  This is what
     you'll get if you evaluate an expression like `main'.  The call
     to COERCE_ARRAY below actually does all the usual unary
     conversions, which includes converting values of type `function'
     to `pointer to function'.  This is the challenging conversion
     discussed above.  Then, `unpack_long' will convert that pointer
     back into an address.

     So, suppose the user types `disassemble foo' on an architecture
     with a strange function pointer representation, on which GDB
     cannot build its own descriptors, and suppose further that `foo'
     has no linker-built descriptor.  The address->pointer conversion
     will signal an error and prevent the command from running, even
     though the next step would have been to convert the pointer
     directly back into the same address.

     The following shortcut avoids this whole mess.  If VAL is a
     function, just return its address directly.  */
  if (TYPE_CODE (value_type (val)) == TYPE_CODE_FUNC
      || TYPE_CODE (value_type (val)) == TYPE_CODE_METHOD)
    return value_address (val);

  val = coerce_array (val);

  /* Some architectures (e.g. Harvard), map instruction and data
     addresses onto a single large unified address space.  For
     instance: An architecture may consider a large integer in the
     range 0x10000000 .. 0x1000ffff to already represent a data
     addresses (hence not need a pointer to address conversion) while
     a small integer would still need to be converted integer to
     pointer to address.  Just assume such architectures handle all
     integer conversions in a single function.  */

  /* JimB writes:

     I think INTEGER_TO_ADDRESS is a good idea as proposed --- but we
     must admonish GDB hackers to make sure its behavior matches the
     compiler's, whenever possible.

     In general, I think GDB should evaluate expressions the same way
     the compiler does.  When the user copies an expression out of
     their source code and hands it to a `print' command, they should
     get the same value the compiler would have computed.  Any
     deviation from this rule can cause major confusion and annoyance,
     and needs to be justified carefully.  In other words, GDB doesn't
     really have the freedom to do these conversions in clever and
     useful ways.

     AndrewC pointed out that users aren't complaining about how GDB
     casts integers to pointers; they are complaining that they can't
     take an address from a disassembly listing and give it to `x/i'.
     This is certainly important.

     Adding an architecture method like integer_to_address() certainly
     makes it possible for GDB to "get it right" in all circumstances
     --- the target has complete control over how things get done, so
     people can Do The Right Thing for their target without breaking
     anyone else.  The standard doesn't specify how integers get
     converted to pointers; usually, the ABI doesn't either, but
     ABI-specific code is a more reasonable place to handle it.  */

  if (TYPE_CODE (value_type (val)) != TYPE_CODE_PTR
      && TYPE_CODE (value_type (val)) != TYPE_CODE_REF
      && gdbarch_integer_to_address_p (gdbarch))
    return gdbarch_integer_to_address (gdbarch, value_type (val),
				       value_contents (val));

  return unpack_long (value_type (val), value_contents (val));
#endif
}

/* Unpack raw data (copied from debugee, target byte order) at VALADDR
   as a long, or as a double, assuming the raw data is described
   by type TYPE.  Knows how to convert different sizes of values
   and can convert between fixed and floating point.  We don't assume
   any alignment for the raw data.  Return value is in host byte order.

   If you want functions and arrays to be coerced to pointers, and
   references to be dereferenced, call value_as_long() instead.

   C++: It is assumed that the front-end has taken care of
   all matters concerning pointers to members.  A pointer
   to member which reaches here is considered to be equivalent
   to an INT (or some size).  After all, it is only an offset.  */

LONGEST
unpack_long (struct type *type, const gdb_byte *valaddr)
{
  enum bfd_endian byte_order = gdbarch_byte_order (get_type_arch (type));
  enum type_code code = TYPE_CODE (type);
  int len = TYPE_LENGTH (type);
  int nosign = TYPE_UNSIGNED (type);

  switch (code)
    {
    case TYPE_CODE_TYPEDEF:
      return unpack_long (check_typedef (type), valaddr);
    case TYPE_CODE_ENUM:
    case TYPE_CODE_FLAGS:
    case TYPE_CODE_BOOL:
    case TYPE_CODE_INT:
    case TYPE_CODE_CHAR:
    case TYPE_CODE_RANGE:
    case TYPE_CODE_MEMBERPTR:
      if (nosign)
	return extract_unsigned_integer (valaddr, len, byte_order);
      else
	return extract_signed_integer (valaddr, len, byte_order);

    case TYPE_CODE_FLT:
      return extract_typed_floating (valaddr, type);

    case TYPE_CODE_DECFLOAT:
      /* libdecnumber has a function to convert from decimal to integer, but
	 it doesn't work when the decimal number has a fractional part.  */
      return decimal_to_doublest (valaddr, len, byte_order);

    case TYPE_CODE_PTR:
    case TYPE_CODE_REF:
      /* Assume a CORE_ADDR can fit in a LONGEST (for now).  Not sure
         whether we want this to be true eventually.  */
      return extract_typed_address (valaddr, type);

    default:
      error (_("Value can't be converted to integer."));
    }
  return 0;			/* Placate lint.  */
}

/* Return a double value from the specified type and address.
   INVP points to an int which is set to 0 for valid value,
   1 for invalid value (bad float format).  In either case,
   the returned double is OK to use.  Argument is in target
   format, result is in host format.  */

DOUBLEST
unpack_double (struct type *type, const gdb_byte *valaddr, int *invp)
{
  enum bfd_endian byte_order = gdbarch_byte_order (get_type_arch (type));
  enum type_code code;
  int len;
  int nosign;

  *invp = 0;			/* Assume valid.   */
  CHECK_TYPEDEF (type);
  code = TYPE_CODE (type);
  len = TYPE_LENGTH (type);
  nosign = TYPE_UNSIGNED (type);
  if (code == TYPE_CODE_FLT)
    {
      /* NOTE: cagney/2002-02-19: There was a test here to see if the
	 floating-point value was valid (using the macro
	 INVALID_FLOAT).  That test/macro have been removed.

	 It turns out that only the VAX defined this macro and then
	 only in a non-portable way.  Fixing the portability problem
	 wouldn't help since the VAX floating-point code is also badly
	 bit-rotten.  The target needs to add definitions for the
	 methods gdbarch_float_format and gdbarch_double_format - these
	 exactly describe the target floating-point format.  The
	 problem here is that the corresponding floatformat_vax_f and
	 floatformat_vax_d values these methods should be set to are
	 also not defined either.  Oops!

         Hopefully someone will add both the missing floatformat
         definitions and the new cases for floatformat_is_valid ().  */

      if (!floatformat_is_valid (floatformat_from_type (type), valaddr))
	{
	  *invp = 1;
	  return 0.0;
	}

      return extract_typed_floating (valaddr, type);
    }
  else if (code == TYPE_CODE_DECFLOAT)
    return decimal_to_doublest (valaddr, len, byte_order);
  else if (nosign)
    {
      /* Unsigned -- be sure we compensate for signed LONGEST.  */
      return (ULONGEST) unpack_long (type, valaddr);
    }
  else
    {
      /* Signed -- we are OK with unpack_long.  */
      return unpack_long (type, valaddr);
    }
}

/* Unpack raw data (copied from debugee, target byte order) at VALADDR
   as a CORE_ADDR, assuming the raw data is described by type TYPE.
   We don't assume any alignment for the raw data.  Return value is in
   host byte order.

   If you want functions and arrays to be coerced to pointers, and
   references to be dereferenced, call value_as_address() instead.

   C++: It is assumed that the front-end has taken care of
   all matters concerning pointers to members.  A pointer
   to member which reaches here is considered to be equivalent
   to an INT (or some size).  After all, it is only an offset.  */

CORE_ADDR
unpack_pointer (struct type *type, const gdb_byte *valaddr)
{
  /* Assume a CORE_ADDR can fit in a LONGEST (for now).  Not sure
     whether we want this to be true eventually.  */
  return unpack_long (type, valaddr);
}


/* Get the value of the FIELDNO'th field (which must be static) of
   TYPE.  Return NULL if the field doesn't exist or has been
   optimized out. */

struct value *
value_static_field (struct type *type, int fieldno)
{
  struct value *retval;

  switch (TYPE_FIELD_LOC_KIND (type, fieldno))
    {
    case FIELD_LOC_KIND_PHYSADDR:
      retval = value_at_lazy (TYPE_FIELD_TYPE (type, fieldno),
			      TYPE_FIELD_STATIC_PHYSADDR (type, fieldno));
      break;
    case FIELD_LOC_KIND_PHYSNAME:
    {
      char *phys_name = TYPE_FIELD_STATIC_PHYSNAME (type, fieldno);
      /*TYPE_FIELD_NAME (type, fieldno);*/
      struct symbol *sym = lookup_symbol (phys_name, 0, VAR_DOMAIN, 0);

      if (sym == NULL)
	{
	  /* With some compilers, e.g. HP aCC, static data members are
	     reported as non-debuggable symbols */
	  struct minimal_symbol *msym = lookup_minimal_symbol (phys_name,
							       NULL, NULL);

	  if (!msym)
	    return NULL;
	  else
	    {
	      retval = value_at_lazy (TYPE_FIELD_TYPE (type, fieldno),
				      SYMBOL_VALUE_ADDRESS (msym));
	    }
	}
      else
	{
	  /* SYM should never have a SYMBOL_CLASS which will require
	     read_var_value to use the FRAME parameter.  */
	  if (symbol_read_needs_frame (sym))
	    warning (_("static field's value depends on the current "
		     "frame - bad debug info?"));
	  retval = read_var_value (sym, NULL);
 	}
      if (retval && VALUE_LVAL (retval) == lval_memory)
	SET_FIELD_PHYSADDR (TYPE_FIELD (type, fieldno),
			    value_address (retval));
      break;
    }
    default:
      gdb_assert (0);
    }

  return retval;
}

/* Change the enclosing type of a value object VAL to NEW_ENCL_TYPE.  
   You have to be careful here, since the size of the data area for the value 
   is set by the length of the enclosing type.  So if NEW_ENCL_TYPE is bigger 
   than the old enclosing type, you have to allocate more space for the data.  
   The return value is a pointer to the new version of this value structure. */

struct value *
value_change_enclosing_type (struct value *val, struct type *new_encl_type)
{
  if (TYPE_LENGTH (new_encl_type) > TYPE_LENGTH (value_enclosing_type (val))) 
    val->contents =
      (gdb_byte *) xrealloc (val->contents, TYPE_LENGTH (new_encl_type));

  val->enclosing_type = new_encl_type;
  return val;
}

/* Given a value ARG1 (offset by OFFSET bytes)
   of a struct or union type ARG_TYPE,
   extract and return the value of one of its (non-static) fields.
   FIELDNO says which field. */

struct value *
value_primitive_field (struct value *arg1, int offset,
		       int fieldno, struct type *arg_type)
{
  struct value *v;
  struct type *type;

  CHECK_TYPEDEF (arg_type);
  type = TYPE_FIELD_TYPE (arg_type, fieldno);

  /* Call check_typedef on our type to make sure that, if TYPE
     is a TYPE_CODE_TYPEDEF, its length is set to the length
     of the target type instead of zero.  However, we do not
     replace the typedef type by the target type, because we want
     to keep the typedef in order to be able to print the type
     description correctly.  */
  check_typedef (type);

  /* Handle packed fields */

  if (TYPE_FIELD_BITSIZE (arg_type, fieldno))
    {
      /* Create a new value for the bitfield, with bitpos and bitsize
	 set.  If possible, arrange offset and bitpos so that we can
	 do a single aligned read of the size of the containing type.
	 Otherwise, adjust offset to the byte containing the first
	 bit.  Assume that the address, offset, and embedded offset
	 are sufficiently aligned.  */
      int bitpos = TYPE_FIELD_BITPOS (arg_type, fieldno);
      int container_bitsize = TYPE_LENGTH (type) * 8;

      v = allocate_value_lazy (type);
      v->bitsize = TYPE_FIELD_BITSIZE (arg_type, fieldno);
      if ((bitpos % container_bitsize) + v->bitsize <= container_bitsize
	  && TYPE_LENGTH (type) <= (int) sizeof (LONGEST))
	v->bitpos = bitpos % container_bitsize;
      else
	v->bitpos = bitpos % 8;
      v->offset = value_embedded_offset (arg1)
	+ (bitpos - v->bitpos) / 8;
      v->parent = arg1;
      value_incref (v->parent);
      if (!value_lazy (arg1))
	value_fetch_lazy (v);
    }
  else if (fieldno < TYPE_N_BASECLASSES (arg_type))
    {
      /* This field is actually a base subobject, so preserve the
         entire object's contents for later references to virtual
         bases, etc.  */

      /* Lazy register values with offsets are not supported.  */
      if (VALUE_LVAL (arg1) == lval_register && value_lazy (arg1))
	value_fetch_lazy (arg1);

      if (value_lazy (arg1))
	v = allocate_value_lazy (value_enclosing_type (arg1));
      else
	{
	  v = allocate_value (value_enclosing_type (arg1));
	  memcpy (value_contents_all_raw (v), value_contents_all_raw (arg1),
		  TYPE_LENGTH (value_enclosing_type (arg1)));
	}
      v->type = type;
      v->offset = value_offset (arg1);
      v->embedded_offset = (offset + value_embedded_offset (arg1)
			    + TYPE_FIELD_BITPOS (arg_type, fieldno) / 8);
    }
  else
    {
      /* Plain old data member */
      offset += TYPE_FIELD_BITPOS (arg_type, fieldno) / 8;

      /* Lazy register values with offsets are not supported.  */
      if (VALUE_LVAL (arg1) == lval_register && value_lazy (arg1))
	value_fetch_lazy (arg1);

      if (value_lazy (arg1))
	v = allocate_value_lazy (type);
      else
	{
	  v = allocate_value (type);
	  memcpy (value_contents_raw (v),
		  value_contents_raw (arg1) + offset,
		  TYPE_LENGTH (type));
	}
      v->offset = (value_offset (arg1) + offset
		   + value_embedded_offset (arg1));
    }
  set_value_component_location (v, arg1);
  VALUE_REGNUM (v) = VALUE_REGNUM (arg1);
  VALUE_FRAME_ID (v) = VALUE_FRAME_ID (arg1);
  return v;
}

/* Given a value ARG1 of a struct or union type,
   extract and return the value of one of its (non-static) fields.
   FIELDNO says which field. */

struct value *
value_field (struct value *arg1, int fieldno)
{
  return value_primitive_field (arg1, 0, fieldno, value_type (arg1));
}

/* Return a non-virtual function as a value.
   F is the list of member functions which contains the desired method.
   J is an index into F which provides the desired method.

   We only use the symbol for its address, so be happy with either a
   full symbol or a minimal symbol.
 */

struct value *
value_fn_field (struct value **arg1p, struct fn_field *f, int j, struct type *type,
		int offset)
{
  struct value *v;
  struct type *ftype = TYPE_FN_FIELD_TYPE (f, j);
  char *physname = TYPE_FN_FIELD_PHYSNAME (f, j);
  struct symbol *sym;
  struct minimal_symbol *msym;

  sym = lookup_symbol (physname, 0, VAR_DOMAIN, 0);
  if (sym != NULL)
    {
      msym = NULL;
    }
  else
    {
      gdb_assert (sym == NULL);
      msym = lookup_minimal_symbol (physname, NULL, NULL);
      if (msym == NULL)
	return NULL;
    }

  v = allocate_value (ftype);
  if (sym)
    {
      set_value_address (v, BLOCK_START (SYMBOL_BLOCK_VALUE (sym)));
    }
  else
    {
      /* The minimal symbol might point to a function descriptor;
	 resolve it to the actual code address instead.  */
      struct objfile *objfile = msymbol_objfile (msym);
      struct gdbarch *gdbarch = get_objfile_arch (objfile);

      set_value_address (v,
	gdbarch_convert_from_func_ptr_addr
	   (gdbarch, SYMBOL_VALUE_ADDRESS (msym), &current_target));
    }

  if (arg1p)
    {
      if (type != value_type (*arg1p))
	*arg1p = value_ind (value_cast (lookup_pointer_type (type),
					value_addr (*arg1p)));

      /* Move the `this' pointer according to the offset.
         VALUE_OFFSET (*arg1p) += offset;
       */
    }

  return v;
}


/* Unpack a bitfield of the specified FIELD_TYPE, from the anonymous
   object at VALADDR.  The bitfield starts at BITPOS bits and contains
   BITSIZE bits.

   Extracting bits depends on endianness of the machine.  Compute the
   number of least significant bits to discard.  For big endian machines,
   we compute the total number of bits in the anonymous object, subtract
   off the bit count from the MSB of the object to the MSB of the
   bitfield, then the size of the bitfield, which leaves the LSB discard
   count.  For little endian machines, the discard count is simply the
   number of bits from the LSB of the anonymous object to the LSB of the
   bitfield.

   If the field is signed, we also do sign extension. */

LONGEST
unpack_bits_as_long (struct type *field_type, const gdb_byte *valaddr,
		     int bitpos, int bitsize)
{
  enum bfd_endian byte_order = gdbarch_byte_order (get_type_arch (field_type));
  ULONGEST val;
  ULONGEST valmask;
  int lsbcount;
  int bytes_read;

  /* Read the minimum number of bytes required; there may not be
     enough bytes to read an entire ULONGEST.  */
  CHECK_TYPEDEF (field_type);
  if (bitsize)
    bytes_read = ((bitpos % 8) + bitsize + 7) / 8;
  else
    bytes_read = TYPE_LENGTH (field_type);

  val = extract_unsigned_integer (valaddr + bitpos / 8,
				  bytes_read, byte_order);

  /* Extract bits.  See comment above. */

  if (gdbarch_bits_big_endian (get_type_arch (field_type)))
    lsbcount = (bytes_read * 8 - bitpos % 8 - bitsize);
  else
    lsbcount = (bitpos % 8);
  val >>= lsbcount;

  /* If the field does not entirely fill a LONGEST, then zero the sign bits.
     If the field is signed, and is negative, then sign extend. */

  if ((bitsize > 0) && (bitsize < 8 * (int) sizeof (val)))
    {
      valmask = (((ULONGEST) 1) << bitsize) - 1;
      val &= valmask;
      if (!TYPE_UNSIGNED (field_type))
	{
	  if (val & (valmask ^ (valmask >> 1)))
	    {
	      val |= ~valmask;
	    }
	}
    }
  return (val);
}

/* Unpack a field FIELDNO of the specified TYPE, from the anonymous object at
   VALADDR.  See unpack_bits_as_long for more details.  */

LONGEST
unpack_field_as_long (struct type *type, const gdb_byte *valaddr, int fieldno)
{
  int bitpos = TYPE_FIELD_BITPOS (type, fieldno);
  int bitsize = TYPE_FIELD_BITSIZE (type, fieldno);
  struct type *field_type = TYPE_FIELD_TYPE (type, fieldno);

  return unpack_bits_as_long (field_type, valaddr, bitpos, bitsize);
}

/* Modify the value of a bitfield.  ADDR points to a block of memory in
   target byte order; the bitfield starts in the byte pointed to.  FIELDVAL
   is the desired value of the field, in host byte order.  BITPOS and BITSIZE
   indicate which bits (in target bit order) comprise the bitfield.  
   Requires 0 < BITSIZE <= lbits, 0 <= BITPOS+BITSIZE <= lbits, and
   0 <= BITPOS, where lbits is the size of a LONGEST in bits.  */

void
modify_field (struct type *type, gdb_byte *addr,
	      LONGEST fieldval, int bitpos, int bitsize)
{
  enum bfd_endian byte_order = gdbarch_byte_order (get_type_arch (type));
  ULONGEST oword;
  ULONGEST mask = (ULONGEST) -1 >> (8 * sizeof (ULONGEST) - bitsize);

  /* If a negative fieldval fits in the field in question, chop
     off the sign extension bits.  */
  if ((~fieldval & ~(mask >> 1)) == 0)
    fieldval &= mask;

  /* Warn if value is too big to fit in the field in question.  */
  if (0 != (fieldval & ~mask))
    {
      /* FIXME: would like to include fieldval in the message, but
         we don't have a sprintf_longest.  */
      warning (_("Value does not fit in %d bits."), bitsize);

      /* Truncate it, otherwise adjoining fields may be corrupted.  */
      fieldval &= mask;
    }

  oword = extract_unsigned_integer (addr, sizeof oword, byte_order);

  /* Shifting for bit field depends on endianness of the target machine.  */
  if (gdbarch_bits_big_endian (get_type_arch (type)))
    bitpos = sizeof (oword) * 8 - bitpos - bitsize;

  oword &= ~(mask << bitpos);
  oword |= fieldval << bitpos;

  store_unsigned_integer (addr, sizeof oword, byte_order, oword);
}

/* Pack NUM into BUF using a target format of TYPE.  */

void
pack_long (gdb_byte *buf, struct type *type, LONGEST num)
{
  enum bfd_endian byte_order = gdbarch_byte_order (get_type_arch (type));
  int len;

  type = check_typedef (type);
  len = TYPE_LENGTH (type);

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_INT:
    case TYPE_CODE_CHAR:
    case TYPE_CODE_ENUM:
    case TYPE_CODE_FLAGS:
    case TYPE_CODE_BOOL:
    case TYPE_CODE_RANGE:
    case TYPE_CODE_MEMBERPTR:
      store_signed_integer (buf, len, byte_order, num);
      break;

    case TYPE_CODE_REF:
    case TYPE_CODE_PTR:
      store_typed_address (buf, type, (CORE_ADDR) num);
      break;

    default:
      error (_("Unexpected type (%d) encountered for integer constant."),
	     TYPE_CODE (type));
    }
}


/* Pack NUM into BUF using a target format of TYPE.  */

void
pack_unsigned_long (gdb_byte *buf, struct type *type, ULONGEST num)
{
  int len;
  enum bfd_endian byte_order;

  type = check_typedef (type);
  len = TYPE_LENGTH (type);
  byte_order = gdbarch_byte_order (get_type_arch (type));

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_INT:
    case TYPE_CODE_CHAR:
    case TYPE_CODE_ENUM:
    case TYPE_CODE_FLAGS:
    case TYPE_CODE_BOOL:
    case TYPE_CODE_RANGE:
    case TYPE_CODE_MEMBERPTR:
      store_unsigned_integer (buf, len, byte_order, num);
      break;

    case TYPE_CODE_REF:
    case TYPE_CODE_PTR:
      store_typed_address (buf, type, (CORE_ADDR) num);
      break;

    default:
      error (_("\
Unexpected type (%d) encountered for unsigned integer constant."),
	     TYPE_CODE (type));
    }
}


/* Convert C numbers into newly allocated values.  */

struct value *
value_from_longest (struct type *type, LONGEST num)
{
  struct value *val = allocate_value (type);

  pack_long (value_contents_raw (val), type, num);
  return val;
}


/* Convert C unsigned numbers into newly allocated values.  */

struct value *
value_from_ulongest (struct type *type, ULONGEST num)
{
  struct value *val = allocate_value (type);

  pack_unsigned_long (value_contents_raw (val), type, num);

  return val;
}


/* Create a value representing a pointer of type TYPE to the address
   ADDR.  */
struct value *
value_from_pointer (struct type *type, CORE_ADDR addr)
{
  struct value *val = allocate_value (type);

  store_typed_address (value_contents_raw (val), check_typedef (type), addr);
  return val;
}


/* Create a value of type TYPE whose contents come from VALADDR, if it
   is non-null, and whose memory address (in the inferior) is
   ADDRESS.  */

struct value *
value_from_contents_and_address (struct type *type,
				 const gdb_byte *valaddr,
				 CORE_ADDR address)
{
  struct value *v = allocate_value (type);

  if (valaddr == NULL)
    set_value_lazy (v, 1);
  else
    memcpy (value_contents_raw (v), valaddr, TYPE_LENGTH (type));
  set_value_address (v, address);
  VALUE_LVAL (v) = lval_memory;
  return v;
}

struct value *
value_from_double (struct type *type, DOUBLEST num)
{
  struct value *val = allocate_value (type);
  struct type *base_type = check_typedef (type);
  enum type_code code = TYPE_CODE (base_type);

  if (code == TYPE_CODE_FLT)
    {
      store_typed_floating (value_contents_raw (val), base_type, num);
    }
  else
    error (_("Unexpected type encountered for floating constant."));

  return val;
}

struct value *
value_from_decfloat (struct type *type, const gdb_byte *dec)
{
  struct value *val = allocate_value (type);

  memcpy (value_contents_raw (val), dec, TYPE_LENGTH (type));
  return val;
}

struct value *
coerce_ref (struct value *arg)
{
  struct type *value_type_arg_tmp = check_typedef (value_type (arg));

  if (TYPE_CODE (value_type_arg_tmp) == TYPE_CODE_REF)
    arg = value_at_lazy (TYPE_TARGET_TYPE (value_type_arg_tmp),
			 unpack_pointer (value_type (arg),		
					 value_contents (arg)));
  return arg;
}

struct value *
coerce_array (struct value *arg)
{
  struct type *type;

  arg = coerce_ref (arg);
  type = check_typedef (value_type (arg));

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_ARRAY:
      if (current_language->c_style_arrays)
	arg = value_coerce_array (arg);
      break;
    case TYPE_CODE_FUNC:
      arg = value_coerce_function (arg);
      break;
    }
  return arg;
}


/* Return true if the function returning the specified type is using
   the convention of returning structures in memory (passing in the
   address as a hidden first parameter).  */

int
using_struct_return (struct gdbarch *gdbarch,
		     struct type *func_type, struct type *value_type)
{
  enum type_code code = TYPE_CODE (value_type);

  if (code == TYPE_CODE_ERROR)
    error (_("Function return type unknown."));

  if (code == TYPE_CODE_VOID)
    /* A void return value is never in memory.  See also corresponding
       code in "print_return_value".  */
    return 0;

  /* Probe the architecture for the return-value convention.  */
  return (gdbarch_return_value (gdbarch, func_type, value_type,
				NULL, NULL, NULL)
	  != RETURN_VALUE_REGISTER_CONVENTION);
}

/* Set the initialized field in a value struct.  */

void
set_value_initialized (struct value *val, int status)
{
  val->initialized = status;
}

/* Return the initialized field in a value struct.  */

int
value_initialized (struct value *val)
{
  return val->initialized;
}

void
_initialize_values (void)
{
  add_cmd ("convenience", no_class, show_convenience, _("\
Debugger convenience (\"$foo\") variables.\n\
These variables are created when you assign them values;\n\
thus, \"print $foo=1\" gives \"$foo\" the value 1.  Values may be any type.\n\
\n\
A few convenience variables are given values automatically:\n\
\"$_\"holds the last address examined with \"x\" or \"info lines\",\n\
\"$__\" holds the contents of the last address examined with \"x\"."),
	   &showlist);

  add_cmd ("values", no_class, show_values,
	   _("Elements of value history around item number IDX (or last ten)."),
	   &showlist);

  add_com ("init-if-undefined", class_vars, init_if_undefined_command, _("\
Initialize a convenience variable if necessary.\n\
init-if-undefined VARIABLE = EXPRESSION\n\
Set an internal VARIABLE to the result of the EXPRESSION if it does not\n\
exist or does not contain a value.  The EXPRESSION is not evaluated if the\n\
VARIABLE is already initialized."));

  add_prefix_cmd ("function", no_class, function_command, _("\
Placeholder command for showing help on convenience functions."),
		  &functionlist, "function ", 0, &cmdlist);
}
