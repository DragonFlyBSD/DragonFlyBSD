// Copyright: (C) 2009 Gilles Muller, Julia Lawall, INRIA, DIKU.  GPLv2.
//
// -no_includes -include_headers
//

@ disable is_zero @
expression *E;
@@

  E ==
- 0
+ NULL

@ disable is_zero @
expression *E;
@@

- 0
+ NULL
  == E

@ disable isnt_zero @
expression *E;
@@

  E !=
- 0
+ NULL

@ disable isnt_zero @
expression *E;
@@

- 0
+ NULL
  != E

@ disable is_zero @
expression *E;
@@

  E =
- 0
+ NULL
