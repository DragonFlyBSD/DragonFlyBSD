/* Scheme/Guile language support routines for GDB, the GNU debugger.

   Copyright (C) 1995, 1996, 1998, 1999, 2000, 2001, 2005, 2007, 2008, 2009,
   2010 Free Software Foundation, Inc.

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
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "parser-defs.h"
#include "language.h"
#include "value.h"
#include "scm-lang.h"
#include "valprint.h"
#include "gdbcore.h"
#include "c-lang.h"
#include "infcall.h"
#include "objfiles.h"

static void scm_ipruk (char *, struct type *, LONGEST, struct ui_file *);
static void scm_scmval_print (struct type *, LONGEST, struct ui_file *,
			      int, const struct value_print_options *);
static void scm_scmlist_print (struct type *, LONGEST, struct ui_file *,
			       int, const struct value_print_options *);
static int scm_inferior_print (struct type *, LONGEST, struct ui_file *,
			       int, const struct value_print_options *);

/* Prints the SCM value VALUE by invoking the inferior, if appropraite.
   Returns >= 0 on success;  return -1 if the inferior cannot/should not
   print VALUE. */

static int
scm_inferior_print (struct type *type, LONGEST value, struct ui_file *stream,
		    int recurse, const struct value_print_options *options)
{
  struct value *func, *arg, *result;
  struct symbol *gdb_output_sym, *gdb_output_len_sym;
  char *output;
  int ret, output_len;

  func = find_function_in_inferior ("gdb_print", NULL);
  arg = value_from_longest (type, value);

  result = call_function_by_hand (func, 1, &arg);
  ret = (int) value_as_long (result);
  if (ret == 0)
    {
      /* XXX: Should we cache these symbols?  */
      gdb_output_sym =
	lookup_symbol_global ("gdb_output", NULL, VAR_DOMAIN);
      gdb_output_len_sym =
	lookup_symbol_global ("gdb_output_length", NULL, VAR_DOMAIN);

      if ((gdb_output_sym == NULL) || (gdb_output_len_sym == NULL))
	ret = -1;
      else
	{
	  struct value *remote_buffer;

	  read_memory (SYMBOL_VALUE_ADDRESS (gdb_output_len_sym),
		       (char *) &output_len, sizeof (output_len));

	  output = (char *) alloca (output_len);
	  remote_buffer = value_at (type,
				    SYMBOL_VALUE_ADDRESS (gdb_output_sym));
	  read_memory (value_as_address (remote_buffer),
		       output, output_len);

	  ui_file_write (stream, output, output_len);
	}
    }

  return ret;
}

/* {Names of immediate symbols}
 * This table must agree with the declarations in scm.h: {Immediate Symbols}.*/

static char *scm_isymnames[] =
{
  /* This table must agree with the declarations */
  "and",
  "begin",
  "case",
  "cond",
  "do",
  "if",
  "lambda",
  "let",
  "let*",
  "letrec",
  "or",
  "quote",
  "set!",
  "define",
#if 0
  "literal-variable-ref",
  "literal-variable-set!",
#endif
  "apply",
  "call-with-current-continuation",

 /* user visible ISYMS */
 /* other keywords */
 /* Flags */

  "#f",
  "#t",
  "#<undefined>",
  "#<eof>",
  "()",
  "#<unspecified>"
};

static void
scm_scmlist_print (struct type *type, LONGEST svalue,
		   struct ui_file *stream, int recurse,
		   const struct value_print_options *options)
{
#define SCM_SIZE (TYPE_LENGTH (type))
#define SCM_BYTE_ORDER (gdbarch_byte_order (get_type_arch (type)))
  unsigned int more = options->print_max;

  if (recurse > 6)
    {
      fputs_filtered ("...", stream);
      return;
    }
  scm_scmval_print (type, SCM_CAR (svalue), stream, recurse + 1, options);
  svalue = SCM_CDR (svalue);
  for (; SCM_NIMP (svalue); svalue = SCM_CDR (svalue))
    {
      if (SCM_NECONSP (svalue))
	break;
      fputs_filtered (" ", stream);
      if (--more == 0)
	{
	  fputs_filtered ("...", stream);
	  return;
	}
      scm_scmval_print (type, SCM_CAR (svalue), stream, recurse + 1, options);
    }
  if (SCM_NNULLP (svalue))
    {
      fputs_filtered (" . ", stream);
      scm_scmval_print (type, svalue, stream, recurse + 1, options);
    }
#undef SCM_BYTE_ORDER
#undef SCM_SIZE
}

static void
scm_ipruk (char *hdr, struct type *type, LONGEST ptr,
	   struct ui_file *stream)
{
#define SCM_SIZE (TYPE_LENGTH (type))
#define SCM_BYTE_ORDER (gdbarch_byte_order (get_type_arch (type)))
  fprintf_filtered (stream, "#<unknown-%s", hdr);
  if (SCM_CELLP (ptr))
    fprintf_filtered (stream, " (0x%lx . 0x%lx) @",
		      (long) SCM_CAR (ptr), (long) SCM_CDR (ptr));
  fprintf_filtered (stream, " 0x%s>", phex_nz (ptr, SCM_SIZE));
#undef SCM_BYTE_ORDER
#undef SCM_SIZE
}

static void
scm_scmval_print (struct type *type, LONGEST svalue,
		  struct ui_file *stream, int recurse,
		  const struct value_print_options *options)
{
  struct gdbarch *gdbarch = get_type_arch (type);

#define SCM_SIZE (TYPE_LENGTH (type))
#define SCM_BYTE_ORDER (gdbarch_byte_order (gdbarch))
taloop:
  switch (7 & (int) svalue)
    {
    case 2:
    case 6:
      print_longest (stream,
		     options->format ? options->format : 'd',
		     1, svalue >> 2);
      break;
    case 4:
      if (SCM_ICHRP (svalue))
	{
	  svalue = SCM_ICHR (svalue);
	  scm_printchar (svalue, builtin_type (gdbarch)->builtin_char,
			 stream);
	  break;
	}
      else if (SCM_IFLAGP (svalue)
	       && (SCM_ISYMNUM (svalue)
		   < (sizeof scm_isymnames / sizeof (char *))))
	{
	  fputs_filtered (SCM_ISYMCHARS (svalue), stream);
	  break;
	}
      else if (SCM_ILOCP (svalue))
	{
	  fprintf_filtered (stream, "#@%ld%c%ld",
			    (long) SCM_IFRAME (svalue),
			    SCM_ICDRP (svalue) ? '-' : '+',
			    (long) SCM_IDIST (svalue));
	  break;
	}
      else
	goto idef;
      break;
    case 1:
      /* gloc */
      svalue = SCM_CAR (svalue - 1);
      goto taloop;
    default:
    idef:
      scm_ipruk ("immediate", type, svalue, stream);
      break;
    case 0:

      switch (SCM_TYP7 (svalue))
	{
	case scm_tcs_cons_gloc:
	  if (SCM_CDR (SCM_CAR (svalue) - 1L) == 0)
	    {
#if 0
	      SCM name;
#endif

	      fputs_filtered ("#<latte ", stream);
#if 1
	      fputs_filtered ("???", stream);
#else
	      name = ((SCM n *) (STRUCT_TYPE (exp)))[struct_i_name];
	      scm_lfwrite (CHARS (name),
			   (sizet) sizeof (char),
			     (sizet) LENGTH (name),
			   port);
#endif
	      fprintf_filtered (stream, " #X%s>", phex_nz (svalue, SCM_SIZE));
	      break;
	    }
	case scm_tcs_cons_imcar:
	case scm_tcs_cons_nimcar:
	  fputs_filtered ("(", stream);
	  scm_scmlist_print (type, svalue, stream, recurse + 1, options);
	  fputs_filtered (")", stream);
	  break;
	case scm_tcs_closures:
	  fputs_filtered ("#<CLOSURE ", stream);
	  scm_scmlist_print (type, SCM_CODE (svalue), stream,
			     recurse + 1, options);
	  fputs_filtered (">", stream);
	  break;
	case scm_tc7_string:
	  {
	    int len = SCM_LENGTH (svalue);
	    CORE_ADDR addr = (CORE_ADDR) SCM_CDR (svalue);
	    int i;
	    int done = 0;
	    int buf_size;
	    gdb_byte buffer[64];
	    int truncate = options->print_max && len > (int) options->print_max;
	    if (truncate)
	      len = options->print_max;
	    fputs_filtered ("\"", stream);
	    for (; done < len; done += buf_size)
	      {
		buf_size = min (len - done, 64);
		read_memory (addr + done, buffer, buf_size);

		for (i = 0; i < buf_size; ++i)
		  switch (buffer[i])
		    {
		    case '\"':
		    case '\\':
		      fputs_filtered ("\\", stream);
		    default:
		      fprintf_filtered (stream, "%c", buffer[i]);
		    }
	      }
	    fputs_filtered (truncate ? "...\"" : "\"", stream);
	    break;
	  }
	  break;
	case scm_tcs_symbols:
	  {
	    int len = SCM_LENGTH (svalue);

	    char *str = alloca (len);
	    read_memory (SCM_CDR (svalue), (gdb_byte *) str, len + 1);
	    /* Should handle weird characters FIXME */
	    str[len] = '\0';
	    fputs_filtered (str, stream);
	    break;
	  }
	case scm_tc7_vector:
	  {
	    int len = SCM_LENGTH (svalue);
	    int i;
	    LONGEST elements = SCM_CDR (svalue);
	    LONGEST val;

	    fputs_filtered ("#(", stream);
	    for (i = 0; i < len; ++i)
	      {
		if (i > 0)
		  fputs_filtered (" ", stream);
		val = scm_get_field (elements, i, SCM_SIZE, SCM_BYTE_ORDER);
		scm_scmval_print (type, val, stream, recurse + 1, options);
	      }
	    fputs_filtered (")", stream);
	  }
	  break;
#if 0
	case tc7_lvector:
	  {
	    SCM result;
	    SCM hook;

	    hook = scm_get_lvector_hook (exp, LV_PRINT_FN);
	    if (hook == BOOL_F)
	      {
		scm_puts ("#<locked-vector ", port);
		scm_intprint (CDR (exp), 16, port);
		scm_puts (">", port);
	      }
	    else
	      {
		result
		  = scm_apply (hook,
			       scm_listify (exp, port, 
					    (writing ? BOOL_T : BOOL_F),
					    SCM_UNDEFINED),
			       EOL);
		if (result == BOOL_F)
		  goto punk;
	      }
	    break;
	  }
	  break;
	case tc7_bvect:
	case tc7_ivect:
	case tc7_uvect:
	case tc7_fvect:
	case tc7_dvect:
	case tc7_cvect:
	  scm_raprin1 (exp, port, writing);
	  break;
#endif
	case scm_tcs_subrs:
	  {
	    int index = SCM_CAR (svalue) >> 8;
#if 1
	    char str[20];
	    sprintf (str, "#%d", index);
#else
	    char *str = index ? SCM_CHARS (scm_heap_org + index) : "";
#define SCM_CHARS(x) ((char *)(SCM_CDR(x)))
	    char *str = CHARS (SNAME (exp));
#endif

	    fprintf_filtered (stream, "#<primitive-procedure %s>",
			      str);
	  }
	  break;
#if 0
#ifdef CCLO
	case tc7_cclo:
	  scm_puts ("#<compiled-closure ", port);
	  scm_iprin1 (CCLO_SUBR (exp), port, writing);
	  scm_putc ('>', port);
	  break;
#endif
	case tc7_contin:
	  fprintf_filtered (stream, "#<continuation %d @ #X%lx >",
			    LENGTH (svalue),
			    (long) CHARS (svalue));
	  break;
	case tc7_port:
	  i = PTOBNUM (exp);
	  if (i < scm_numptob 
	      && scm_ptobs[i].print 
	      && (scm_ptobs[i].print) (exp, port, writing))
	    break;
	  goto punk;
	case tc7_smob:
	  i = SMOBNUM (exp);
	  if (i < scm_numsmob && scm_smobs[i].print
	      && (scm_smobs[i].print) (exp, port, writing))
	    break;
	  goto punk;
#endif
	default:
#if 0
	punk:
#endif
	  scm_ipruk ("type", type, svalue, stream);
	}
      break;
    }
#undef SCM_BYTE_ORDER
#undef SCM_SIZE
}

int
scm_val_print (struct type *type, const gdb_byte *valaddr,
	       int embedded_offset, CORE_ADDR address,
	       struct ui_file *stream, int recurse,
	       const struct value *val,
	       const struct value_print_options *options)
{
  if (is_scmvalue_type (type)
      && value_bits_valid (val, TARGET_CHAR_BIT * embedded_offset,
			   TARGET_CHAR_BIT * TYPE_LENGTH (type)))
    {
      enum bfd_endian byte_order = gdbarch_byte_order (get_type_arch (type));
      LONGEST svalue
	= extract_signed_integer (valaddr, TYPE_LENGTH (type), byte_order);

      if (scm_inferior_print (type, svalue, stream, recurse, options) >= 0)
	{
	}
      else
	{
	  scm_scmval_print (type, svalue, stream, recurse, options);
	}

      gdb_flush (stream);
      return (0);
    }
  else
    {
      return c_val_print (type, valaddr, 0, address, stream, recurse,
			  val, options);
    }
}

int
scm_value_print (struct value *val, struct ui_file *stream,
		 const struct value_print_options *options)
{
  struct value_print_options opts = *options;

  opts.deref_ref = 1;
  return (common_val_print (val, stream, 0, &opts, current_language));
}
