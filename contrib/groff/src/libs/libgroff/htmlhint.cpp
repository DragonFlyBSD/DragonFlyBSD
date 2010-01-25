/* Copyright (C) 2000, 2001, 2002, 2008, 2009
     Free Software Foundation, Inc.
     Written by Gaius Mulley (gaius@glam.ac.uk)

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

#include "lib.h"

#include <stddef.h>
#include <stdlib.h>

#include "nonposix.h"
#include "stringclass.h"
#include "html-strings.h"

/*
 *  This file contains a very simple set of routines which might
 *  be shared by preprocessors.  It allows a preprocessor to indicate
 *  when an inline image should be created.
 *  This string is intercepted by pre-grohtml and substituted for
 *  the image name and suppression escapes.
 *
 *  pre-html runs troff twice, once with -Thtml (or -Txhtml) and once
 *  with -Tps.  `troff -Thtml' (and `troff -Txhtml') emits a
 *  <src='image'.png> tag and the postscript device driver works out
 *  the min/max limits of the graphic region.  These region limits are
 *  read by pre-html and an image is generated via
 *
 *    troff -Tps -> gs -> png
 */

/*
 *  html_begin_suppress - emit a start of image tag which will be seen
 *                        by pre-html.
 */
void html_begin_suppress()
{
  put_string(HTML_IMAGE_INLINE_BEGIN, stdout);
}

/*
 *  html_end_suppress - emit an end of image tag which will be seen
 *                      by pre-html.
 */
void html_end_suppress()
{
  put_string(HTML_IMAGE_INLINE_END, stdout);
}
