/* $DragonFly: src/usr.sbin/pcvt/vttest/Attic/esc.c,v 1.2 2005/02/20 17:19:11 asmodai Exp $ */

#include "header.h"

println(s) char *s; {
  printf("%s\n", s);
}

esc(s) char *s; {
  printf("%c%s", 27, s);
}

esc2(s1, s2) char s1, s2; {
  printf("%c%s%s", 27, s1, s2);
}

brcstr(ps, c) char *ps, c; {
  printf("%c[%s%c", 27, ps, c);
}

brc(pn,c) int pn; char c; {
  printf("%c[%d%c", 27, pn, c);
}

brc2(pn1, pn2 ,c) int pn1, pn2; char c; {
  printf("%c[%d;%d%c", 27, pn1, pn2, c);
}

cub(pn) int pn; {  /* Cursor Backward */
  brc(pn,'D');
}
cud(pn) int pn; {  /* Cursor Down */
  brc(pn,'B');
}
cuf(pn) int pn; {  /* Cursor Forward */
  brc(pn,'C');
}
cup(pn1, pn2) int pn1, pn2; {  /* Cursor Position */
  brc2(pn1, pn2, 'H');
}
cuu(pn) int pn; {  /* Cursor Up */
  brc(pn,'A');
}
da() {  /* Device Attributes */
  brc(0,'c');
}
decaln() {  /* Screen Alignment Display */
  esc("#8");
}
decdhl(lower) int lower; {  /* Double Height Line (also double width) */
  if (lower) esc("#4");
  else       esc("#3");
}
decdwl() {  /* Double Wide Line */
  esc("#6");
}
deckpam() {  /* Keypad Application Mode */
  esc("=");
}
deckpnm() {  /* Keypad Numeric Mode */
  esc(">");
}
decll(ps) char *ps; {  /* Load LEDs */
  brcstr(ps, 'q');
}
decrc() {  /* Restore Cursor */
  esc("8");
}
decreqtparm(pn) int pn; {  /* Request Terminal Parameters */
  brc(pn,'x');
}
decsc() {  /* Save Cursor */
  esc("7");
}
decstbm(pn1, pn2) int pn1, pn2; {  /* Set Top and Bottom Margins */
  if (pn1 || pn2) brc2(pn1, pn2, 'r');
  else            esc("[r");
  /* Good for >24-line terminals */
}
decswl() {  /* Single With Line */
  esc("#5");
}
dectst(pn) int pn; {  /* Invoke Confidence Test */
  brc2(2, pn, 'y');
}
dsr(pn) int pn; {  /* Device Status Report */
  brc(pn, 'n');
}
ed(pn) int pn; {  /* Erase in Display */
  brc(pn, 'J');
}
el(pn) int pn; {  /* Erase in Line */
  brc(pn,'K');
}
hts() {  /* Horizontal Tabulation Set */
  esc("H");
}
hvp(pn1, pn2) int pn1, pn2; {  /* Horizontal and Vertical Position */
  brc2(pn1, pn2, 'f');
}
ind() {  /* Index */
  esc("D");
}
nel() {  /* Next Line */
  esc("E");
}
ri() {  /* Reverse Index */
  esc("M");
}
ris() { /*  Reset to Initial State */
  esc("c");
}
rm(ps) char *ps; {  /* Reset Mode */
  brcstr(ps, 'l');
}
scs(g,c) int g; char c; {  /* Select character Set */
  printf("%c%c%c%c%c%c%c", 27, g ? ')' : '(', c,
                           27, g ? '(' : ')', 'B',
			   g ? 14 : 15);
}
sgr(ps) char *ps; {  /* Select Graphic Rendition */
  brcstr(ps, 'm');
}
sm(ps) char *ps; {  /* Set Mode */
  brcstr(ps, 'h');
}
tbc(pn) int pn; {  /* Tabulation Clear */
  brc(pn, 'g');
}

vt52cup(l,c) int l,c; {
  printf("%cY%c%c", 27, l + 31, c + 31);
}

char inchar() {

  /*
   *   Wait until a character is typed on the terminal
   *   then read it, without waiting for CR.
   */

  int lval, waittime, getpid(); static int val; char ch;

  fflush(stdout);
  lval = val;
  brkrd = 0;
  reading = 1;
  read(0,&ch,1);
  reading = 0;
  if (brkrd)
    val = 0177;
  else
    val = ch;
  if ((val==0177) && (val==lval))
    kill(getpid(), (int) SIGTERM);
  return(val);
}

char *instr() {

  /*
   *   Get an unfinished string from the terminal:
   *   wait until a character is typed on the terminal,
   *   then read it, and all other available characters.
   *   Return a pointer to that string.
   */


  int i, val, crflag; long l1; char ch;
  static char result[80];

  i = 0;
  result[i++] = inchar();
/* Wait 0.1 seconds (1 second in vanilla UNIX) */
  zleep(100);
  fflush(stdout);
  while(ioctl(0,FIONREAD,&l1), l1 > 0L) {
    while(l1-- > 0L) {
      read(0,result+i,1);
      if (i++ == 78) goto out1;
    }
  }
out1:
  result[i] = '\0';
  return(result);
}

ttybin(bin) int bin; {
}


trmop(fc,arg) int fc, arg; {
}

inputline(s) char *s; {
  scanf("%s",s);
}

inflush() {

  /*
   *   Flush input buffer, make sure no pending input character
   */

  int val;

  long l1;
  ioctl (0, FIONREAD, &l1);
  while(l1-- > 0L) read(0,&val,1);
}

zleep(t) int t; {

/*
 *    Sleep and do nothing (don't waste CPU) for t milliseconds
 */

  t = t / 1000;
  if (t == 0) t = 1;
  sleep(t);		/* UNIX can only sleep whole seconds */
}
