Disable _rl_enable_bracketed_paste in case of cons25 too.
Avoids echoing 2004h

--- terminal.c.orig	2020-10-08 15:06:34 UTC
+++ terminal.c
@@ -465,7 +465,7 @@ _rl_init_terminal_io (const char *termin
   if (term == 0)
     term = "dumb";
 
-  dumbterm = STREQ (term, "dumb");
+  dumbterm = STREQ (term, "dumb") || STREQ (term, "cons25");
 
 #ifdef __MSDOS__
   _rl_term_im = _rl_term_ei = _rl_term_ic = _rl_term_IC = (char *)NULL;
