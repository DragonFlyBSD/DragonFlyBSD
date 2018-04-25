/*
 * include.h - includes all important files for Phantasia
 */

#include <ctype.h>
#ifndef COMPILING_SETUP
#include <curses.h>
#endif
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#ifdef COMPILING_SETUP
/* XXX this used to be provided by curses.h */
#include <stdio.h>	/* for FILE */
#include <stdbool.h>	/* for bool */
#ifndef TRUE
#define	TRUE	true
#endif
#endif

#include "macros.h"
#include "phantdefs.h"
#include "phantstruct.h"
#include "phantglobs.h"
#include "pathnames.h"
