#!/bin/sh

# $DragonFly: doc/share/web2c/pdf_geninfo.sh,v 1.1.1.1 2004/04/02 09:37:19 hmp Exp $
#
# Script for generating meta information for a PDF
# version of a document.
#
# Author:	Hiten Pandya <hmp@backplane.com>

# . $DOC_META_RC_FILE

/bin/cat << _EOT
\pdfoutput=1
\pdfcompresslevel=9
\pdfinfo
{
  /Title	($DOC_TITLE)
  /Creator	($DOC_PRODUCER)
  /Author	($DOC_AUTHORS)
}
\pdfpagewidth=210 true mm
\pdfpageheight=297 true mm
_EOT
#\pdfhorigin 	(1 true in)
#\pdfvorigin 	(1 true in)
#\pdfpagewidth	(210 true mm)
#\pdfpageheight	(297 true mm)
#_EOT
