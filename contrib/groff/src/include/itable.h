// -*- C++ -*-
/* Copyright (C) 1989, 1990, 1991, 1992, 2003, 2004, 2006, 2009
   Free Software Foundation, Inc.
     Written by James Clark (jjc@jclark.com)

This file is part of groff.

groff is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or
(at your option) any later version.

groff is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include <assert.h>

// name2(a,b) concatenates two C identifiers.
#ifdef TRADITIONAL_CPP
# define name2(a,b) a/**/b
#else /* not TRADITIONAL_CPP */
# define name2(a,b) name2x(a,b)
# define name2x(a,b) a ## b
#endif /* not TRADITIONAL_CPP */

// `class ITABLE(T)' is the type of a hash table mapping an integer (int >= 0)
// to an object of type T.
//
// `struct IASSOC(T)' is the type of a association (pair) between an integer
// (int >= 0) and an object of type T.
//
// `class ITABLE_ITERATOR(T)' is the type of an iterator iterating through a
// `class ITABLE(T)'.
//
// Nowadays one would use templates for this; this code predates the addition
// of templates to C++.
#define ITABLE(T) name2(T,_itable)
#define IASSOC(T) name2(T,_iassoc)
#define ITABLE_ITERATOR(T) name2(T,_itable_iterator)

// ptable.h declares this too
#ifndef NEXT_PTABLE_SIZE_DEFINED
# define NEXT_PTABLE_SIZE_DEFINED
extern unsigned next_ptable_size(unsigned);	// Return the first suitable
				// hash table size greater than the given
				// value.
#endif

// Declare the types `class ITABLE(T)', `struct IASSOC(T)', and `class
// ITABLE_ITERATOR(T)' for the type `T'.
#define declare_itable(T)						      \
									      \
struct IASSOC(T) {							      \
  int key;								      \
  T *val;								      \
  IASSOC(T)();								      \
};									      \
									      \
class ITABLE(T);							      \
									      \
class ITABLE_ITERATOR(T) {						      \
  ITABLE(T) *p;								      \
  unsigned i;								      \
public:									      \
  ITABLE_ITERATOR(T)(ITABLE(T) *);	/* Initialize an iterator running     \
					   through the given table.  */	      \
  int next(int *, T **);		/* Fetch the next pair, store the key \
					   and value in arg1 and arg2,	      \
					   respectively, and return 1.  If    \
					   there is no more pair in the	      \
					   table, return 0.  */		      \
};									      \
									      \
class ITABLE(T) {							      \
  IASSOC(T) *v;								      \
  unsigned size;							      \
  unsigned used;							      \
  enum {								      \
    FULL_NUM = 2,							      \
    FULL_DEN = 3,							      \
    INITIAL_SIZE = 17							      \
  };									      \
public:									      \
  ITABLE(T)();				/* Create an empty table.  */	      \
  ~ITABLE(T)();				/* Delete a table, including its      \
					   values.  */			      \
  void define(int, T *);		/* Define the value (arg2) for a key  \
					   (arg1).  */			      \
  T *lookup(int);			/* Return a pointer to the value of   \
					   the given key, if found in the     \
					   table, or NULL otherwise.  */      \
  friend class ITABLE_ITERATOR(T);					      \
};


// Values must be allocated by the caller (always using new[], not new)
// and are freed by ITABLE.

// Define the implementations of the members of the types `class ITABLE(T)',
// `struct IASSOC(T)', `class ITABLE_ITERATOR(T)' for the type `T'.
#define implement_itable(T)						      \
									      \
IASSOC(T)::IASSOC(T)()							      \
: key(-1), val(0)							      \
{									      \
}									      \
									      \
ITABLE(T)::ITABLE(T)()							      \
{									      \
  v = new IASSOC(T)[size = INITIAL_SIZE];				      \
  used = 0;								      \
}									      \
									      \
ITABLE(T)::~ITABLE(T)()							      \
{									      \
  for (unsigned i = 0; i < size; i++)					      \
    a_delete v[i].val;							      \
  a_delete v;								      \
}									      \
									      \
void ITABLE(T)::define(int key, T *val)					      \
{									      \
  assert(key >= 0);							      \
  unsigned int h = (unsigned int)(key);					      \
  unsigned n;								      \
  for (n = unsigned(h % size);						      \
       v[n].key >= 0;							      \
       n = (n == 0 ? size - 1 : n - 1))					      \
    if (v[n].key == key) {						      \
      a_delete v[n].val;						      \
      v[n].val = val;							      \
      return;								      \
    }									      \
  if (val == 0)								      \
    return;								      \
  if (used*FULL_DEN >= size*FULL_NUM) {					      \
    IASSOC(T) *oldv = v;						      \
    unsigned old_size = size;						      \
    size = next_ptable_size(size);					      \
    v = new IASSOC(T)[size];						      \
    for (unsigned i = 0; i < old_size; i++)				      \
      if (oldv[i].key >= 0) {						      \
	if (oldv[i].val != 0) {						      \
	  unsigned j;							      \
	  for (j = (unsigned int)(oldv[i].key) % size;			      \
	       v[j].key >= 0;						      \
	       j = (j == 0 ? size - 1 : j - 1))				      \
		 ;							      \
	  v[j].key = oldv[i].key;					      \
	  v[j].val = oldv[i].val;					      \
	}								      \
      }									      \
    for (n = unsigned(h % size);					      \
	 v[n].key >= 0;							      \
	 n = (n == 0 ? size - 1 : n - 1))				      \
      ;									      \
    a_delete oldv;							      \
  }									      \
  v[n].key = key;							      \
  v[n].val = val;							      \
  used++;								      \
}									      \
									      \
T *ITABLE(T)::lookup(int key)						      \
{									      \
  assert(key >= 0);							      \
  for (unsigned n = (unsigned int)key % size;				      \
       v[n].key >= 0;							      \
       n = (n == 0 ? size - 1 : n - 1))					      \
    if (v[n].key == key)						      \
      return v[n].val;							      \
  return 0;								      \
}									      \
									      \
ITABLE_ITERATOR(T)::ITABLE_ITERATOR(T)(ITABLE(T) *t)			      \
: p(t), i(0)								      \
{									      \
}									      \
									      \
int ITABLE_ITERATOR(T)::next(int *keyp, T **valp)			      \
{									      \
  unsigned size = p->size;						      \
  IASSOC(T) *v = p->v;							      \
  for (; i < size; i++)							      \
    if (v[i].key >= 0) {						      \
      *keyp = v[i].key;							      \
      *valp = v[i].val;							      \
      i++;								      \
      return 1;								      \
    }									      \
  return 0;								      \
}

// end of itable.h
