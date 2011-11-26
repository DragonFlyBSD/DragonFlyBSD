// -*- C -*-
/* Copyright (C) 2000, 2001, 2003, 2004, 2009
     Free Software Foundation, Inc.
     Written by Gaius Mulley (gaius@glam.ac.uk).

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


#define eof              (char)-1


/*
 *  defines the class and methods implemented within pushback.cpp
 */

class pushBackBuffer
{
 private:
  char       *charStack;
  int         stackPtr;   /* index to push back stack        */
  int         debug;
  int         verbose;
  int         eofFound;
  char       *fileName;
  int         lineNo;
  int         stdIn;

 public:
         pushBackBuffer (char *);
  ~      pushBackBuffer ();
  char   getPB          (void);
  char   putPB          (char ch);
  void   skipUntilToken (void);
  void   skipToNewline  (void);
  double readNumber     (void);
  int    readInt        (void);
  char  *readString     (void);
  int    isString       (const char *string);
};


