/* macro.h -- declarations for macro.c.
   $Id: macro.h,v 1.6 2007/07/01 21:20:32 karl Exp $

   Copyright (C) 1998, 1999, 2007 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef MACRO_H
#define MACRO_H

extern FILE *macro_expansion_output_stream;
extern char *macro_expansion_filename;
extern int me_executing_string;
extern int only_macro_expansion;

/* Here is a structure used to remember input text strings and offsets
   within them. */
typedef struct {
  char *pointer;                /* Pointer to the input text. */
  int offset;                   /* Offset of the last character output. */
} ITEXT;

/* Macro definitions for user-defined commands. */
typedef struct {
  char *name;                   /* Name of the macro. */
  char **arglist;               /* Args to replace when executing. */
  int argcount;                 /* Number of args in arglist */
  char *body;                   /* Macro body. */
  char *source_file;            /* File where this macro is defined. */
  int source_lineno;            /* Line number within FILENAME. */
  int inhibited;                /* Nonzero means make find_macro () fail. */
  int flags;                    /* ME_RECURSE, etc. */
} MACRO_DEF;

/* flags for MACRO_DEF */
#define ME_RECURSE      0x01

extern void execute_macro (MACRO_DEF *def);
extern MACRO_DEF *find_macro (char *name);
extern char *expand_macro (MACRO_DEF *def);

extern ITEXT *remember_itext (char *pointer, int offset);
extern void forget_itext (char *pointer);
extern void maybe_write_itext (char *pointer, int offset);
extern void write_region_to_macro_output (char *string, int start, int end);
extern void append_to_expansion_output (int offset);
extern void me_append_before_this_command (void);
extern void me_execute_string (char *execution_string);
extern void me_execute_string_keep_state (char *execution_string,
    char *append_string);

extern char *alias_expand (char *tok);
extern int enclosure_command (char *tok);
extern void enclosure_expand (int arg, int start, int end);

/* The @commands.  */
extern void cm_macro (void), cm_rmacro (void), cm_unmacro (void);
extern void cm_alias (void), cm_definfoenclose (void);

extern int array_len (char **array);
extern void free_array (char **array);

enum quote_type { quote_none, quote_single, quote_many };
extern char **get_brace_args (enum quote_type type);

#endif /* not MACRO_H */
