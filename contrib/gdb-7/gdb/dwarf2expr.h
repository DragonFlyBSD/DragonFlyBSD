/* DWARF 2 Expression Evaluator.

   Copyright (C) 2001, 2002, 2003, 2005, 2007, 2008, 2009
   Free Software Foundation, Inc.

   Contributed by Daniel Berlin <dan@dberlin.org>.

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

#if !defined (DWARF2EXPR_H)
#define DWARF2EXPR_H

/* The location of a value.  */
enum dwarf_value_location
{
  /* The piece is in memory.
     The value on the dwarf stack is its address.  */
  DWARF_VALUE_MEMORY,

  /* The piece is in a register.
     The value on the dwarf stack is the register number.  */
  DWARF_VALUE_REGISTER,

  /* The piece is on the dwarf stack.  */
  DWARF_VALUE_STACK,

  /* The piece is a literal.  */
  DWARF_VALUE_LITERAL
};

/* The dwarf expression stack.  */

struct dwarf_stack_value
{
  CORE_ADDR value;

  /* Non-zero if the piece is in memory and is known to be
     on the program's stack.  It is always ok to set this to zero.
     This is used, for example, to optimize memory access from the target.
     It can vastly speed up backtraces on long latency connections when
     "set stack-cache on".  */
  int in_stack_memory;
};

/* The expression evaluator works with a dwarf_expr_context, describing
   its current state and its callbacks.  */
struct dwarf_expr_context
{
  /* The stack of values, allocated with xmalloc.  */
  struct dwarf_stack_value *stack;

  /* The number of values currently pushed on the stack, and the
     number of elements allocated to the stack.  */
  int stack_len, stack_allocated;

  /* Target architecture to use for address operations.  */
  struct gdbarch *gdbarch;

  /* Target address size in bytes.  */
  int addr_size;

  /* An opaque argument provided by the caller, which will be passed
     to all of the callback functions.  */
  void *baton;

  /* Return the value of register number REGNUM.  */
  CORE_ADDR (*read_reg) (void *baton, int regnum);

  /* Read LENGTH bytes at ADDR into BUF.  */
  void (*read_mem) (void *baton, gdb_byte *buf, CORE_ADDR addr, size_t length);

  /* Return the location expression for the frame base attribute, in
     START and LENGTH.  The result must be live until the current
     expression evaluation is complete.  */
  void (*get_frame_base) (void *baton, gdb_byte **start, size_t *length);

  /* Return the CFA for the frame.  */
  CORE_ADDR (*get_frame_cfa) (void *baton);

  /* Return the thread-local storage address for
     DW_OP_GNU_push_tls_address.  */
  CORE_ADDR (*get_tls_address) (void *baton, CORE_ADDR offset);

#if 0
  /* Not yet implemented.  */

  /* Return the location expression for the dwarf expression
     subroutine in the die at OFFSET in the current compilation unit.
     The result must be live until the current expression evaluation
     is complete.  */
  unsigned char *(*get_subr) (void *baton, off_t offset, size_t *length);

  /* Return the `object address' for DW_OP_push_object_address.  */
  CORE_ADDR (*get_object_address) (void *baton);
#endif

  /* The current depth of dwarf expression recursion, via DW_OP_call*,
     DW_OP_fbreg, DW_OP_push_object_address, etc., and the maximum
     depth we'll tolerate before raising an error.  */
  int recursion_depth, max_recursion_depth;

  /* Location of the value.  */
  enum dwarf_value_location location;

  /* For VALUE_LITERAL, a the current literal value's length and
     data.  */
  ULONGEST len;
  gdb_byte *data;

  /* Initialization status of variable: Non-zero if variable has been
     initialized; zero otherwise.  */
  int initialized;

  /* An array of pieces.  PIECES points to its first element;
     NUM_PIECES is its length.

     Each time DW_OP_piece is executed, we add a new element to the
     end of this array, recording the current top of the stack, the
     current location, and the size given as the operand to
     DW_OP_piece.  We then pop the top value from the stack, reset the
     location, and resume evaluation.

     The Dwarf spec doesn't say whether DW_OP_piece pops the top value
     from the stack.  We do, ensuring that clients of this interface
     expecting to see a value left on the top of the stack (say, code
     evaluating frame base expressions or CFA's specified with
     DW_CFA_def_cfa_expression) will get an error if the expression
     actually marks all the values it computes as pieces.

     If an expression never uses DW_OP_piece, num_pieces will be zero.
     (It would be nice to present these cases as expressions yielding
     a single piece, so that callers need not distinguish between the
     no-DW_OP_piece and one-DW_OP_piece cases.  But expressions with
     no DW_OP_piece operations have no value to place in a piece's
     'size' field; the size comes from the surrounding data.  So the
     two cases need to be handled separately.)  */
  int num_pieces;
  struct dwarf_expr_piece *pieces;
};


/* A piece of an object, as recorded by DW_OP_piece.  */
struct dwarf_expr_piece
{
  enum dwarf_value_location location;

  union
  {
    struct
    {
      /* This piece's address or register number.  */
      CORE_ADDR value;
      /* Non-zero if the piece is known to be in memory and on
	 the program's stack.  */
      int in_stack_memory;
    } expr;

    struct
    {
      /* A pointer to the data making up this piece, for literal
	 pieces.  */
      gdb_byte *data;
      /* The length of the available data.  */
      ULONGEST length;
    } literal;
  } v;

  /* The length of the piece, in bytes.  */
  ULONGEST size;
};

struct dwarf_expr_context *new_dwarf_expr_context (void);
void free_dwarf_expr_context (struct dwarf_expr_context *ctx);
struct cleanup *
    make_cleanup_free_dwarf_expr_context (struct dwarf_expr_context *ctx);

void dwarf_expr_push (struct dwarf_expr_context *ctx, CORE_ADDR value,
		      int in_stack_memory);
void dwarf_expr_pop (struct dwarf_expr_context *ctx);
void dwarf_expr_eval (struct dwarf_expr_context *ctx, unsigned char *addr,
		      size_t len);
CORE_ADDR dwarf_expr_fetch (struct dwarf_expr_context *ctx, int n);
int dwarf_expr_fetch_in_stack_memory (struct dwarf_expr_context *ctx, int n);


gdb_byte *read_uleb128 (gdb_byte *buf, gdb_byte *buf_end, ULONGEST * r);
gdb_byte *read_sleb128 (gdb_byte *buf, gdb_byte *buf_end, LONGEST * r);
CORE_ADDR dwarf2_read_address (struct gdbarch *gdbarch, gdb_byte *buf,
			       gdb_byte *buf_end, int addr_size);

#endif /* dwarf2expr.h */
