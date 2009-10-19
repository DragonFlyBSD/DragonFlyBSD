/* Data structures associated with tracepoints in GDB.
   Copyright (C) 1997, 1998, 1999, 2000, 2007, 2008, 2009
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

#if !defined (TRACEPOINT_H)
#define TRACEPOINT_H 1

/* The data structure for an action: */
struct action_line
  {
    struct action_line *next;
    char *action;
  };

enum actionline_type
  {
    BADLINE = -1,
    GENERIC = 0,
    END = 1,
    STEPPING = 2
  };

extern unsigned long trace_running_p;

/* A hook used to notify the UI of tracepoint operations.  */

void (*deprecated_trace_find_hook) (char *arg, int from_tty);
void (*deprecated_trace_start_stop_hook) (int start, int from_tty);

int get_traceframe_number (void);
void free_actions (struct breakpoint *);
enum actionline_type validate_actionline (char **, struct breakpoint *);

extern void end_actions_pseudocommand (char *args, int from_tty);
extern void while_stepping_pseudocommand (char *args, int from_tty);

#endif	/* TRACEPOINT_H */
