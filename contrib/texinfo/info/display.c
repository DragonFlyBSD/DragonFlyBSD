/* display.c -- How to display Info windows.
   $Id: display.c,v 1.16 2008/06/11 09:55:41 gray Exp $

   Copyright (C) 1993, 1997, 2003, 2004, 2006, 2007, 2008
   Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Originally written by Brian Fox (bfox@ai.mit.edu). */

#include "info.h"
#include "display.h"

extern int info_any_buffered_input_p (void); /* Found in session.c. */

static void free_display (DISPLAY_LINE **display);
static DISPLAY_LINE **make_display (int width, int height);

void handle_tag (char *tag);
void handle_tag_start (char *tag);
void handle_tag_end (char *tag);

/* An array of display lines which tell us what is currently visible on
   the display.  */
DISPLAY_LINE **the_display = NULL;

/* Non-zero means do no output. */
int display_inhibited = 0;

/* Initialize THE_DISPLAY to WIDTH and HEIGHT, with nothing in it. */
void
display_initialize_display (int width, int height)
{
  free_display (the_display);
  the_display = make_display (width, height);
  display_clear_display (the_display);
}

/* Clear all of the lines in DISPLAY making the screen blank. */
void
display_clear_display (DISPLAY_LINE **display)
{
  register int i;

  for (i = 0; display[i]; i++)
    {
      display[i]->text[0] = '\0';
      display[i]->textlen = 0;
      display[i]->inverse = 0;
    }
}

/* Non-zero if we didn't completely redisplay a window. */
int display_was_interrupted_p = 0;

/* Update the windows pointed to by WINDOW in the_display.  This actually
   writes the text on the screen. */
void
display_update_display (WINDOW *window)
{
  register WINDOW *win;

  display_was_interrupted_p = 0;

  /* For every window in the list, check contents against the display. */
  for (win = window; win; win = win->next)
    {
      /* Only re-display visible windows which need updating. */
      if (((win->flags & W_WindowVisible) == 0) ||
          ((win->flags & W_UpdateWindow) == 0) ||
          (win->height == 0))
        continue;

      display_update_one_window (win);
      if (display_was_interrupted_p)
        break;
    }

  /* Always update the echo area. */
  display_update_one_window (the_echo_area);
}

void
handle_tag_start (char *tag)
{
  /* TODO really handle this tag.  */
  return;
}

void
handle_tag_end (char *tag)
{
  /* TODO really handle this tag.  */
  return;
}

void
handle_tag (char *tag)
{
    if (tag[0] == '/')
      {
	tag++;
	handle_tag_end (tag);
      }
    else
      handle_tag_start (tag);
}


struct display_node_closure {
  WINDOW *win;
  DISPLAY_LINE **display;
};

static int
find_diff (const char *a, size_t alen, const char *b, size_t blen, int *ppos)
{
  mbi_iterator_t itra, itrb;
  int i = 0;
  int pos = 0;
  
  for (i = 0, mbi_init (itra, a, alen), mbi_init (itrb, b, blen);
       mbi_avail (itra) && mbi_avail (itrb);
       mbi_advance (itra), mbi_advance (itrb))
    {
      if (mb_cmp (mbi_cur (itra), mbi_cur (itrb)))
	break;
      pos += mb_len (mbi_cur (itra));
    }
  *ppos = pos;
  return i;
}

int
display_node_text(void *closure, size_t line_index,
		  const char *src_line,
		  char *printed_line, size_t pl_index, size_t pl_count)
{
  struct display_node_closure *dn = closure;
  WINDOW *win = dn->win;
  DISPLAY_LINE **display = dn->display;
  DISPLAY_LINE *entry = display[win->first_row + line_index];

  /* We have the exact line as it should appear on the screen.
     Check to see if this line matches the one already appearing
     on the screen. */

  /* If the window is very small, entry might be NULL. */
  if (entry)
    {
      int i, off;
	      
      /* If the screen line is inversed, then we have to clear
	 the line from the screen first.  Why, I don't know.
	 (But don't do this if we have no visible entries, as can
	 happen if the window is shrunk very small.)  */
      if (entry->inverse
	  /* Need to erase the line if it has escape sequences.  */
	  || (raw_escapes_p && mbschr (entry->text, '\033') != 0))
	{
	  terminal_goto_xy (0, win->first_row + line_index);
	  terminal_clear_to_eol ();
	  entry->inverse = 0;
	  entry->text[0] = '\0';
	  entry->textlen = 0;
	}

      i = find_diff (printed_line, pl_index,
		     entry->text, strlen (entry->text), &off);

      /* If the lines are not the same length, or if they differed
	 at all, we must do some redrawing. */
      if (i != pl_count || pl_count != entry->textlen)
	{
	  /* Move to the proper point on the terminal. */
	  terminal_goto_xy (i, win->first_row + line_index);
	  /* If there is any text to print, print it. */
	  if (i != pl_count)
	    terminal_put_text (printed_line + i);
	  
	  /* If the printed text didn't extend all the way to the edge
	     of the window, and text was appearing between here and the
	     edge of the window, clear from here to the end of the
	     line. */
	  if ((pl_count < win->width && pl_count < entry->textlen)
	      || entry->inverse)
	    terminal_clear_to_eol ();
	  
	  fflush (stdout);
	  
	  /* Update the display text buffer. */
	  if (strlen (printed_line) > (unsigned int) screenwidth)
	    /* printed_line[] can include more than screenwidth
	       characters, e.g. if multibyte encoding is used or
	       if we are under -R and there are escape sequences
	       in it.  However, entry->text was allocated (in
	       display_initialize_display) for screenwidth
	       bytes only.  */
	    entry->text = xrealloc (entry->text, strlen (printed_line) + 1);
	  strcpy (entry->text + off, printed_line + off);
	  entry->textlen = pl_count;
	  
	  /* Lines showing node text are not in inverse.  Only modelines
	     have that distinction. */
	  entry->inverse = 0;
	}
    }

  /* A line has been displayed, and the screen reflects that state.
     If there is typeahead pending, then let that typeahead be read
     now, instead of continuing with the display. */
  if (info_any_buffered_input_p ())
    {
      display_was_interrupted_p = 1;
      return 1;
    }

  if (line_index + 1 == win->height)
    return 1;

  return 0;
}

void
display_update_one_window (WINDOW *win)
{
  size_t line_index = 0;
  DISPLAY_LINE **display = the_display;

  /* If display is inhibited, that counts as an interrupted display. */
  if (display_inhibited)
    display_was_interrupted_p = 1;

  /* If the window has no height, or display is inhibited, quit now.
     Strictly speaking, it should only be necessary to test if the
     values are equal to zero, since window_new_screen_size should
     ensure that the window height/width never becomes negative, but
     since historically this has often been the culprit for crashes, do
     our best to be doubly safe.  */
  if (win->height <= 0 || win->width <= 0 || display_inhibited)
    return;

  /* If the window's first row doesn't appear in the_screen, then it
     cannot be displayed.  This can happen when the_echo_area is the
     window to be displayed, and the screen has shrunk to less than one
     line. */
  if ((win->first_row < 0) || (win->first_row > the_screen->height))
    return;

  if (win->node && win->line_starts)
    {
      struct display_node_closure dnc;

      dnc.win = win;
      dnc.display = the_display;
      
      line_index = process_node_text (win, win->line_starts[win->pagetop],
				      1,
				      display_node_text,
				      &dnc);
      if (display_was_interrupted_p)
	return;
    }
  
  /* We have reached the end of the node or the end of the window.  If it
     is the end of the node, then clear the lines of the window from here
     to the end of the window. */
  for (; line_index < win->height; line_index++)
    {
      DISPLAY_LINE *entry = display[win->first_row + line_index];

      /* If this line has text on it then make it go away. */
      if (entry && entry->textlen)
        {
          entry->textlen = 0;
          entry->text[0] = '\0';

          terminal_goto_xy (0, win->first_row + line_index);
          terminal_clear_to_eol ();
        }
    }

  /* Finally, if this window has a modeline it might need to be redisplayed.
     Check the window's modeline against the one in the display, and update
     if necessary. */
  if (!(win->flags & W_InhibitMode))
    {
      window_make_modeline (win);
      line_index = win->first_row + win->height;

      /* This display line must both be in inverse, and have the same
         contents. */
      if ((!display[line_index]->inverse) ||
          (strcmp (display[line_index]->text, win->modeline) != 0))
        {
          terminal_goto_xy (0, line_index);
          terminal_begin_inverse ();
          terminal_put_text (win->modeline);
          terminal_end_inverse ();
          strcpy (display[line_index]->text, win->modeline);
          display[line_index]->inverse = 1;
          display[line_index]->textlen = strlen (win->modeline);
          fflush (stdout);
        }
    }

  fflush (stdout);

  /* Okay, this window doesn't need updating anymore. */
  win->flags &= ~W_UpdateWindow;
}

/* Scroll the region of the_display starting at START, ending at END, and
   moving the lines AMOUNT lines.  If AMOUNT is less than zero, the lines
   are moved up in the screen, otherwise down.  Actually, it is possible
   for no scrolling to take place in the case that the terminal doesn't
   support it.  This doesn't matter to us. */
void
display_scroll_display (int start, int end, int amount)
{
  register int i, last;
  DISPLAY_LINE *temp;

  /* If this terminal cannot do scrolling, give up now. */
  if (!terminal_can_scroll)
    return;

  /* If there isn't anything displayed on the screen because it is too
     small, quit now. */
  if (!the_display[0])
    return;

  /* If there is typeahead pending, then don't actually do any scrolling. */
  if (info_any_buffered_input_p ())
    return;

  /* Do it on the screen. */
  terminal_scroll_terminal (start, end, amount);

  /* Now do it in the display buffer so our contents match the screen. */
  if (amount > 0)
    {
      last = end + amount;

      /* Shift the lines to scroll right into place. */
      for (i = 0; i < (end - start); i++)
        {
          temp = the_display[last - i];
          the_display[last - i] = the_display[end - i];
          the_display[end - i] = temp;
        }

      /* The lines have been shifted down in the buffer.  Clear all of the
         lines that were vacated. */
      for (i = start; i != (start + amount); i++)
        {
          the_display[i]->text[0] = '\0';
          the_display[i]->textlen = 0;
          the_display[i]->inverse = 0;
        }
    }

  if (amount < 0)
    {
      last = start + amount;
      for (i = 0; i < (end - start); i++)
        {
          temp = the_display[last + i];
          the_display[last + i] = the_display[start + i];
          the_display[start + i] = temp;
        }

      /* The lines have been shifted up in the buffer.  Clear all of the
         lines that are left over. */
      for (i = end + amount; i != end; i++)
        {
          the_display[i]->text[0] = '\0';
          the_display[i]->textlen = 0;
          the_display[i]->inverse = 0;
        }
    }
}

/* Try to scroll lines in WINDOW.  OLD_PAGETOP is the pagetop of WINDOW before
   having had its line starts recalculated.  OLD_STARTS is the list of line
   starts that used to appear in this window.  OLD_COUNT is the number of lines
   that appear in the OLD_STARTS array. */
void
display_scroll_line_starts (WINDOW *window, int old_pagetop,
    char **old_starts, int old_count)
{
  register int i, old, new;     /* Indices into the line starts arrays. */
  int last_new, last_old;       /* Index of the last visible line. */
  int old_first, new_first;     /* Index of the first changed line. */
  int unchanged_at_top = 0;
  int already_scrolled = 0;

  /* Locate the first line which was displayed on the old window. */
  old_first = old_pagetop;
  new_first = window->pagetop;

  /* Find the last line currently visible in this window. */
  last_new = window->pagetop + (window->height - 1);
  if (last_new > window->line_count)
    last_new = window->line_count - 1;

  /* Find the last line which used to be currently visible in this window. */
  last_old = old_pagetop + (window->height - 1);
  if (last_old > old_count)
    last_old = old_count - 1;

  for (old = old_first, new = new_first;
       old < last_old && new < last_new;
       old++, new++)
    if (old_starts[old] != window->line_starts[new])
      break;
    else
      unchanged_at_top++;

  /* Loop through the old lines looking for a match in the new lines. */
  for (old = old_first + unchanged_at_top; old < last_old; old++)
    {
      for (new = new_first; new < last_new; new++)
        if (old_starts[old] == window->line_starts[new])
          {
            /* Find the extent of the matching lines. */
            for (i = 0; (old + i) < last_old; i++)
              if (old_starts[old + i] != window->line_starts[new + i])
                break;

            /* Scroll these lines if there are enough of them. */
            {
              int start, end, amount;

              start = (window->first_row
                       + ((old + already_scrolled) - old_pagetop));
              amount = new - (old + already_scrolled);
              end = window->first_row + window->height;

              /* If we are shifting the block of lines down, then the last
                 AMOUNT lines will become invisible.  Thus, don't bother
                 scrolling them. */
              if (amount > 0)
                end -= amount;

              if ((end - start) > 0)
                {
                  display_scroll_display (start, end, amount);

                  /* Some lines have been scrolled.  Simulate the scrolling
                     by offsetting the value of the old index. */
                  old += i;
                  already_scrolled += amount;
                }
            }
          }
    }
}

/* Move the screen cursor to directly over the current character in WINDOW. */
void
display_cursor_at_point (WINDOW *window)
{
  int vpos, hpos;

  vpos = window_line_of_point (window) - window->pagetop + window->first_row;
  hpos = window_get_cursor_column (window);
  terminal_goto_xy (hpos, vpos);
  fflush (stdout);
}

/* **************************************************************** */
/*                                                                  */
/*                   Functions Static to this File                  */
/*                                                                  */
/* **************************************************************** */

/* Make a DISPLAY_LINE ** with width and height. */
static DISPLAY_LINE **
make_display (int width, int height)
{
  register int i;
  DISPLAY_LINE **display;

  display = xmalloc ((1 + height) * sizeof (DISPLAY_LINE *));

  for (i = 0; i < height; i++)
    {
      display[i] = xmalloc (sizeof (DISPLAY_LINE));
      display[i]->text = xmalloc (1 + width);
      display[i]->textlen = 0;
      display[i]->inverse = 0;
    }
  display[i] = NULL;
  return display;
}

/* Free the storage allocated to DISPLAY. */
static void
free_display (DISPLAY_LINE **display)
{
  register int i;
  register DISPLAY_LINE *display_line;

  if (!display)
    return;

  for (i = 0; (display_line = display[i]); i++)
    {
      free (display_line->text);
      free (display_line);
    }
  free (display);
}

