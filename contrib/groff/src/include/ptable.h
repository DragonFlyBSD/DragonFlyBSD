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
#include <string.h>

// name2(a,b) concatenates two C identifiers.
#ifdef TRADITIONAL_CPP
# define name2(a,b) a/**/b
#else /* not TRADITIONAL_CPP */
# define name2(a,b) name2x(a,b)
# define name2x(a,b) a ## b
#endif /* not TRADITIONAL_CPP */

// `class PTABLE(T)' is the type of a hash table mapping a string
// (const char *) to an object of type T.
//
// `struct PASSOC(T)' is the type of a association (pair) between a
// string (const char *) and an object of type T.
//
// `class PTABLE_ITERATOR(T)' is the type of an iterator iterating through a
// `class PTABLE(T)'.
//
// Nowadays one would use templates for this; this code predates the addition
// of templates to C++.
#define PTABLE(T) name2(T,_ptable)
#define PASSOC(T) name2(T,_passoc)
#define PTABLE_ITERATOR(T) name2(T,_ptable_iterator)

// itable.h declares this too
#ifndef NEXT_PTABLE_SIZE_DEFINED
# define NEXT_PTABLE_SIZE_DEFINED
extern unsigned next_ptable_size(unsigned);	// Return the first suitable
				// hash table size greater than the given
				// value.
#endif

extern unsigned long hash_string(const char *);	// Return a hash code of the
				// given string.  The hash function is
				// platform dependent.  */

// Declare the types `class PTABLE(T)', `struct PASSOC(T)', and `class
// PTABLE_ITERATOR(T)' for the type `T'.
#define declare_ptable(T)						      \
									      \
struct PASSOC(T) {							      \
  char *key;							      	      \
  T *val;								      \
  PASSOC(T)();								      \
};									      \
									      \
class PTABLE(T);							      \
									      \
class PTABLE_ITERATOR(T) {						      \
  PTABLE(T) *p;								      \
  unsigned i;								      \
public:									      \
  PTABLE_ITERATOR(T)(PTABLE(T) *);	/* Initialize an iterator running     \
					   through the given table.  */	      \
  int next(const char **, T **);	/* Fetch the next pair, store the key \
					   and value in arg1 and arg2,	      \
					   respectively, and return 1.  If    \
					   there is no more pair in the	      \
					   table, return 0.  */		      \
};									      \
									      \
class PTABLE(T) {							      \
  PASSOC(T) *v;								      \
  unsigned size;							      \
  unsigned used;							      \
  enum {								      \
    FULL_NUM = 2,							      \
    FULL_DEN = 3,							      \
    INITIAL_SIZE = 17							      \
  };									      \
public:									      \
  PTABLE(T)();				/* Create an empty table.  */	      \
  ~PTABLE(T)();				/* Delete a table, including its      \
					   values.  */			      \
  const char *define(const char *, T *);/* Define the value (arg2) for a key  \
					   (arg1).  Return the copy in the    \
					   table of the key (arg1), or	      \
					   possibly NULL if the value (arg2)  \
					   is NULL.  */			      \
  T *lookup(const char *);		/* Return a pointer to the value of   \
					   the given key, if found in the     \
					   table, or NULL otherwise.  */      \
  T *lookupassoc(const char **);	/* Return a pointer to the value of   \
					   the given key, passed by reference,\
					   and replace the key argument with  \
					   the copy found in the table, if    \
					   the key is found in the table.     \
					   Return NULL otherwise.  */	      \
  friend class PTABLE_ITERATOR(T);					      \
};


// Keys (which are strings) are allocated and freed by PTABLE.
// Values must be allocated by the caller (always using new[], not new)
// and are freed by PTABLE.

// Define the implementations of the members of the types `class PTABLE(T)',
// `struct PASSOC(T)', `class PTABLE_ITERATOR(T)' for the type `T'.
#define implement_ptable(T)						      \
									      \
PASSOC(T)::PASSOC(T)()							      \
: key(0), val(0)							      \
{									      \
}									      \
									      \
PTABLE(T)::PTABLE(T)()							      \
{									      \
  v = new PASSOC(T)[size = INITIAL_SIZE];				      \
  used = 0;								      \
}									      \
									      \
PTABLE(T)::~PTABLE(T)()							      \
{									      \
  for (unsigned i = 0; i < size; i++) {					      \
    a_delete v[i].key;							      \
    a_delete v[i].val;							      \
  }									      \
  a_delete v;								      \
}									      \
									      \
const char *PTABLE(T)::define(const char *key, T *val)			      \
{									      \
  assert(key != 0);							      \
  unsigned long h = hash_string(key);					      \
  unsigned n;								      \
  for (n = unsigned(h % size);					      	      \
       v[n].key != 0;							      \
       n = (n == 0 ? size - 1 : n - 1))					      \
    if (strcmp(v[n].key, key) == 0) {					      \
      a_delete v[n].val;						      \
      v[n].val = val;							      \
      return v[n].key;							      \
    }									      \
  if (val == 0)								      \
    return 0;								      \
  if (used*FULL_DEN >= size*FULL_NUM) {					      \
    PASSOC(T) *oldv = v;						      \
    unsigned old_size = size;						      \
    size = next_ptable_size(size);					      \
    v = new PASSOC(T)[size];						      \
    for (unsigned i = 0; i < old_size; i++)				      \
      if (oldv[i].key != 0) {						      \
	if (oldv[i].val == 0)						      \
	  a_delete oldv[i].key;						      \
	else {								      \
	  unsigned j;							      \
	  for (j = unsigned(hash_string(oldv[i].key) % size);	      	      \
	       v[j].key != 0;						      \
	       j = (j == 0 ? size - 1 : j - 1))				      \
		 ;							      \
	  v[j].key = oldv[i].key;					      \
	  v[j].val = oldv[i].val;					      \
	}								      \
      }									      \
    for (n = unsigned(h % size);					      \
	 v[n].key != 0;							      \
	 n = (n == 0 ? size - 1 : n - 1))				      \
      ;									      \
    a_delete oldv;							      \
  }									      \
  char *temp = new char[strlen(key)+1];					      \
  strcpy(temp, key);							      \
  v[n].key = temp;							      \
  v[n].val = val;							      \
  used++;								      \
  return temp;								      \
}									      \
									      \
T *PTABLE(T)::lookup(const char *key)					      \
{									      \
  assert(key != 0);							      \
  for (unsigned n = unsigned(hash_string(key) % size);			      \
       v[n].key != 0;							      \
       n = (n == 0 ? size - 1 : n - 1))					      \
    if (strcmp(v[n].key, key) == 0)					      \
      return v[n].val;							      \
  return 0;								      \
}									      \
									      \
T *PTABLE(T)::lookupassoc(const char **keyptr)				      \
{									      \
  const char *key = *keyptr;						      \
  assert(key != 0);							      \
  for (unsigned n = unsigned(hash_string(key) % size);			      \
       v[n].key != 0;							      \
       n = (n == 0 ? size - 1 : n - 1))					      \
    if (strcmp(v[n].key, key) == 0) {					      \
      *keyptr = v[n].key;						      \
      return v[n].val;							      \
    }									      \
  return 0;								      \
}									      \
									      \
PTABLE_ITERATOR(T)::PTABLE_ITERATOR(T)(PTABLE(T) *t)			      \
: p(t), i(0)								      \
{									      \
}									      \
									      \
int PTABLE_ITERATOR(T)::next(const char **keyp, T **valp)		      \
{									      \
  unsigned size = p->size;						      \
  PASSOC(T) *v = p->v;							      \
  for (; i < size; i++)							      \
    if (v[i].key != 0) {						      \
      *keyp = v[i].key;							      \
      *valp = v[i].val;							      \
      i++;								      \
      return 1;								      \
    }									      \
  return 0;								      \
}

// end of ptable.h
